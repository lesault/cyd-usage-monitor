"""Run: .venv/bin/python -m unittest discover -s host/tests -v"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import activity  # noqa: E402
import cydconfig  # noqa: E402
import rules  # noqa: E402

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


if __name__ == "__main__":
    unittest.main()
