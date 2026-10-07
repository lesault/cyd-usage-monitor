#!/usr/bin/env python3
"""Forward Claude usage and Mac health to the CYD over USB serial.

Reads ~/.claude-cyd/state.json (written by statusline.py), samples the Mac, and
sends one JSON line every few seconds. The bridge makes the decisions (idle,
night, alerts); the firmware just renders. Reconnects when the cable is
unplugged. See README for the frame format.
"""
import argparse
import glob
import json
import os
import sys
import time

import serial

import activity
import cydconfig
import rules
import sysstats

STATE = os.path.expanduser("~/.claude-cyd/state.json")
LOG = os.path.expanduser("~/.claude-cyd/bridge.log")
CONFIG_RELOAD_S = 30


def find_port(explicit):
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*"))
    return ports[0] if ports else None


def load_state():
    try:
        with open(STATE) as f:
            state = json.load(f)
    except (OSError, ValueError):
        return None
    return state if isinstance(state, dict) else None


def build_frame(cfg, sampler, act, now=None):
    now = int(now if now is not None else time.time())
    lt = time.localtime(now)
    frame = {"t": now, "tz": lt.tm_gmtoff}

    state = load_state()
    if state:
        frame["age"] = max(0, now - int(state.get("ts", now)))
        for k in ("s", "w", "m", "c", "cost", "dur", "la", "lr"):
            if state.get(k) is not None:
                frame[k] = state[k]

    sysinfo = sampler.snapshot(cfg["services"])
    summary = act.summary(now, lt.tm_gmtoff)
    idle = summary["idle"]
    frame["auto"] = int(idle is None or idle >= cfg["idle_min"] * 60)
    frame["night"] = int(rules.in_night(lt.tm_hour * 60 + lt.tm_min, cfg["night"]))
    frame["al"] = rules.compute_alerts(state, sysinfo, cfg, now)
    frame["sys"] = sysstats.public(sysinfo)
    frame["act"] = {
        "today": summary["today"],
        "n": summary["n"],
        "last": -1 if idle is None else idle,
        "days": summary["days"],
    }
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

    cydconfig.ensure()
    try:
        os.chmod(LOG, 0o600)  # launchd creates it world-readable if install.sh didn't
    except OSError:
        pass
    cfg, cfg_t = cydconfig.load(), time.time()
    sampler, act = sysstats.Sampler(), activity.Activity()
    last_prune_day = None
    last_err = last_serial_err = None
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
                last_serial_err = None

            if time.time() - cfg_t > CONFIG_RELOAD_S:
                cfg, cfg_t = cydconfig.load(), time.time()
            tz = time.localtime().tm_gmtoff
            today = activity.day_index(time.time(), tz)
            if today != last_prune_day:
                activity.prune(tz=tz)
                last_prune_day = today
                activity.truncate_if_large(LOG)

            line = json.dumps(build_frame(cfg, sampler, act), separators=(",", ":")) + "\n"
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
            msg = "serial error: %s - retrying" % (e,)
            if msg != last_serial_err:  # a busy or flaky port would otherwise log every 2 s
                print(msg, flush=True)
                last_serial_err = msg
            try:
                if ser:
                    ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)
        except KeyboardInterrupt:
            break
        except Exception as e:  # a bad frame must not take the bridge down (launchd would loop it)
            msg = "frame error: %r" % (e,)
            if msg != last_err:
                print(msg, flush=True)
                last_err = msg
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
