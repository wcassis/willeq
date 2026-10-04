#!/usr/bin/env python3
"""B975: intentional hops cannot borrow another visit's ready/steady evidence."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import rendered_zone_summarize as summary


def point(epoch, name, zone):
    return (f"{epoch:.4f} [memledger] point={name} zone={zone} rss_anon=100 "
            "allocs=0 zone_mesh=10/10 textures_driver=10/10 irrlicht_scene=10/10 gpu_driver=10/10")


class FinalVisitTest(unittest.TestCase):
    def verdict(self, final_points, final_visit=True):
        lines = [point(1, "game_zone_begin", "qeynos2"),
                 point(2, "ready", "qeynos2"),
                 point(63, "steady_60s", "qeynos2"),
                 point(70, "game_zone_begin", "arena"),
                 point(71, "ready", "arena")] + final_points
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            (path / "client.log").write_text("\n".join(lines))
            (path / "summary.json").write_text(json.dumps({
                "start_epoch": 0, "end_epoch": 150, "returncode": 0,
                "kill_reason": "", "points": [],
            }))
            args = ["summary", "--run", root, "--zone", "qeynos2",
                    "--attempt", "test", "--tsv", str(path / "results.tsv")]
            if final_visit:
                args.append("--final-visit")
            with patch.object(sys, "argv", args), \
                    patch.object(summary, "frame_check", return_value=(1, 1, [])), \
                    patch.object(summary, "mesa_proof", return_value=([], "n", "ok")), \
                    contextlib.redirect_stdout(io.StringIO()):
                summary.main()
            return json.loads((path / "details.json").read_text())["row"]

    def test_return_to_target_passes_only_in_tour_mode(self):
        points = [point(80, "game_zone_begin", "qeynos2"),
                  point(81, "ready", "qeynos2"), point(142, "steady_60s", "qeynos2")]
        self.assertEqual(self.verdict(points)["verdict"], "pass")
        self.assertIn("moved_after_ready", self.verdict(points, False)["signature"])

    def test_failed_return_cannot_reuse_first_visit(self):
        row = self.verdict([point(80, "game_zone_begin", "qeynos2")])
        self.assertEqual(row["verdict"], "fail")
        self.assertEqual(row["reached_ready"], "n")

    def test_return_without_steady_cannot_reuse_first_visit(self):
        row = self.verdict([point(80, "game_zone_begin", "qeynos2"),
                            point(81, "ready", "qeynos2")])
        self.assertEqual(row["verdict"], "fail")
        self.assertEqual(row["reached_steady"], "n")

    def test_steady_before_ready_does_not_count(self):
        row = self.verdict([point(80, "game_zone_begin", "qeynos2"),
                            point(81, "steady_60s", "qeynos2"),
                            point(82, "ready", "qeynos2")])
        self.assertEqual(row["verdict"], "fail")
        self.assertEqual(row["reached_steady"], "n")

    def test_finishing_in_wrong_zone_fails(self):
        row = self.verdict([point(132, "steady_60s", "arena")])
        self.assertEqual(row["verdict"], "fail")
        self.assertEqual(row["ready_zone"], "arena")


if __name__ == "__main__":
    unittest.main()
