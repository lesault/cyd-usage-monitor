"""Pure decision logic: night window and alert codes."""


def _minutes(hhmm):
    h, m = hhmm.split(":")
    return int(h) * 60 + int(m)


def in_night(local_minutes, window):
    """True if local_minutes (minutes since midnight) falls in window = [start, end]."""
    if not window:
        return False
    start, end = _minutes(window[0]), _minutes(window[1])
    if start == end:
        return False
    if start < end:
        return start <= local_minutes < end
    return local_minutes >= start or local_minutes < end  # spans midnight


def _raised(win, now, threshold):
    """True for a live window ({"p", "r"}) at or over the threshold; junk counts as not raised."""
    try:
        return win["r"] > now and win["p"] >= threshold
    except (TypeError, KeyError):
        return False


def compute_alerts(state, sysinfo, cfg, now):
    """Return the list of active alert codes.

    state: the statusline snapshot ({"s": {"p","r"}, "w": {...}}) or None.
    sysinfo: the "sys" dict (may be empty). cfg: loaded config.
    """
    al = []
    th = cfg["alerts"]
    state = state if isinstance(state, dict) else {}
    if _raised(state.get("s"), now, th["sess"]):
        al.append("sess")
    if _raised(state.get("w"), now, th["week"]):
        al.append("week")
    if sysinfo.get("total") and 100.0 * sysinfo["free_b"] / sysinfo["total"] < th["disk_free_pct"]:
        al.append("disk")
    if any(v == 0 for v in sysinfo.get("svc", [])):
        al.append("svc")
    return al
