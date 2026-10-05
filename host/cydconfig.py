"""User-editable settings for the CYD bridge: ~/.claude-cyd/config.json."""
import json
import os

DIR = os.path.expanduser("~/.claude-cyd")
PATH = os.path.join(DIR, "config.json")

DEFAULTS = {
    "idle_min": 10,                    # minutes without Claude activity before auto-rotating
    "night": ["23:00", "07:00"],       # dim between these local times; null to disable
    "services": ["com.user.claude-cyd"],  # launchd labels to watch ("system/<label>" for daemons)
    "alerts": {"sess": 90, "week": 95, "disk_free_pct": 10},
}


def load(path=PATH):
    cfg = json.loads(json.dumps(DEFAULTS))
    try:
        with open(path) as f:
            user = json.load(f)
    except (OSError, ValueError):
        return cfg
    for k, v in user.items():
        if k == "alerts" and isinstance(v, dict):
            cfg["alerts"].update(v)
        else:
            cfg[k] = v
    return cfg


def ensure(path=PATH):
    """Create the config file with defaults if it doesn't exist."""
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(DEFAULTS, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    ensure()
