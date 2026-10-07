#!/usr/bin/env python3
"""Install-time helpers for install.sh (not copied to ~/.claude-cyd).

Done in Python rather than sed/echo so paths containing spaces, '&', '#' or '<' come out
correct: XML-escaped in the launchd plist, shell-quoted and JSON-escaped in the settings
snippet.

usage: install_helpers.py plist TEMPLATE PYTHON BRIDGE LOG
       install_helpers.py snippet STATUSLINE_PATH
"""
import json
import shlex
import sys
from xml.sax.saxutils import escape


def render_plist(template, python, bridge, log):
    out = template
    for key, value in (("__PYTHON__", python), ("__BRIDGE__", bridge), ("__LOG__", log)):
        out = out.replace(key, escape(value))
    return out


def statusline_snippet(statusline_path):
    """The settings.json fragment that points Claude Code's statusLine at the script."""
    cmd = "python3 " + shlex.quote(statusline_path)
    return json.dumps({"statusLine": {"type": "command", "command": cmd, "refreshInterval": 30}},
                      indent=2)


def main(argv):
    if len(argv) == 6 and argv[1] == "plist":
        with open(argv[2]) as f:
            sys.stdout.write(render_plist(f.read(), *argv[3:6]))
    elif len(argv) == 3 and argv[1] == "snippet":
        print(statusline_snippet(argv[2]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
