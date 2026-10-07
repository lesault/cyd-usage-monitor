"""Run: python3 -m unittest discover -s host/tests -v"""
import json
import os
import sys
import tempfile
import threading
import time
import types
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

# bridge/sysstats import hardware-facing packages that the tests never touch.
for _name in ("serial", "psutil"):
    try:
        __import__(_name)
    except ImportError:
        sys.modules[_name] = types.ModuleType(_name)

import activity  # noqa: E402
import bridge  # noqa: E402
import cydconfig  # noqa: E402
import install_helpers  # noqa: E402
import rules  # noqa: E402
import statusline  # noqa: E402

DAY = 86400
D0 = 20000 * DAY  # midnight UTC of some day; tz=0 in these tests
NOW = D0 + 12 * 3600


def row(ts, sid="a", cost=0.0, s=None, w=None, la=0, lr=0):
    return {"ts": ts, "sid": sid, "cost": cost, "s": s, "w": w, "la": la, "lr": lr}


class ActivityTests(unittest.TestCase):
    def test_empty(self):
        r = activity.summarize([], NOW, 0)
        self.assertIsNone(r["idle"])
        self.assertEqual(r["today"], 0)
        self.assertEqual(r["n"], 0)
        self.assertEqual(r["days"], [0.0] * 7)

    def test_idle_ignores_heartbeats(self):
        rows = [row(NOW - 3000, cost=1.0), row(NOW - 2000, cost=1.5)]
        rows += [row(NOW - 1000 + i * 120, cost=1.5) for i in range(5)]  # unchanged heartbeats
        self.assertEqual(activity.summarize(rows, NOW, 0)["idle"], 2000)

    def test_two_concurrent_sessions_cost(self):
        rows = [
            row(D0 + 100, "a", 0.0), row(D0 + 200, "b", 0.0),
            row(D0 + 300, "a", 1.0), row(D0 + 400, "b", 0.5), row(D0 + 500, "a", 2.0),
        ]
        r = activity.summarize(rows, NOW, 0)
        self.assertEqual(r["n"], 2)
        self.assertAlmostEqual(r["today"], 2.5)

    def test_session_spanning_midnight_counts_only_today(self):
        rows = [row(D0 - 3600, "a", 3.0), row(D0 + 3600, "a", 4.2)]
        self.assertAlmostEqual(activity.summarize(rows, NOW, 0)["today"], 1.2)

    def test_history_starting_mid_session_does_not_overcount(self):
        rows = [row(D0 + 100, "a", 5.0), row(D0 + 200, "a", 5.4)]  # first sighting at $5.00
        self.assertAlmostEqual(activity.summarize(rows, NOW, 0)["today"], 0.4)

    def test_daily_bars_and_window_reset(self):
        rows = [
            row(D0 - 2 * DAY + 10, w=10), row(D0 - 2 * DAY + 20, w=15),   # +5 two days ago
            row(D0 - DAY + 10, w=18),                                      # +3 yesterday
            row(D0 + 10, w=2),                                             # reset: counts from 0
            row(D0 + 20, w=4),                                             # +2
        ]
        days = activity.summarize(rows, NOW, 0)["days"]
        self.assertEqual(days[4], 5.0)   # two days ago
        self.assertEqual(days[5], 3.0)   # yesterday
        self.assertEqual(days[6], 4.0)   # today: 2 after reset + 2
        self.assertEqual(days[0:4], [0.0] * 4)

    def test_timezone_shifts_day_boundary(self):
        ts = D0 + 23 * 3600  # 23:00 UTC = 00:00 next day at UTC+1
        self.assertEqual(activity.day_index(ts, 0), 20000)
        self.assertEqual(activity.day_index(ts, 3600), 20001)

    def test_prune_and_corrupt_lines(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "h.jsonl")
            with open(p, "w") as f:
                f.write(json.dumps(row(NOW - 20 * DAY)) + "\n")
                f.write("not json\n")
                f.write(json.dumps(row(NOW - 3600)) + "\n")
            self.assertEqual(len(activity.load(p)), 2)  # corrupt line skipped
            activity.prune(p, now=NOW, tz=0)
            self.assertEqual([r["ts"] for r in activity.load(p)], [NOW - 3600])


