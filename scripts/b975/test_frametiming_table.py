#!/usr/bin/env python3
"""B975: crowd timings exclude spawning and zone-hop warmup."""
import tempfile
import unittest
from pathlib import Path

from frametiming_table import parse


class IdlePhaseTest(unittest.TestCase):
    def test_zone_hop_uses_last_face_before_movement(self):
        # A hop tour faces the crowd before leaving the zone, after returning,
        # and finally at the measured viewpoint. Only the last interval is idle.
        lines = []
        for timing in (9000, 8000, 1600):
            lines.extend([
                "Script: executing '/face 0 1 0'",
                f"[nametags] cpu_us_p50={timing}",
                "FRAME TIMING BREAKDOWN (60 frames, 30.0 fps estimate)",
                f"[INFO] Total Frame: {timing} us",
            ])
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


if __name__ == "__main__":
    unittest.main()
