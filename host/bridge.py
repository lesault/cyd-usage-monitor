#!/usr/bin/env python3
"""Forward Claude usage state to the CYD over USB serial.

Reads ~/.claude-cyd/state.json (written by statusline.py) and sends one JSON
line every few seconds. Always sends the wall-clock time so the CYD clock and
countdowns work even when no Claude Code session is running. Reconnects when
the cable is unplugged.
"""
import argparse
import glob
import json
import os
import sys
import time

import serial

STATE = os.path.expanduser("~/.claude-cyd/state.json")


def find_port(explicit):
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*"))
    return ports[0] if ports else None


def build_frame():
    now = int(time.time())
    frame = {"t": now, "tz": time.localtime(now).tm_gmtoff}
    try:
        with open(STATE) as f:
            st = json.load(f)
        frame["age"] = max(0, now - int(st.get("ts", now)))
        for k in ("s", "w", "m", "c", "cost", "dur", "la", "lr"):
            if st.get(k) is not None:
                frame[k] = st[k]
    except (OSError, ValueError):
        pass  # no data yet: send time only
    return frame


def open_port(path):
    ser = serial.Serial()
    ser.port = path
    ser.baudrate = 115200
    ser.timeout = 0
    ser.dtr = False  # avoid resetting the ESP32 on connect
    ser.rts = False
    ser.open()
    return ser


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--interval", type=float, default=5.0)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ser = None
    while True:
        try:
            if ser is None:
                path = find_port(args.port)
                if not path:
                    time.sleep(2)
                    continue
                ser = open_port(path)
                print("connected:", path, flush=True)
            line = json.dumps(build_frame(),separators=(",", ":")) + "\n"
            ser.write(line.encode())
            if args.verbose:
                print(">", line.strip(), flush=True)
                echo = ser.read(512)
                if echo:
                    sys.stdout.write(echo.decode(errors="replace"))
            else:
                ser.read(512)  # drain firmware logs
            time.sleep(args.interval)
        except (serial.SerialException, OSError) as e:
            print("serial error:", e, "- retrying", flush=True)
            try:
                if ser:
                    ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)
        except KeyboardInterrupt:
            break


if __name__ == "__main__":
    main()
