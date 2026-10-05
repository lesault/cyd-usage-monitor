# CYD Claude usage monitor

A 320x240 landscape dashboard on an ESP32 "Cheap Yellow Device" (ESP32-2432S028).
It shows Claude session and weekly usage, and becomes an ambient Mac display
(clock, health, activity) when Claude is quiet. Data reaches it over USB serial.

```
Claude Code -> statusline.py -> state.json + history.jsonl ->
   bridge.py (launchd; + Mac stats, idle/night/alert rules) -> USB -> CYD
```

## Pages (tap right / left half to cycle)
Home (session + weekly) - Forecast - Info - Activity - Health - Clock

| Gesture | Action |
|---|---|
| Tap right / left half | Next / previous page |
| Tap clock (bottom-left) | Toggle "resets in" vs "resets at" |
| Long-press (~1 s) | Settings: brightness, auto-dim, 12/24 h, auto-switch, recalibrate |
| Tap while dimmed | Wake only |
| Tap an alert | Dismiss it (stays quiet for 30 min) |

On Home, the pale tick on each bar marks how much of the window has elapsed; fill
ahead of the tick means you are burning faster than pace.

**Auto-switch:** when Claude has been quiet for `idle_min` minutes (and the screen
hasn't been touched for 30 s) it rotates Clock -> Health -> Activity, and snaps back to
Home as soon as Claude usage changes. A touch takes over. Toggle in Settings.

**Night:** between the `night` times the screen dims to minimum; a touch or a new alert
lights it for a minute.

**Alerts** take over the whole screen: session or week near the limit, low disk space,
or a monitored service down.

## Configuration
`~/.claude-cyd/config.json` (created by `install.sh`, reloaded every 30 s, no reflash):
```json
{ "idle_min": 10,
  "night": ["23:00", "07:00"],
  "services": ["com.user.claude-cyd"],
  "alerts": { "sess": 90, "week": 95, "disk_free_pct": 10 } }
```
`night: null` disables night mode. `services` are launchd labels (`system/<label>` for daemons).

## Install / update
- Firmware: `.venv/bin/pio run -t upload --upload-port /dev/cu.usbserial-*`
  (stop the bridge first: `launchctl bootout gui/$(id -u)/com.user.claude-cyd`).
  The port number changes occasionally; check `ls /dev/cu.usbserial*`.
- Host: `host/install.sh` (copies scripts to `~/.claude-cyd`, installs the launchd agent).
  Re-run after editing anything in `host/`.
- `~/.claude/settings.json` needs:
  `"statusLine": {"type": "command", "command": "python3 ~/.claude-cyd/statusline.py", "refreshInterval": 30}`
- Host tests: `.venv/bin/python -m unittest discover -s host/tests`

## Frame format (bridge -> CYD, one JSON line every ~5 s)
`t, tz` (clock) - `age, s, w, m, c, cost, dur, la, lr` (Claude state; s/w = `{p, r}` percent and
reset epoch) - `auto, night` (flags) - `al` (alert codes `sess|week|disk|svc`) -
`sys` (`cpu, mem, disk, free, up, rx, tx, svc[], svn[]`) - `act` (`today, n, last, days[7]`).

## Notes
- Claude data only changes on API responses; "Live" means the feed is alive, not that the
  figures are up to the second. A lower weekly/session reading within the same window is a
  stale session racing a fresh one and is ignored (logged in `~/.claude-cyd/regress.log`).
- The 7-day Activity chart (weekly allowance used per day) fills in over a week; history
  starts when `statusline.py` first runs and is kept 14 days in `history.jsonl`.
- The ESP32 reboots when the bridge connects (macOS toggles reset); it recovers within 5 s.
- The right edge of the panel is not fully visible, so everything keeps a 4 px margin.
- Test screens without Claude: `.venv/bin/python host/fake_feed.py SCENARIO`
  (low, mid, high, limit, expired, nodata, idle, night, alert-sess, alert-disk, alert-svc).
  Stop the bridge first: only one process can hold the port.
- Restore Marauder: `.venv/bin/esptool --port /dev/cu.usbserial-* --baud 115200 write-flash 0x0 backup/marauder-backup.bin`
  (the backup is local only, deliberately not in git).
- Logs in `~/.claude-cyd/`: `bridge.log`, `missing.log` (windows Claude Code omitted), `regress.log`.