class RulesTests(unittest.TestCase):
    def test_night_spanning_midnight(self):
        w = ["23:00", "07:00"]
        self.assertTrue(rules.in_night(23 * 60, w))
        self.assertTrue(rules.in_night(3 * 60, w))
        self.assertFalse(rules.in_night(7 * 60, w))
        self.assertFalse(rules.in_night(12 * 60, w))

    def test_night_same_day_and_disabled(self):
        self.assertTrue(rules.in_night(13 * 60, ["12:00", "14:00"]))
        self.assertFalse(rules.in_night(15 * 60, ["12:00", "14:00"]))
        self.assertFalse(rules.in_night(3 * 60, None))

    def test_alerts(self):
        cfg = cydconfig.load("/nonexistent")
        now = 1000
        state = {"s": {"p": 92, "r": now + 100}, "w": {"p": 50, "r": now + 100}}
        sysinfo = {"total": 1000, "free_b": 50, "svc": [1, 0]}
        self.assertEqual(rules.compute_alerts(state, sysinfo, cfg, now), ["sess", "disk", "svc"])

    def test_expired_window_does_not_alert(self):
        cfg = cydconfig.load("/nonexistent")
        state = {"s": {"p": 99, "r": 500}, "w": {"p": 99, "r": 500}}
        self.assertEqual(rules.compute_alerts(state, {}, cfg, 1000), [])

    def test_no_state_no_alerts(self):
        cfg = cydconfig.load("/nonexistent")
        self.assertEqual(rules.compute_alerts(None, {"total": 1000, "free_b": 900, "svc": [1]}, cfg, 1), [])


