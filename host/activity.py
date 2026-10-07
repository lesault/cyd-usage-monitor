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
                    int(r["ts"])
                    rows.append(r)
                except (ValueError, KeyError, TypeError):
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


def summarize(rows, now, tz):
    """Return {"idle": secs|None, "today": usd, "n": sessions, "days": [7 floats]}."""
    rows = sorted(rows, key=lambda r: r["ts"])
    today = day_index(now, tz)

    # Last real change: heartbeat rows with identical values don't count.
    last_change, seen = None, {}
    for r in rows:
        sig = (r.get("cost"), r.get("la"), r.get("lr"), r.get("s"), r.get("w"))
        sid = r.get("sid", "")
        if seen.get(sid) != sig:
            last_change = r["ts"]
            seen[sid] = sig

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

    return {
        "idle": None if last_change is None else max(0, int(now - last_change)),
        "today": round(total, 2),
        "n": n,
        "days": [round(d, 1) for d in days],
    }


class Activity:
    """Caches the parsed history until the file changes."""

    def __init__(self, path=HISTORY):
        self.path = path
        self._key = None
        self._rows = []

    def rows(self):
        try:
            st = os.stat(self.path)
            key = (st.st_mtime_ns, st.st_size)
        except OSError:
            key = None
        if key != self._key:
            self._rows = load(self.path)
            self._key = key
        return self._rows

    def summary(self, now, tz):
        return summarize(self.rows(), now, tz)
