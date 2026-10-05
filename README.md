# CYD Claude usage monitor

A 320x240 landscape dashboard on an ESP32 "Cheap Yellow Device" showing Claude
session and weekly usage, fed from Claude Code's statusLine over USB serial.

```
Claude Code -> ~/.claude-cyd/statusline.py -> state.json -> bridge.py (launchd) -> USB -> CYD
```

## Touch controls
| Gesture | Action |
|---|---|
| Tap right / left half | Next / previous page (Home, Forecast, Info) |
| Tap clock (bottom-left) | Toggle "resets in" vs "resets at" |
| Long-press (~1 s) | Settings: brightness, auto-dim, 12/24 h, recalibrate |
| Tap while dimmed | Wake only |

Pages return to Home after 30 s. The bar's pale tick marks how much of the
window has elapsed; fill ahead of the tick means you are burning faster than pace.

## Install / update
- Firmware: `.venv/bin/pio run -t upload --upload-port /dev/cu.usbserial-21320`
  (stop the bridge first: `launchctl bootout gui/$(id -u)/com.user.claude-cyd`)
- Host: `host/install.sh` (copies scripts to `~/.claude-cyd`, installs the launchd agent).
  Re-run after editing anything in `host/`.
- `~/.claude/settings.json` needs `statusLine.command = python3 ~/.claude-cyd/statusline.py`.

## Notes
- Data refreshes only while Claude Code is active; the bottom-right shows LIVE / STALE age.
  Countdowns keep running locally.
- The ESP32 reboots when the bridge connects (macOS toggles reset); it recovers within 5 s.
- Test screens without Claude: `.venv/bin/python host/fake_feed.py low|mid|high|limit|expired|nodata`
  (stop the bridge first; only one process can hold the port).
- Restore Marauder: `.venv/bin/esptool --port /dev/cu.usbserial-21320 --baud 115200 write-flash 0x0 backup/marauder-backup.bin`
- Logs: `~/.claude-cyd/bridge.log`, `~/.claude-cyd/missing.log` (windows Claude Code omitted).
