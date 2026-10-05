"""Mac health sampler for the CYD: CPU, memory, disk, network, uptime, services."""
import os
import subprocess
import time

import psutil

CACHE_S = 4.5      # reuse a snapshot for this long (frames go out every ~5 s)
SERVICE_S = 15.0   # how often to re-check launchd services


def service_up(label):
    """1 if the launchd job is loaded and running, else 0."""
    target = label if label.startswith("system/") else "gui/%d/%s" % (os.getuid(), label)
    try:
        r = subprocess.run(["launchctl", "print", target], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return 0
    if r.returncode != 0:
        return 0
    return 1 if ("state = running" in r.stdout or "pid = " in r.stdout) else 0


def short_name(label):
    return label.split("/")[-1].split(".")[-1][:12]


def _net_bytes():
    rx = tx = 0
    for name, c in psutil.net_io_counters(pernic=True).items():
        if name.startswith("en"):
            rx += c.bytes_recv
            tx += c.bytes_sent
    return rx, tx


class Sampler:
    def __init__(self):
        psutil.cpu_percent(None)  # prime: the first call has no interval to measure
        self._net, self._net_t = _net_bytes(), time.time()
        self._svc, self._svc_t, self._svc_labels = [], 0.0, None
        self._cache, self._cache_t = None, 0.0

    def snapshot(self, services):
        """Return the sys dict. Keys "total" and "free_b" are for local rules only."""
        now = time.time()
        if self._cache and now - self._cache_t < CACHE_S:
            return self._cache

        vm = psutil.virtual_memory()
        du = psutil.disk_usage("/")
        rx, tx = _net_bytes()
        dt = max(now - self._net_t, 0.001)
        rx_kbs = max(0, (rx - self._net[0]) / dt / 1024)
        tx_kbs = max(0, (tx - self._net[1]) / dt / 1024)
        self._net, self._net_t = (rx, tx), now

        if self._svc_labels != services or now - self._svc_t >= SERVICE_S:
            self._svc = [service_up(s) for s in services]
            self._svc_t, self._svc_labels = now, list(services)

        self._cache = {
            "cpu": round(psutil.cpu_percent(None)),
            "mem": round((vm.total - vm.available) / vm.total * 100),
            "disk": round(du.percent),
            "free": round(du.free / 1e9),
            "up": int(now - psutil.boot_time()),
            "rx": round(rx_kbs),
            "tx": round(tx_kbs),
            "svc": list(self._svc),
            "svn": [short_name(s) for s in services],
            "total": du.total,
            "free_b": du.free,
        }
        self._cache_t = now
        return self._cache


PUBLIC_KEYS = ("cpu", "mem", "disk", "free", "up", "rx", "tx", "svc", "svn")


def public(sysinfo):
    return {k: sysinfo[k] for k in PUBLIC_KEYS if k in sysinfo}
