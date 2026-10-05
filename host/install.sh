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

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }

mkdir -p "$DEST" "$HOME/Library/LaunchAgents"
[[ -x "$DEST/venv/bin/python" ]] || python3 -m venv "$DEST/venv"
"$DEST/venv/bin/pip" install -q pyserial psutil
for f in bridge statusline cydconfig rules activity sysstats; do cp "$SRC/$f.py" "$DEST/"; done
chmod +x "$DEST/statusline.py"
"$DEST/venv/bin/python" "$DEST/cydconfig.py"  # writes a default config.json if missing

sed -e "s#__PYTHON__#$DEST/venv/bin/python#" \
    -e "s#__BRIDGE__#$DEST/bridge.py#" \
    -e "s#__LOG__#$DEST/bridge.log#g" \
    "$SRC/com.user.claude-cyd.plist.in" > "$PLIST"

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

cat <<EOF
Installed. Bridge log: $DEST/bridge.log   Config: $DEST/config.json

Last step (once): add this to ~/.claude/settings.json so Claude Code feeds the display:

  "statusLine": {
    "type": "command",
    "command": "python3 $DEST/statusline.py",
    "refreshInterval": 30
  }
EOF
