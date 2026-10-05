#!/bin/zsh
# Install the host side of the CYD monitor under ~/.claude-cyd so it keeps
# working when this (external) volume is unmounted, and so launchd can read it.
set -e
SRC="${0:A:h}"
DEST="$HOME/.claude-cyd"
LABEL=com.user.claude-cyd
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

mkdir -p "$DEST"
[[ -x "$DEST/venv/bin/python" ]] || python3 -m venv "$DEST/venv"
"$DEST/venv/bin/pip" install -q pyserial psutil
for f in bridge statusline cydconfig rules activity sysstats; do cp "$SRC/$f.py" "$DEST/"; done
chmod +x "$DEST/statusline.py"
"$DEST/venv/bin/python" "$DEST/cydconfig.py"  # writes default config.json if missing

sed -e "s#/Volumes/Warm/CYD/.venv/bin/python#$DEST/venv/bin/python#" \
    -e "s#/Volumes/Warm/CYD/host/bridge.py#$DEST/bridge.py#" \
    -e "s#/Users/lesault/.claude-cyd#$DEST#g" \
    "$SRC/com.user.claude-cyd.plist" > "$PLIST"

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "installed. bridge log: $DEST/bridge.log"
echo "statusLine command: python3 $DEST/statusline.py"
