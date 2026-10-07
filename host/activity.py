"""Derive activity stats from ~/.claude-cyd/history.jsonl.

history.jsonl rows are appended by statusline.py:
    {"ts", "sid", "cost", "la", "lr", "s", "w", "dur"}
where cost is cumulative USD for that session and s/w are the 5-hour and weekly
used percentages. Nothing here reads Claude transcripts.
"""
import contextlib
import json
import os

try:
    import fcntl
except ImportError:  # not on Windows; the bridge is macOS-only anyway
    fcntl = None

DIR = os.path.expanduser("~/.claude-cyd")
HISTORY = os.path.join(DIR, "history.jsonl")
KEEP_DAYS = 14


def day_index(ts, tz):
    return int((ts + tz) // 86400)


@contextlib.contextmanager
def lock(path=HISTORY):
    """Exclusive lock around history changes, so a prune can't drop a concurrent append.

    The appender (statusline.py) and the pruner (bridge.py) both take it. If locking
    isn't available the caller just proceeds unlocked.
    """
    f = None
    try:
        if fcntl is not None:
            f = open(path + ".lock", "a")
            fcntl.flock(f, fcntl.LOCK_EX)
    except OSError:
        if f:
            f.close()
        f = None
    try:
        yield
    finally:
        if f:
            f.close()  # closing releases the flock


def load(path=HISTORY):
    rows = []
    try:
        with open(path) as f:
            for line in f:
                try:
                    r = json.loads(line)
                    r["ts"] = int(r["ts"])
                    rows.append(r)
                except (ValueError, KeyError, TypeError, OverflowError):
                    continue
    except OSError:
        pass
    rows.sort(key=lambda r: r["ts"])
    return rows


def prune(path=HISTORY, now=None, tz=0, keep_days=KEEP_DAYS):
    """Drop rows older than keep_days (atomic rewrite)."""
    import time
    now = int(now if now is not None else time.time())
    with lock(path):
        rows = load(path)
        keep = [r for r in rows if day_index(r["ts"], tz) > day_index(now, tz) - keep_days]
        if len(keep) == len(rows):
            return
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            for r in keep:
                f.write(json.dumps(r, separators=(",", ":")) + "\n")
        os.replace(tmp, path)


def _began_today(first, today, tz):
    """True if the session behind its first row (today) provably started today.

    Rows carry the session's wall-clock age ("dur", seconds), so ts - dur is its start.
    """
    dur = first.get("dur")
    if not isinstance(dur, (int, float)) or isinstance(dur, bool) or dur < 0:
        return False
    return day_index(first["ts"] - dur, tz) >= today


def last_change(rows):
    """Timestamp of the last real change: heartbeat rows with identical values don't count."""
    last, seen = None, {}
    for r in sorted(rows, key=lambda r: r["ts"]):
        sig = (r.get("cost"), r.get("la"), r.get("lr"), r.get("s"), r.get("w"))
        sid = r.get("sid", "")
        if seen.get(sid) != sig:
            last = r["ts"]
            seen[sid] = sig
    return last


def aggregate(rows, now, tz):
    """Cost today, sessions today and per-day weekly usage: {"today", "n", "days"}."""
    rows = sorted(rows, key=lambda r: r["ts"])
    today = day_index(now, tz)

    # Cost today: cumulative per-session cost minus where that session stood before today.
    by_sid = {}
    for r in rows:
        by_sid.setdefault(r.get("sid", ""), []).append(r)
    total, n = 0.0, 0
    for rs in by_sid.values():
        todays = [r for r in rs if day_index(r["ts"], tz) == today]
        if not todays:
            continue
        n += 1
        costs = [r["cost"] for r in todays if r.get("cost") is not None]
        if not costs:
            continue
        before = [r["cost"] for r in rs if day_index(r["ts"], tz) < today and r.get("cost") is not None]
        if before:
            base = before[-1]
        elif _began_today(todays[0], today, tz):
            base = 0.0  # a new session: everything it has cost so far was spent today
        else:
            base = costs[0]  # history starts mid-session: we can't see what came before
        total += max(0.0, max(costs) - base)

    # Weekly allowance used per day; a drop means the window reset, so count from zero.
    days, prev = [0.0] * 7, None
    for r in rows:
        w = r.get("w")
        if w is None:
            continue
        if prev is not None:
            idx = 6 - (today - day_index(r["ts"], tz))
            if 0 <= idx < 7:
                days[idx] += (w - prev) if w >= prev else w
        prev = w

    return {"today": round(total, 2), "n": n, "days": [round(d, 1) for d in days]}


def idle_secs(last, now):
    return None if last is None else max(0, int(now - last))


def summarize(rows, now, tz):
    """Return {"idle": secs|None, "today": usd, "n": sessions, "days": [7 floats]}."""
    out = aggregate(rows, now, tz)
    out["idle"] = idle_secs(last_change(rows), now)
    return out


def capped_append(path, text, max_bytes=64 * 1024):
    """Append text to a diagnostic log, dropping the oldest half once it passes max_bytes."""
    try:
        if os.path.getsize(path) > max_bytes:
            with open(path, "rb") as f:
                f.seek(-max_bytes // 2, os.SEEK_END)
                tail = f.read().split(b"\n", 1)[-1]  # start on a whole line
            tmp = path + ".tmp"
            with open(tmp, "wb") as f:
                f.write(tail)
            os.replace(tmp, path)
    except OSError:
        pass
    with open(path, "a") as f:
        f.write(text)


def truncate_if_large(path, max_bytes=1024 * 1024):
    """Empty a log that something else holds open (launchd's stdout) once it gets big."""
    try:
        if os.path.getsize(path) > max_bytes:
            os.truncate(path, 0)
    except OSError:
        pass


class Activity:
    """Keeps the parsed history in memory, reading only what was appended since last time."""

    def __init__(self, path=HISTORY):
        self.path = path
        self._rows = []
        self._ino = None
        self._pos = 0          # bytes consumed so far (always ends on a newline)
        self._version = 0      # bumped whenever the rows change
        self._last = None      # last_change(), cached for _last_v
        self._last_v = None
        self._agg = None       # ((version, today, tz), aggregate) cache

    def _reset(self):
        self._rows, self._pos = [], 0
        self._version += 1

    def rows(self):
        try:
            st = os.stat(self.path)
        except OSError:
            if self._rows or self._pos:
                self._reset()
            self._ino = None
            return self._rows
        if st.st_ino != self._ino or st.st_size < self._pos:  # replaced (pruned) or truncated
            self._reset()
            self._ino = st.st_ino
        if st.st_size > self._pos:
            try:
                with open(self.path, "rb") as f:
                    f.seek(self._pos)
                    chunk = f.read()
            except OSError:
                return self._rows
            end = chunk.rfind(b"\n") + 1  # leave a half-written last line for next time
            if end:
                new = []
                for line in chunk[:end].decode(errors="replace").splitlines():
                    try:
                        r = json.loads(line)
                        r["ts"] = int(r["ts"])
                        new.append(r)
                    except (ValueError, KeyError, TypeError, OverflowError):
                        continue
                self._pos += end
                if new:
                    seq = [r["ts"] for r in ([self._rows[-1]] if self._rows else []) + new]
                    ordered = all(x <= y for x, y in zip(seq, seq[1:]))
                    self._rows.extend(new)
                    if not ordered:
                        self._rows.sort(key=lambda r: r["ts"])
                    self._version += 1
        return self._rows

    def summary(self, now, tz):
        rows = self.rows()
        if self._last_v != self._version:
            self._last, self._last_v = last_change(rows), self._version
        key = (self._version, day_index(now, tz), tz)
        if self._agg is None or self._agg[0] != key:
            self._agg = (key, aggregate(rows, now, tz))
        out = dict(self._agg[1])
        out["idle"] = idle_secs(self._last, now)
        return out
