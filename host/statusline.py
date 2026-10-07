#!/usr/bin/env python3
"""Claude Code statusLine command for the CYD usage monitor.

Reads the statusline JSON on stdin, writes a normalised snapshot to
~/.claude-cyd/state.json (atomically) and keeps the last raw payload in
~/.claude-cyd/raw.json for debugging. Prints a short line for the terminal.
"""
import json
import os
import sys
import tempfile
import time

import activity

DIR = os.path.expanduser("~/.claude-cyd")


def dig(d, *path):
    for k in path:
        if not isinstance(d, dict):
            return None
        d = d.get(k)
    return d


def num(v):
    """v if it is a finite-looking number (not a bool or string), else None."""
    return v if isinstance(v, (int, float)) and not isinstance(v, bool) else None


def window(raw, key):
    w = dig(raw, "rate_limits", key)
    if not isinstance(w, dict):
        return None
    return clean_window(w.get("used_percentage"), w.get("resets_at"))


def clean_window(p, r):
    """{"p", "r"} from a percentage and reset time, or None if either is unusable."""
    try:
        return {"p": float(p), "r": int(r)}
    except (TypeError, ValueError, OverflowError):
        return None


def atomic_write(path, text):
    fd, tmp = tempfile.mkstemp(dir=DIR)
    with os.fdopen(fd, "w") as f:
        f.write(text)
    os.replace(tmp, path)


HEARTBEAT = 120  # seconds between unchanged history rows


def append_history(now, sid, state):
    """Append a row to history.jsonl when something changed (or as a heartbeat).

    O_APPEND single writes keep concurrent Claude Code sessions from corrupting it, and the
    history lock stops the bridge's daily prune from dropping a row mid-rewrite.
    """
    row = {
        "ts": now, "sid": sid[:8], "cost": state["cost"], "la": state["la"], "lr": state["lr"],
        "s": state["s"]["p"] if state["s"] else None,
        "w": state["w"]["p"] if state["w"] else None,
        "dur": state["dur"],
    }
    path = os.path.join(DIR, "history.jsonl")
    with activity.lock(path):
        _append_row(path, row, now)


def _append_row(path, row, now):
    sig = ("cost", "la", "lr", "s", "w")
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            f.seek(max(0, f.tell() - 4096))
            tail = f.read().decode(errors="ignore").splitlines()
        for line in reversed(tail):
            try:
                last = json.loads(line)
            except ValueError:
                continue
            if last.get("sid") == row["sid"]:
                if all(last.get(k) == row[k] for k in sig) and now - last["ts"] < HEARTBEAT:
                    return
                break
    except OSError:
        pass
    fd = os.open(path, os.O_APPEND | os.O_CREAT | os.O_WRONLY, 0o600)
    try:
        os.write(fd, (json.dumps(row, separators=(",", ":")) + "\n").encode())
    finally:
        os.close(fd)


def normalise(raw, prev, now):
    """Turn a statusline payload (dict) and the previous state into the new state dict."""
    prev = prev if isinstance(prev, dict) else {}
    wins = {}
    for out, key in (("s", "five_hour"), ("w", "seven_day")):
        win = window(raw, key)
        old = prev.get(out)
        old = clean_window(old.get("p"), old.get("r")) if isinstance(old, dict) else None
        if win is None:
            # Claude Code sometimes omits a window; keep the last one until it resets.
            activity.capped_append(os.path.join(DIR, "missing.log"), "%d missing %s\n" % (now, key))
            if old and old["r"] > now:
                win = old
        elif old and old["r"] == win["r"] and old["p"] > win["p"]:
            # Usage only rises within a window, so a lower reading is a stale session's
            # value racing a fresher one: keep the higher and log it for diagnosis.
            activity.capped_append(
                os.path.join(DIR, "regress.log"),
                "%d %s sid=%s sent=%s kept=%s\n"
                % (now, key, str(raw.get("session_id", ""))[:8], win["p"], old["p"]))
            win = old
        wins[out] = win

    ms = num(dig(raw, "cost", "total_duration_ms"))
    model = dig(raw, "model", "display_name")
    return {
        "ts": now,
        "s": wins["s"],
        "w": wins["w"],
        "m": model if isinstance(model, str) else "",
        "c": num(dig(raw, "context_window", "used_percentage")),
        "cost": num(dig(raw, "cost", "total_cost_usd")),
        "dur": int(ms / 1000) if ms is not None and ms == ms and abs(ms) != float("inf") else 0,
        "la": num(dig(raw, "cost", "total_lines_added")),
        "lr": num(dig(raw, "cost", "total_lines_removed")),
    }


def summary_line(state):
    parts = [state["m"] or "claude"]
    if state["s"]:
        parts.append("5h %d%%" % round(state["s"]["p"]))
    if state["w"]:
        parts.append("7d %d%%" % round(state["w"]["p"]))
    return " | ".join(parts)


def main():
    text = sys.stdin.read()
    try:
        raw = json.loads(text)
    except ValueError:
        raw = None
    if not isinstance(raw, dict):  # not our payload: say something harmless and leave state alone
        print("claude")
        return
    os.makedirs(DIR, mode=0o700, exist_ok=True)
    atomic_write(os.path.join(DIR, "raw.json"), text)

    now = int(time.time())
    try:
        with open(os.path.join(DIR, "state.json")) as f:
            prev = json.load(f)
    except (OSError, ValueError):
        prev = {}

    state = normalise(raw, prev, now)
    atomic_write(os.path.join(DIR, "state.json"), json.dumps(state))
    append_history(now, str(raw.get("session_id", "")), state)
    print(summary_line(state))


if __name__ == "__main__":
    main()
