#!/usr/bin/env python3
"""B975: crowd timings exclude spawning and zone-hop warmup."""
import tempfile
import unittest
from pathlib import Path

from frametiming_table import parse


def block(value):
    return ["FRAME TIMING BREAKDOWN (60 frames, 30.0 fps estimate)"] + [
        f"[INFO] {name}: {value} us" for name in
        ("Total Frame", "Scene Draw All", "Solid Pass", "Transparent Pass")]


def point(name, zone="qeynos2"):
    return f"[memledger] point={name} zone={zone} "


class IdlePhaseTest(unittest.TestCase):
    def read(self, lines):
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / "client.log"
            log.write_text("\n".join(lines))
            return parse(log, "qeynos2")

    def test_zone_hop_uses_last_face_before_movement(self):
        # A hop tour faces the crowd before leaving the zone, after returning,
        # and finally at the measured viewpoint. Only the last interval is idle.
        lines = [point("game_zone_begin"), point("ready")]
        for timing in (9000, 8000, 1600):
            lines.extend([
                "Script: executing '/face 0 1 0'",
                f"[nametags] cpu_us_p50={timing}",
            ] + block(timing))
        # The first block after the last face can contain pre-face frames.
        lines += ["[nametags] cpu_us_p50=1600"] + block(1600)
        lines.extend([
            "Script: executing '/move 0 30 0'",
            "[nametags] cpu_us_p50=12000",
            "FRAME TIMING BREAKDOWN (60 frames, 10.0 fps estimate)",
            "[INFO] Total Frame: 12000 us",
        ])
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / "client.log"
            log.write_text("\n".join(lines))
            row = parse(log)
        self.assertTrue(row["idle"])
        self.assertEqual(row["blocks"], 1)
        self.assertEqual(row["cpu_us"], 1600)
        self.assertEqual(row["total"], 1.6)

    def test_empty_idle_does_not_report_warmup_samples(self):
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / "client.log"
            log.write_text("\n".join([
                "[nametags] cpu_us_p50=9000",
                "FRAME TIMING BREAKDOWN (60 frames, 30.0 fps estimate)",
                "[INFO] Total Frame: 9000 us",
                "Script: executing '/face 0 1 0'",
                "Script: executing '/move 0 30 0'",
            ]))
            row = parse(log)
        self.assertEqual(row["blocks"], 0)
        self.assertIsNone(row["cpu_us"])
        self.assertIsNone(row["total"])

    def test_late_ready_excludes_crossing_windows(self):
        row = self.read([point("game_zone_begin"), "Script: executing '/face 0 1 0'"]
                        + block(9000) + ["[nametags] cpu_us_p50=9000", point("ready")]
                        + block(8000) + ["[nametags] cpu_us_p50=8000"]
                        + block(1600) + ["[nametags] cpu_us_p50=1600",
                        "Script: executing '/move 0 30 0'", point("steady_60s")])
        self.assertTrue(row["ready"] and row["steady"])
        self.assertEqual(row["blocks"], 1)
        self.assertEqual(row["total"], 1.6)
        self.assertEqual(row["cpu_us"], 1600)

    def test_no_ready_and_ready_after_movement_fail_closed(self):
        for ending in ([], [point("ready"), point("steady_60s")]):
            row = self.read([point("game_zone_begin"), "Script: executing '/face 0 1 0'"]
                            + block(9000) + block(8000)
                            + ["Script: executing '/move 0 30 0'"] + ending)
            self.assertEqual(row["blocks"], 0)
            self.assertIsNone(row["total"])

    def test_earlier_visit_is_never_borrowed(self):
        earlier = [point("game_zone_begin"), point("ready"),
                   "Script: executing '/face 0 1 0'"] + block(9000) + block(8000) + [
                   "Script: executing '/move 0 30 0'", point("steady_60s"),
                   "[memledger-owner] point=steady_60s owner=ent.nametag_batch live=123"]
        for final in ([point("game_zone_begin")],
                      [point("game_zone_begin"), point("ready")],
                      [point("game_zone_begin", "arena"), point("ready", "arena"),
                       point("steady_60s", "arena")]):
            row = self.read(earlier + final)
            self.assertFalse(row["steady"])
            self.assertEqual(row["blocks"], 0)
            self.assertIsNone(row["row"])

    def test_missing_boundary_or_incomplete_block_has_no_fallback(self):
        for commands in ([], ["Script: executing '/face 0 1 0'"]):
            row = self.read([point("game_zone_begin"), point("ready")] + commands
                            + block(9000) + block(8000))
            self.assertEqual(row["blocks"], 0)
        row = self.read([point("ready"), "Script: executing '/face 0 1 0'"]
                        + block(9000) + block(8000)[:2]
                        + ["Script: executing '/move 0 30 0'"])
        self.assertEqual(row["blocks"], 0)


if __name__ == "__main__":
    unittest.main()
