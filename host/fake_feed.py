#!/usr/bin/env python3
"""Send canned frames to the CYD to check each screen state.

usage: fake_feed.py SCENARIO [--port /dev/cu.usbserial-xxxx]
scenarios: low  mid  high  limit  expired  nodata  timeonly
"""
import argparse
import glob
import json
import time

import serial

H = 3600


def frame(scn, base):
    now = int(time.time())
    f = {"t": now, "tz": time.localtime(now).tm_gmtoff}
    if scn == "timeonly":
        return f
    f.update({"age": 5, "m": "Sonnet 5.5", "c": 31, "cost": 1.84, "dur": 5400, "la": 212, "lr": 37})
    # (session %, hours into session, weekly %, days into week)
    table = {
        "low": (12, 1.0, 8, 1.5),
        "mid": (58, 2.0, 40, 3.0),
        "high": (91, 3.0, 82, 5.5),
        "limit": (100, 4.5, 97, 6.5),
    }
    if scn == "nodata":
        return f
    if scn == "expired":
        f["s"] = {"p": 77, "r": base - 600}
        f["w"] = {"p": 30, "r": base + 3 * 86400}
        return f
    sp, sh, wp, wd = table[scn]
    f["s"] = {"p": sp, "r": int(base + (5 - sh) * H)}
    f["w"] = {"p": wp, "r": int(base + (7 - wd) * 86400)}
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario")
    ap.add_argument("--port")
    a = ap.parse_args()
    port = a.port or sorted(glob.glob("/dev/cu.usbserial-*"))[0]
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0
    ser.dtr = ser.rts = False
    ser.open()
    base = int(time.time())  # fixed reset times, like the real feed
    print("sending", a.scenario, "to", port, "- Ctrl-C to stop")
    try:
        while True:
            ser.write((json.dumps(frame(a.scenario, base), separators=(",", ":")) + "\n").encode())
            time.sleep(3)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
