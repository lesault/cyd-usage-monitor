"""User-editable settings for the CYD bridge: ~/.claude-cyd/config.json.

The file is hand-edited, so load() never raises: anything malformed falls back to the
default for that setting.
"""
import json
import os
import re

DIR = os.path.expanduser("~/.claude-cyd")
PATH = os.path.join(DIR, "config.json")

MAX_SERVICES = 4  # the firmware shows at most this many

DEFAULTS = {
    "idle_min": 10,                    # minutes without Claude activity before auto-rotating
    "night": ["23:00", "07:00"],       # dim between these local times; null to disable
    "services": ["com.user.claude-cyd"],  # launchd labels to watch ("system/<label>" for daemons)
    "alerts": {"sess": 90, "week": 95, "disk_free_pct": 10},
}

_HHMM = re.compile(r"^([01]?\d|2[0-3]):[0-5]\d$")


def _number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and v >= 0


def _clean(user, cfg):
    """Copy the valid settings from user (any JSON value) over cfg."""
    if not isinstance(user, dict):
        return
    if _number(user.get("idle_min")) and user["idle_min"] > 0:
        cfg["idle_min"] = user["idle_min"]
    if "night" in user:
        n = user["night"]
        if n is None:
            cfg["night"] = None
        elif (isinstance(n, (list, tuple)) and len(n) == 2
              and all(isinstance(t, str) and _HHMM.match(t) for t in n)):
            cfg["night"] = list(n)
    if isinstance(user.get("services"), list):
        cfg["services"] = [s for s in user["services"] if isinstance(s, str) and s][:MAX_SERVICES]
    if isinstance(user.get("alerts"), dict):
        for k in cfg["alerts"]:
            if _number(user["alerts"].get(k)):
                cfg["alerts"][k] = user["alerts"][k]


def load(path=PATH):
    cfg = json.loads(json.dumps(DEFAULTS))
    try:
        with open(path) as f:
            user = json.load(f)
    except (OSError, ValueError):
        return cfg
    _clean(user, cfg)
    return cfg


def ensure(path=PATH):
    """Create the config file with defaults if it doesn't exist."""
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    with open(path, "w") as f:
        json.dump(DEFAULTS, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    ensure()
