#!/usr/bin/env python3
"""Forward Claude usage and Mac health to the CYD over USB serial.

Reads ~/.claude-cyd/state.json (written by statusline.py), samples the Mac, and
sends one JSON line every few seconds. The bridge makes the decisions (idle,
night, alerts); the firmware just renders. Reconnects when the cable is
unplugged. See README for the frame format.

Before sending anything the bridge checks that the port really is a CYD: it sends
{"probe":1} and waits for {"cyd":1}. Frames (usage, cost, uptime...) are never written
to a device that doesn't answer. `--no-handshake` skips this for firmware that predates it.
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

HANDSHAKE_S = 8.0   # the boot splash can keep a freshly reset board busy for a few seconds
PROBE_EVERY_S = 0.5
REJECT_S = 15.0     # how long to leave a port that didn't answer alone
STATE = os.path.expanduser("~/.claude-cyd/state.json")
LOG = os.path.expanduser("~/.claude-cyd/bridge.log")
CONFIG_RELOAD_S = 30


def find_ports(explicit):
    if explicit:
        return [explicit]
    return sorted(glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*"))


def is_hello(line):
    """True for the firmware's {"cyd":1}, even with boot noise stuck to the front of the line."""
    try:
        return json.loads(line[max(line.find("{"), 0):]).get("cyd") == 1
    except (ValueError, AttributeError):
        return False


def handshake(ser, timeout=HANDSHAKE_S, clock=time.monotonic, sleep=time.sleep):
    """True once the device on ser answers {"probe":1} with {"cyd":1}.

    Only the probe is ever written. Anything else the device sends (boot noise, an echo
    of our own probe from a loopback adapter) is ignored.
    """
    deadline = clock() + timeout
    next_probe, buf = 0.0, b""
    while clock() < deadline:
        if clock() >= next_probe:
            ser.write(b'{"probe":1}\n')
            next_probe = clock() + PROBE_EVERY_S
        buf += ser.read(512) or b""
        *lines, buf = buf.split(b"\n")
        buf = buf[-512:]  # a device spewing newline-free noise can't grow this forever
        for line in lines:
            if is_hello(line.decode(errors="replace")):
                return True
        sleep(0.05)
    return False


def connect(explicit, rejected, check=True, clock=time.time):
    """Open the first port that is (or, with check=False, might be) a CYD.

    rejected maps port -> time before which it is left alone; entries for ports that have
    gone away are dropped so a re-plug is tried at once.
    """
    ports = find_ports(explicit)
    for gone in set(rejected) - set(ports):
        del rejected[gone]
    for path in ports:
        if rejected.get(path, 0) > clock():
            continue
        first = path not in rejected
        ser = None
        try:
            ser = open_port(path)
            if not check or handshake(ser):
                return ser, path
            why = "did not answer the handshake"
        except (serial.SerialException, OSError) as e:
            why = "cannot talk to it (%s)" % e
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
        rejected[path] = clock() + REJECT_S
        if first:  # say it once, not every retry
            print("ignoring %s: %s" % (path, why), flush=True)
    return None, None


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
    ap.add_argument("--no-handshake", action="store_true",
                    help="send to the port without checking it is a CYD (old firmware)")
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
    ser, rejected = None, {}
    while True:
        try:
            if ser is None:
                ser, path = connect(args.port, rejected, check=not args.no_handshake)
                if ser is None:
                    time.sleep(2)
                    continue
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
