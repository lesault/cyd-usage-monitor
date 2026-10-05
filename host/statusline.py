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

DIR = os.path.expanduser("~/.claude-cyd")


def dig(d, *path):
    for k in path:
        if not isinstance(d, dict):
            return None
        d = d.get(k)
    return d


def window(raw, key):
    w = dig(raw, "rate_limits", key)
    if not isinstance(w, dict):
        return None
    p, r = w.get("used_percentage"), w.get("resets_at")
    if p is None or r is None:
        return None
    return {"p": float(p), "r": int(r)}


def atomic_write(path, text):
    fd, tmp = tempfile.mkstemp(dir=DIR)
    with os.fdopen(fd, "w") as f:
        f.write(text)
    os.replace(tmp, path)


def main():
    text = sys.stdin.read()
    try:
        raw = json.loads(text)
    except ValueError:
        print("claude")
        return
    os.makedirs(DIR, exist_ok=True)
    atomic_write(os.path.join(DIR, "raw.json"), text)

    now = int(time.time())
    try:
        with open(os.path.join(DIR, "state.json")) as f:
            prev = json.load(f)
    except (OSError, ValueError):
        prev = {}

    wins = {}
    for out, key in (("s", "five_hour"), ("w", "seven_day")):
        win = window(raw, key)
        if win is None:
            # Claude Code sometimes omits a window; keep the last one until it resets.
            with open(os.path.join(DIR, "missing.log"), "a") as f:
                f.write("%d missing %s\n" % (now, key))
            old = prev.get(out)
            if old and old["r"] > now:
                win = old
        wins[out] = win

    state = {
        "ts": now,
        "s": wins["s"],
        "w": wins["w"],
        "m": dig(raw, "model", "display_name") or "",
        "c": dig(raw, "context_window", "used_percentage"),
        "cost": dig(raw, "cost", "total_cost_usd"),
        "dur": int((dig(raw, "cost", "total_duration_ms") or 0) / 1000),
        "la": dig(raw, "cost", "total_lines_added"),
        "lr": dig(raw, "cost", "total_lines_removed"),
    }
    atomic_write(os.path.join(DIR, "state.json"), json.dumps(state))

    s, w = state["s"], state["w"]
    parts = [state["m"] or "claude"]
    if s:
        parts.append("5h %d%%" % round(s["p"]))
    if w:
        parts.append("7d %d%%" % round(w["p"]))
    print(" | ".join(parts))


if __name__ == "__main__":
    main()
