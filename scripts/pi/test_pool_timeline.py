#!/usr/bin/env python3
# B1013-1 fixture provenance (tests/data/b1013/, captured by
# /home/user/kb-evidence/b1013-1-pool-timeline-counters/copy_fixture.sh on
# 2026-10-12; source dir: the B997 shipped-default Pi leg, ARM build 593eba6d,
# lane 2, imp200far, --idle 180 --hold-after 75, --bo-census):
#   bo_events.log  190711 B, 6329 lines
#     sha256 0bda173211730d30a93d7ce8d4129bab6650b3f0f4fce1d375f0e670d2167122
#     (the card's card-expected digest carried one extra 'a' -- 65 hex chars,
#     not 64; the file's real digest is this, re-derived from the source copy
#     by /home/user/kb-evidence/b1013-1-pool-timeline-counters/check_hash.py;
#     every other value in the card matched this file exactly)
#   gem_owners.tsv cut 66552-70279 (the final P ready block through E steady_60s)
#     231514 B, sha256 c2494b41bb99b54740249eeb010f0da90cf36f7e506cc4a0193d35cbd32d85f8
import hashlib
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gem_owner_table as g  # noqa: E402

DATA = os.path.join(HERE, "..", "..", "tests", "data", "b1013")
EVENTS = os.path.join(DATA, "qeynos2_default_bo_events.log")
DUMP = os.path.join(DATA, "qeynos2_default_gem_owners_ready_steady.tsv")

# The fixture is the evidence: a file whose bytes changed is a different leg,
# and the expected values below stop meaning anything.
PINNED = {
    EVENTS: "0bda173211730d30a93d7ce8d4129bab6650b3f0f4fce1d375f0e670d2167122",
    DUMP: "c2494b41bb99b54740249eeb010f0da90cf36f7e506cc4a0193d35cbd32d85f8",
}


def sha256_of(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class PoolTimelineTest(unittest.TestCase):
    def test_fixture_bytes_are_pinned(self):
        for path, want in PINNED.items():
            self.assertEqual(sha256_of(path), want, os.path.basename(path))

    def test_pool_timeline_qeynos2_default(self):
        # B1013 (#334): the pool (fact 13) at the two ledger points, plus the
        # event-log timeline between them. Values from the card at cut,
        # re-derived here from the two fixture files:
        #   ready (mono 2740370.512877): draw-time 5292032 B / 1051 BOs,
        #                                stream uploader 3 BOs = 3145728 B
        #   steady_60s (mono 2740430.695843): draw-time 9392128 B / 1852 BOs,
        #                                    stream uploader 7 BOs = 7340032 B
        #   growth = (9392128 + 7340032) - (5292032 + 3145728) = 8294400 B
        #          = 7.91015625 MiB
        #   last create in (ready, steady_60s] = 2740381.257002 (bo_events.log
        #   line 4026) -> 10.744125 s after ready
        #   steady window [ready+20 s, steady_60s]: 0 creates, 0 closes
        #   cross-check: creates (ready, steady] = 843 = 3157 - 2314 (the P
        #   lines' created field); closes = 15 = 881 - 866 (closed)
        r = g.pool_timeline(EVENTS, DUMP)
        self.assertIsNotNone(r)
        self.assertEqual(r["ready"]["mono"], 2740370.512877)
        self.assertEqual(r["steady_60s"]["mono"], 2740430.695843)
        self.assertEqual(r["ready"]["draw_time_bytes"], 5292032)
        self.assertEqual(r["ready"]["draw_time_count"], 1051)
        self.assertEqual(r["ready"]["stream_uploader_bytes"], 3145728)
        self.assertEqual(r["ready"]["stream_uploader_count"], 3)
        self.assertEqual(r["steady_60s"]["draw_time_bytes"], 9392128)
        self.assertEqual(r["steady_60s"]["draw_time_count"], 1852)
        self.assertEqual(r["steady_60s"]["stream_uploader_bytes"], 7340032)
        self.assertEqual(r["steady_60s"]["stream_uploader_count"], 7)
        self.assertEqual(r["growth_bytes"], 8294400)
        self.assertAlmostEqual(r["growth_mib"], 8294400 / 1048576.0)
        self.assertAlmostEqual(r["last_create_s"], 10.744125, delta=1e-6)
        self.assertEqual(r["steady_window_creates"], 0)
        self.assertEqual(r["steady_window_closes"], 0)
        self.assertEqual(r["creates_between"], 843)
        self.assertEqual(r["closes_between"], 15)

    def test_pool_timeline_missing_event_log_is_none(self):
        self.assertIsNone(g.pool_timeline(os.path.join(DATA, "nope.log"), DUMP))

    def test_pool_timeline_missing_points_is_none(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as f:
            f.write("P\tstartup\t1.0\t1\t4096\t0\t0\t0\n"
                    "B\t7\t1\t4096\t0\t0\t1\t1\t-\t-\n"
                    "E\tstartup\n")
        try:
            self.assertIsNone(g.pool_timeline(EVENTS, f.name))
        finally:
            os.unlink(f.name)

    def test_pool_timeline_event_log_without_clock_header(self):
        # pool_timeline cuts the log on the dump's own CLOCK_MONOTONIC stamps,
        # so the '# clock' header (a realtime<->monotonic translation) is not
        # needed. Same events, minus the header.
        import tempfile
        with open(EVENTS) as f:
            raw = f.read()
        assert raw.startswith("# clock "), "the fixture lost its clock header"
        with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as t:
            t.write(raw.split("\n", 1)[1])
        try:
            r = g.pool_timeline(t.name, DUMP)
            self.assertIsNotNone(r)
            self.assertEqual(r["steady_window_creates"], 0)
            self.assertEqual(r["creates_between"], 843)
        finally:
            os.unlink(t.name)


if __name__ == "__main__":
    unittest.main()