class ConfigTests(unittest.TestCase):
    def test_partial_override_keeps_other_defaults(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.json")
            with open(p, "w") as f:
                json.dump({"idle_min": 5, "alerts": {"sess": 80}}, f)
            cfg = cydconfig.load(p)
            self.assertEqual(cfg["idle_min"], 5)
            self.assertEqual(cfg["alerts"], {"sess": 80, "week": 95, "disk_free_pct": 10})
            self.assertEqual(cfg["night"], ["23:00", "07:00"])

    def test_bad_file_falls_back(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.json")
            with open(p, "w") as f:
                f.write("{oops")
            self.assertEqual(cydconfig.load(p)["idle_min"], 10)


    def test_wrong_types_fall_back_per_setting(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.json")
            with open(p, "w") as f:
                json.dump({"idle_min": "ten", "night": "23:00", "services": "x",
                           "alerts": {"sess": "high", "week": 80}}, f)
            cfg = cydconfig.load(p)
            self.assertEqual(cfg["idle_min"], 10)
            self.assertEqual(cfg["night"], ["23:00", "07:00"])
            self.assertEqual(cfg["services"], ["com.user.claude-cyd"])
            self.assertEqual(cfg["alerts"], {"sess": 90, "week": 80, "disk_free_pct": 10})

    def test_top_level_not_an_object(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.json")
            with open(p, "w") as f:
                f.write("[1, 2]")
            self.assertEqual(cydconfig.load(p), cydconfig.DEFAULTS)

    def test_night_validation(self):
        for bad in (["25:00", "07:00"], ["23:00"], ["a", "b"], [1, 2], 5):
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "c.json")
                with open(p, "w") as f:
                    json.dump({"night": bad}, f)
                self.assertEqual(cydconfig.load(p)["night"], ["23:00", "07:00"], bad)

    def test_night_can_be_disabled_or_changed(self):
        for val in (None, ["22:30", "6:15"]):
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "c.json")
                with open(p, "w") as f:
                    json.dump({"night": val}, f)
                self.assertEqual(cydconfig.load(p)["night"], val)

    def test_services_capped_and_filtered(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.json")
            with open(p, "w") as f:
                json.dump({"services": ["a", 5, "", None, "b", "c", "d", "e", "f"]}, f)
            self.assertEqual(cydconfig.load(p)["services"], ["a", "b", "c", "d"])
            self.assertEqual(len(cydconfig.load(p)["services"]), cydconfig.MAX_SERVICES)

    def test_idle_min_must_be_positive(self):
        for bad in (0, -5, True):
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "c.json")
                with open(p, "w") as f:
                    json.dump({"idle_min": bad}, f)
                self.assertEqual(cydconfig.load(p)["idle_min"], 10, bad)


class RulesRobustnessTests(unittest.TestCase):
    def test_malformed_state_does_not_raise(self):
        cfg = cydconfig.load("/nonexistent")
        for state in ({"s": {"p": 99}}, {"s": {"p": "x", "r": 5000}}, {"s": 7}, [], "junk"):
            self.assertEqual(rules.compute_alerts(state, {}, cfg, 1000), [], state)


class CostBaseTests(unittest.TestCase):
    def test_new_session_today_counts_cost_from_first_row(self):
        # First row is already $1.50 in, but dur proves the session began today.
        rows = [dict(row(D0 + 3600, "a", 1.5), dur=900), dict(row(D0 + 7200, "a", 2.0), dur=4500)]
        self.assertAlmostEqual(activity.summarize(rows, NOW, 0)["today"], 2.0)

    def test_session_older_than_history_keeps_conservative_base(self):
        # dur says it began yesterday, but we have no earlier row: only count growth.
        rows = [dict(row(D0 + 3600, "a", 5.0), dur=2 * 86400), dict(row(D0 + 7200, "a", 5.4), dur=2 * 86400 + 3600)]
        self.assertAlmostEqual(activity.summarize(rows, NOW, 0)["today"], 0.4)

    def test_missing_or_bad_dur_is_conservative(self):
        for dur in (None, -5, "x"):
            rows = [dict(row(D0 + 100, "a", 5.0), dur=dur), dict(row(D0 + 200, "a", 5.4), dur=dur)]
            self.assertAlmostEqual(activity.summarize(rows, NOW, 0)["today"], 0.4, msg=dur)

    def test_earlier_row_still_wins(self):
        rows = [dict(row(D0 - 600, "a", 3.0), dur=100), dict(row(D0 + 600, "a", 4.0), dur=1300)]
        self.assertAlmostEqual(activity.summarize(rows, NOW, 0)["today"], 1.0)

    def test_timezone_respected(self):
        # 23:30 UTC is already "today" at UTC+1; the session began 10 minutes earlier, also today.
        ts = D0 - 1800
        rows = [dict(row(ts, "a", 1.0), dur=600), dict(row(ts + 60, "a", 1.2), dur=660)]
        self.assertAlmostEqual(activity.summarize(rows, D0 + 3600, 3600)["today"], 1.2)


class HistoryLockTests(unittest.TestCase):
    def test_prune_waits_for_the_lock(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "h.jsonl")
            with open(p, "w") as f:
                f.write(json.dumps(row(NOW - 20 * DAY)) + "\n")
            done = threading.Event()

            def run():
                activity.prune(p, now=NOW, tz=0)
                done.set()

            with activity.lock(p):
                t = threading.Thread(target=run)
                t.start()
                self.assertFalse(done.wait(0.3))  # blocked while we hold the lock
            self.assertTrue(done.wait(5))
            t.join()
            self.assertEqual(activity.load(p), [])

    def test_prune_does_not_lose_concurrent_appends(self):
        with tempfile.TemporaryDirectory() as d:
            old_dir = statusline.DIR
            statusline.DIR = d
            try:
                p = os.path.join(d, "history.jsonl")
                now = int(time.time())
                state = {"cost": 0.0, "la": 0, "lr": 0, "s": None, "w": None, "dur": 0}
                N = 150

                def appender():
                    for i in range(N):
                        statusline.append_history(now + i, "sess", dict(state, cost=float(i)))

                t = threading.Thread(target=appender)
                t.start()
                while t.is_alive():
                    with activity.lock(p):  # plant an expired row so every prune rewrites
                        with open(p, "a") as f:
                            f.write(json.dumps(row(now - 30 * DAY)) + "\n")
                    activity.prune(p, now=now, tz=0)
                t.join()
                activity.prune(p, now=now, tz=0)
                self.assertEqual(len(activity.load(p)), N)
            finally:
                statusline.DIR = old_dir


class StatuslineHistoryTests(unittest.TestCase):
    def test_unchanged_rows_are_deduped_until_the_heartbeat(self):
        with tempfile.TemporaryDirectory() as d:
            old_dir = statusline.DIR
            statusline.DIR = d
            try:
                st = {"cost": 1.0, "la": 1, "lr": 0, "s": None, "w": None, "dur": 5}
                statusline.append_history(1000, "abc", st)
                statusline.append_history(1030, "abc", st)                        # duplicate
                statusline.append_history(1000 + statusline.HEARTBEAT, "abc", st)  # heartbeat
                statusline.append_history(1040, "abc", dict(st, cost=1.1))         # changed
                rows = activity.load(os.path.join(d, "history.jsonl"))
                self.assertEqual([r["ts"] for r in rows], [1000, 1040, 1000 + statusline.HEARTBEAT])
            finally:
                statusline.DIR = old_dir


class FakeSampler:
    def __init__(self, services):
        self.services = services

    def snapshot(self, services):
        n = len(services)
        return {"cpu": 99, "mem": 99, "disk": 99, "free": 9999, "up": 99999999, "rx": 99999,
                "tx": 99999, "svc": [1] * n, "svn": ["x" * 12] * n, "total": 10, "free_b": 5}


class FakeActivity:
    def summary(self, now, tz):
        return {"idle": 5, "today": 1234.56, "n": 12, "days": [100.0] * 7}


class BridgeFrameTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.old = bridge.STATE
        bridge.STATE = os.path.join(self.d.name, "state.json")

    def tearDown(self):
        bridge.STATE = self.old
        self.d.cleanup()

    def write_state(self, text):
        with open(bridge.STATE, "w") as f:
            f.write(text)

    def test_worst_case_frame_fits_the_firmware_buffer(self):
        self.write_state(json.dumps({
            "ts": 1, "s": {"p": 99.5, "r": 2000000000}, "w": {"p": 99.5, "r": 2000000000},
            "m": "Sonnet 5.5 (1M context)", "c": 100, "cost": 12345.67, "dur": 99999999,
            "la": 9999999, "lr": 9999999}))
        cfg = cydconfig.load("/nonexistent")
        cfg["services"] = ["svc%d" % i for i in range(20)]  # as if the config cap were bypassed
        frame = bridge.build_frame(cfg, FakeSampler(cfg["services"]), FakeActivity(), now=1700000000)
        line = json.dumps(frame, separators=(",", ":"))
        self.assertLess(len(line), 1000)  # firmware buffer is 1024 incl. newline
        self.assertLessEqual(len(frame["sys"]["svc"]), cydconfig.MAX_SERVICES)

    def test_non_object_state_is_ignored(self):
        cfg = cydconfig.load("/nonexistent")
        for text in ("[1,2]", "null", "7", "{oops"):
            self.write_state(text)
            frame = bridge.build_frame(cfg, FakeSampler([]), FakeActivity(), now=1700000000)
            self.assertNotIn("age", frame, text)
            self.assertEqual(frame["al"], [])


class IncrementalActivityTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.p = os.path.join(self.d.name, "h.jsonl")
        self.loads = 0
        self._load = activity.load

        def counting(path=activity.HISTORY):
            self.loads += 1
            return self._load(path)

        activity.load = counting

    def tearDown(self):
        activity.load = self._load
        self.d.cleanup()

    def append(self, *rows, partial=None):
        with open(self.p, "a") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")
            if partial:
                f.write(partial)

    def test_appends_are_read_without_a_full_reload(self):
        a = activity.Activity(self.p)
        self.assertEqual(a.rows(), [])
        self.append(row(NOW - 100, cost=1.0))
        self.assertEqual(len(a.rows()), 1)
        self.append(row(NOW - 50, cost=2.0), row(NOW - 40, cost=3.0))
        self.assertEqual([r["cost"] for r in a.rows()], [1.0, 2.0, 3.0])
        self.assertEqual(self.loads, 0)

    def test_half_written_line_waits_for_its_newline(self):
        a = activity.Activity(self.p)
        self.append(row(NOW - 100), partial='{"ts": %d, "si' % (NOW - 50))
        self.assertEqual(len(a.rows()), 1)
        with open(self.p, "a") as f:
            f.write('d": "a", "cost": 1.0}\n')
        self.assertEqual(len(a.rows()), 2)

    def test_out_of_order_and_corrupt_rows(self):
        a = activity.Activity(self.p)
        self.append(row(NOW - 10))
        a.rows()
        self.append(row(NOW - 90), row(NOW - 30))
        with open(self.p, "a") as f:
            f.write("garbage\n")
        self.assertEqual([r["ts"] for r in a.rows()], [NOW - 90, NOW - 30, NOW - 10])

    def test_prune_replacing_the_file_is_noticed(self):
        a = activity.Activity(self.p)
        self.append(row(NOW - 20 * DAY), row(NOW - 3600))
        self.assertEqual(len(a.rows()), 2)
        activity.prune(self.p, now=NOW, tz=0)
        self.assertEqual([r["ts"] for r in a.rows()], [NOW - 3600])
        self.append(row(NOW - 60))
        self.assertEqual(len(a.rows()), 2)

    def test_deleted_file_empties_the_cache(self):
        a = activity.Activity(self.p)
        self.append(row(NOW - 60))
        self.assertEqual(len(a.rows()), 1)
        os.remove(self.p)
        self.assertEqual(a.rows(), [])

    def test_summary_matches_a_full_summarize(self):
        a = activity.Activity(self.p)
        rows = [row(D0 - 3600, "a", 3.0, w=10), row(D0 + 100, "a", 4.0, w=12),
                row(D0 + 200, "b", 0.5, w=13), row(D0 + 300, "b", 1.5, w=15)]
        for i, r in enumerate(rows):
            self.append(r)
            got = a.summary(NOW, 0)
            self.assertEqual(got, activity.summarize(rows[:i + 1], NOW, 0), i)

    def test_summary_is_cached_until_something_changes(self):
        calls = []
        real = activity.aggregate
        activity.aggregate = lambda *a: calls.append(a) or real(*a)
        try:
            a = activity.Activity(self.p)
            self.append(row(NOW - 100, cost=1.0))
            first = a.summary(NOW, 0)
            again = a.summary(NOW + 5, 0)
            self.assertEqual(len(calls), 1)
            self.assertEqual(again["idle"], first["idle"] + 5)  # idle still moves with the clock
            self.append(row(NOW - 50, cost=2.0))
            a.summary(NOW + 10, 0)
            self.assertEqual(len(calls), 2)       # new data
            a.summary(NOW + DAY, 0)
            self.assertEqual(len(calls), 3)       # new day
        finally:
            activity.aggregate = real


class LogCapTests(unittest.TestCase):
    def test_capped_append_keeps_the_newest_lines(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.log")
            for i in range(500):
                activity.capped_append(p, "line %04d\n" % i, max_bytes=1000)
            self.assertLess(os.path.getsize(p), 1100)
            with open(p) as f:
                lines = f.read().splitlines()
            self.assertEqual(lines[-1], "line 0499")
            self.assertTrue(all(l.startswith("line ") and len(l) == 9 for l in lines))

    def test_truncate_if_large(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "b.log")
            with open(p, "w") as f:
                f.write("x" * 50)
            activity.truncate_if_large(p, max_bytes=100)
            self.assertEqual(os.path.getsize(p), 50)
            activity.truncate_if_large(p, max_bytes=10)
            self.assertEqual(os.path.getsize(p), 0)
            activity.truncate_if_large(os.path.join(d, "missing"), max_bytes=1)  # no error


class FakePsutil:
    @staticmethod
    def cpu_percent(_):
        return 5.0

    @staticmethod
    def virtual_memory():
        return types.SimpleNamespace(total=100, available=40)

    @staticmethod
    def disk_usage(_):
        return types.SimpleNamespace(percent=50, free=10e9, total=20e9)

    @staticmethod
    def net_io_counters(pernic=True):
        return {"en0": types.SimpleNamespace(bytes_recv=0, bytes_sent=0)}

    @staticmethod
    def boot_time():
        return time.time() - 1000


class SamplerTests(unittest.TestCase):
    def setUp(self):
        import sysstats
        self.sysstats = sysstats
        self.old = (sysstats.psutil, sysstats.service_up, sysstats.CACHE_S, sysstats.SERVICE_S)
        sysstats.psutil = FakePsutil
        sysstats.CACHE_S = 0
        sysstats.SERVICE_S = 0.05

    def tearDown(self):
        (self.sysstats.psutil, self.sysstats.service_up,
         self.sysstats.CACHE_S, self.sysstats.SERVICE_S) = self.old

    def test_slow_service_check_does_not_block_snapshots(self):
        gate = threading.Event()
        calls = []

        def service_up(label):
            calls.append(label)
            if len(calls) > 1:   # the first look is synchronous; later ones are wedged
                gate.wait(5)
            return 1

        self.sysstats.service_up = service_up
        s = self.sysstats.Sampler()
        self.assertEqual(s.snapshot(["a"])["svc"], [1])  # first frame is complete
        time.sleep(0.1)
        t0 = time.time()
        snap = s.snapshot(["a"])
        self.assertLess(time.time() - t0, 0.5)           # background refresh, not inline
        self.assertEqual(snap["svc"], [1])               # last known state still reported
        self.assertEqual(len(calls), 2)
        s.snapshot(["a"])
        self.assertEqual(len(calls), 2)                  # only one refresh in flight
        gate.set()

    def test_background_refresh_updates_the_state(self):
        state = {"up": 1}
        self.sysstats.service_up = lambda label: state["up"]
        s = self.sysstats.Sampler()
        self.assertEqual(s.snapshot(["a"])["svc"], [1])
        state["up"] = 0
        deadline = time.time() + 3
        while time.time() < deadline and s.snapshot(["a"])["svc"] != [0]:
            time.sleep(0.05)
        self.assertEqual(s.snapshot(["a"])["svc"], [0])

    def test_changed_service_list_is_checked_straight_away(self):
        self.sysstats.service_up = lambda label: 0 if label == "bad" else 1
        s = self.sysstats.Sampler()
        s.snapshot(["a"])
        self.assertEqual(s.snapshot(["a", "bad"])["svc"], [1, 0])


class StatuslineNormaliseTests(unittest.TestCase):
    NOW = 1_700_000_000

    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.old = statusline.DIR
        statusline.DIR = self.d.name

    def tearDown(self):
        statusline.DIR = self.old
        self.d.cleanup()

    def payload(self, s=(40, 5000), w=(10, 90000), **extra):
        raw = {"model": {"display_name": "Sonnet 5.5"},
               "cost": {"total_cost_usd": 1.5, "total_duration_ms": 90000,
                        "total_lines_added": 3, "total_lines_removed": 1},
               "context_window": {"used_percentage": 22}, "rate_limits": {}}
        if s:
            raw["rate_limits"]["five_hour"] = {"used_percentage": s[0], "resets_at": self.NOW + s[1]}
        if w:
            raw["rate_limits"]["seven_day"] = {"used_percentage": w[0], "resets_at": self.NOW + w[1]}
        raw.update(extra)
        return raw

    def test_normal_payload(self):
        st = statusline.normalise(self.payload(), {}, self.NOW)
        self.assertEqual(st["s"], {"p": 40.0, "r": self.NOW + 5000})
        self.assertEqual((st["m"], st["c"], st["cost"], st["dur"], st["la"], st["lr"]),
                         ("Sonnet 5.5", 22, 1.5, 90, 3, 1))
        self.assertEqual(statusline.summary_line(st), "Sonnet 5.5 | 5h 40% | 7d 10%")

    def test_non_numeric_values_are_dropped_not_fatal(self):
        raw = self.payload()
        raw["rate_limits"]["five_hour"]["used_percentage"] = "lots"
        raw["rate_limits"]["seven_day"]["resets_at"] = "soon"
        raw["cost"] = {"total_cost_usd": "free", "total_duration_ms": "long",
                       "total_lines_added": None, "total_lines_removed": True}
        raw["model"] = {"display_name": 7}
        st = statusline.normalise(raw, {}, self.NOW)
        self.assertIsNone(st["s"])
        self.assertIsNone(st["w"])
        self.assertEqual((st["m"], st["cost"], st["dur"], st["la"], st["lr"]), ("", None, 0, None, None))
        self.assertEqual(statusline.summary_line(st), "claude")

    def test_corrupt_previous_state_is_ignored(self):
        for prev in ([1], "x", {"s": 5}, {"s": {"p": "a", "r": None}}, {"s": {"p": 1}}):
            raw = self.payload(s=None)  # window missing -> would reuse prev if it were valid
            st = statusline.normalise(raw, prev, self.NOW)
            self.assertIsNone(st["s"], prev)

    def test_missing_window_keeps_the_old_one_until_it_resets(self):
        prev = {"s": {"p": 60.0, "r": self.NOW + 100}}
        self.assertEqual(statusline.normalise(self.payload(s=None), prev, self.NOW)["s"], prev["s"])
        self.assertIsNone(statusline.normalise(self.payload(s=None), prev, self.NOW + 200)["s"])

    def test_lower_reading_in_the_same_window_is_a_stale_session(self):
        prev = {"s": {"p": 70.0, "r": self.NOW + 5000}}
        st = statusline.normalise(self.payload(s=(65, 5000)), prev, self.NOW)
        self.assertEqual(st["s"]["p"], 70.0)
        with open(os.path.join(self.d.name, "regress.log")) as f:
            self.assertIn("kept=70.0", f.read())
        # a new window (different reset time) is trusted even when lower
        self.assertEqual(statusline.normalise(self.payload(s=(5, 99999)), prev, self.NOW)["s"]["p"], 5.0)

    def test_main_ignores_input_that_is_not_an_object(self):
        import io
        from contextlib import redirect_stdout
        old_in = sys.stdin
        try:
            for text in ("[1, 2]", "null", "7", "{oops", ""):
                sys.stdin = io.StringIO(text)
                out = io.StringIO()
                with redirect_stdout(out):
                    statusline.main()
                self.assertEqual(out.getvalue().strip(), "claude", text)
            self.assertFalse(os.path.exists(os.path.join(self.d.name, "state.json")))
        finally:
            sys.stdin = old_in

    def test_main_writes_state_and_history(self):
        import io
        from contextlib import redirect_stdout
        old_in = sys.stdin
        try:
            sys.stdin = io.StringIO(json.dumps(dict(self.payload(), session_id="abcdef123456")))
            out = io.StringIO()
            with redirect_stdout(out):
                statusline.main()
        finally:
            sys.stdin = old_in
        self.assertIn("Sonnet 5.5", out.getvalue())
        with open(os.path.join(self.d.name, "state.json")) as f:
            self.assertEqual(json.load(f)["cost"], 1.5)
        rows = activity.load(os.path.join(self.d.name, "history.jsonl"))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["sid"], "abcdef12")


class HistoryLoadTests(unittest.TestCase):
    def test_string_and_infinite_timestamps(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "h.jsonl")
            with open(p, "w") as f:
                f.write('{"ts": "123", "sid": "a"}\n')       # numeric string: coerced
                f.write('{"ts": Infinity, "sid": "a"}\n')    # json.loads accepts this; skip it
                f.write('{"ts": 150, "sid": "a"}\n')
                f.write("[1, 2]\n")
            self.assertEqual([r["ts"] for r in activity.load(p)], [123, 150])
            a = activity.Activity(p)
            self.assertEqual([r["ts"] for r in a.rows()], [123, 150])


def mode(path):
    return os.stat(path).st_mode & 0o777


class PrivateFileTests(unittest.TestCase):
    def test_logs_lock_and_pruned_history_are_owner_only(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.log")
            for i in range(200):
                activity.capped_append(p, "line %d\n" % i, max_bytes=500)  # includes a rewrite
            self.assertEqual(mode(p), 0o600)

            h = os.path.join(d, "history.jsonl")
            with open(h, "w") as f:
                f.write(json.dumps(row(NOW - 20 * DAY)) + "\n" + json.dumps(row(NOW)) + "\n")
            activity.prune(h, now=NOW, tz=0)
            self.assertEqual(mode(h), 0o600)
            self.assertEqual(mode(h + ".lock"), 0o600)


class InstallHelperTests(unittest.TestCase):
    TEMPLATE = os.path.join(os.path.dirname(__file__), "..", "com.user.claude-cyd.plist.in")

    def test_plist_survives_awkward_paths(self):
        import plistlib
        py = "/Users/A & B/My <Dir>/#1/venv/bin/python"
        bridge_py = "/Users/A & B/My <Dir>/#1/bridge.py"
        log = "/Users/A & B/My <Dir>/#1/bridge.log"
        with open(self.TEMPLATE) as f:
            out = install_helpers.render_plist(f.read(), py, bridge_py, log)
        plist = plistlib.loads(out.encode())
        self.assertEqual(plist["ProgramArguments"], [py, bridge_py])
        self.assertEqual(plist["StandardOutPath"], log)
        self.assertEqual(plist["StandardErrorPath"], log)
        self.assertNotIn("__", out)

    def test_statusline_snippet_quotes_the_command(self):
        import shlex
        for path in ("/Users/me/.claude-cyd/statusline.py", "/Users/A B/it's here/statusline.py"):
            snippet = json.loads(install_helpers.statusline_snippet(path))["statusLine"]
            self.assertEqual(snippet["type"], "command")
            self.assertEqual(shlex.split(snippet["command"]), ["python3", path])
            self.assertEqual(snippet["refreshInterval"], 30)

    def test_requirements_are_pinned_and_hashed(self):
        with open(os.path.join(os.path.dirname(__file__), "..", "requirements.txt")) as f:
            text = f.read()
        for name in ("psutil", "pyserial"):
            self.assertRegex(text, r"(?m)^%s==\d+(\.\d+)* \\$" % name)
        pins = [l for l in text.splitlines() if "==" in l]
        self.assertEqual(len(pins), 2)
        self.assertGreaterEqual(text.count("--hash=sha256:"), 2)


if __name__ == "__main__":
    unittest.main()
