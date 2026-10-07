#!/bin/zsh
# Install the host side of the CYD monitor under ~/.claude-cyd and start the bridge.
#
# Everything the background job needs is copied there because macOS can stop launchd
# jobs reading some locations (external volumes, Documents, Desktop). Re-run after
# editing anything in host/.
set -e
SRC="${0:A:h}"
DEST="$HOME/.claude-cyd"
LABEL=com.user.claude-cyd
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }

# Private by default: usage and cost data live here.
mkdir -p "$DEST" "$HOME/Library/LaunchAgents"
chmod 700 "$DEST"
for f in bridge.log missing.log regress.log history.jsonl history.jsonl.lock; do
  [[ -e "$DEST/$f" ]] || : > "$DEST/$f"   # launchd creates its log world-readable if it's missing
  chmod 600 "$DEST/$f"
done

[[ -x "$DEST/venv/bin/python" ]] || python3 -m venv "$DEST/venv"
"$DEST/venv/bin/pip" install -q --require-hashes -r "$SRC/requirements.txt"
for f in bridge statusline cydconfig rules activity sysstats; do cp "$SRC/$f.py" "$DEST/"; done
chmod +x "$DEST/statusline.py"
"$DEST/venv/bin/python" "$DEST/cydconfig.py"  # writes a default config.json if missing

python3 "$SRC/install_helpers.py" plist "$SRC/com.user.claude-cyd.plist.in" \
  "$DEST/venv/bin/python" "$DEST/bridge.py" "$DEST/bridge.log" > "$PLIST"
plutil -lint "$PLIST" >/dev/null

# bootout returns before the job is fully gone; bootstrapping too early fails with an I/O error.
launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
for _ in {1..20}; do
  launchctl print "$DOMAIN/$LABEL" >/dev/null 2>&1 || break
  sleep 0.5
done
for attempt in {1..5}; do
  launchctl bootstrap "$DOMAIN" "$PLIST" && break
  [[ $attempt -eq 5 ]] && { echo "launchctl bootstrap failed" >&2; exit 1; }
  sleep 1
done

cat <<EOT
Installed. Bridge log: $DEST/bridge.log   Config: $DEST/config.json

Last step (once): merge this into ~/.claude/settings.json so Claude Code feeds the display:

EOT
python3 "$SRC/install_helpers.py" snippet "$DEST/statusline.py"
