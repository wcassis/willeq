#!/usr/bin/env python3
"""B946 (#264): the rig's half of the [gldriver] net.

The client emits ONE line at GL init --
    [gldriver] renderer='<GL_RENDERER>' gallium=<path|none> llvm=<path|none>
(src/client/core/gl_driver_census.cpp; the C++ parser's fixtures are
tests/test_gl_driver_census.cpp). rendered_zone_summarize.mesa_driver_field
reads it back from the run's client.log and the row carries it (mesa_driver,
appended at the END of the TSV header). These tests drive the production
function over synthetic lines -- no Pi, no GL, no maps snapshots -- and check
the decision the card made explicit: the line is ADDITIVE, not a replacement.
The maps-snapshot proof (mesa_proof, exit 67 / mesa_mismatch) keeps its
rules; the line is the second, independent read of the same fact, visible
even when the snapshots are missing.
"""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import rendered_zone_summarize as summary

LINE = ("2026-10-10 12:00:00.123 [INFO ] [GRAPHICS] "
        "[gldriver] renderer='Mali400 (Lima)' "
        "gallium=/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so llvm=none")


def point(epoch, name, zone):
    return (f"{epoch:.4f} [memledger] point={name} zone={zone} rss_anon=100 "
            "allocs=0 zone_mesh=10/10 textures_driver=10/10 irrlicht_scene=10/10 gpu_driver=10/10")


def run_main(client_log=LINE):
    lines = ([client_log] if client_log else []) + [
        point(1, "game_zone_begin", "qeynos2"),
        point(2, "ready", "qeynos2"),
        point(63, "steady_60s", "qeynos2")]
    with tempfile.TemporaryDirectory() as root:
        path = Path(root)
        (path / "client.log").write_text("\n".join(lines))
        (path / "summary.json").write_text(json.dumps({
            "start_epoch": 0, "end_epoch": 150, "returncode": 0,
            "kill_reason": "", "points": [],
        }))
        args = ["summary", "--run", root, "--zone", "qeynos2",
                "--attempt", "test", "--tsv", str(path / "results.tsv")]
        with patch.object(sys, "argv", args), \
                patch.object(summary, "frame_check", return_value=(1, 1, [])), \
                patch.object(summary, "mesa_proof", return_value=([], "n", "ok")), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            summary.main()
        row = json.loads((path / "details.json").read_text())["row"]
        tsv = (path / "results.tsv").read_text().splitlines()
        header = tsv[0].split("\t")
        return row, header, tsv[1].split("\t"), out.getvalue()


class MesaDriverFieldTest(unittest.TestCase):
    def test_reads_the_client_line(self):
        field = summary.mesa_driver_field([LINE])
        self.assertEqual(
            field,
            "Mali400 (Lima):/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so:none")

    def test_absent_line_is_empty(self):
        # A client that predates B946 (or a run that never reached GL init):
        # the column is empty, the maps-snapshot proof is what remains.
        self.assertEqual(summary.mesa_driver_field(
            [point(1, "ready", "qeynos2")]), "")

    def test_first_line_wins(self):
        # The line is emitted once; a stray second occurrence (e.g. a reload
        # path) must not override the first.
        other = LINE.replace("renderer='Mali400 (Lima)'", "renderer='other'")
        self.assertEqual(summary.mesa_driver_field([LINE, other]),
                         summary.mesa_driver_field([LINE]))
        self.assertEqual(summary.mesa_driver_field([LINE, other]),
                         "Mali400 (Lima):/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so:none")

    def test_system_mesa_line(self):
        line = ("2026-10-10 12:00:00.000 [INFO ] [GRAPHICS] "
                "[gldriver] renderer='llvmpipe (Mesa 25.1.7-1)' "
                "gallium=/usr/lib/x86_64-linux-gnu/dri/swrast_dri.so "
                "llvm=/usr/lib/x86_64-linux-gnu/libLLVM.so.18.1")
        self.assertEqual(
            summary.mesa_driver_field([line]),
            "llvmpipe (Mesa 25.1.7-1):/usr/lib/x86_64-linux-gnu/dri/swrast_dri.so"
            ":/usr/lib/x86_64-linux-gnu/libLLVM.so.18.1")

    def test_row_carries_the_field_and_header_extends_at_end(self):
        row, header, tsv_row, printed = run_main(LINE)
        self.assertEqual(row["mesa_driver"],
                         "Mali400 (Lima):/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so:none")
        # Appended at the END: every pre-B946 column keeps its index, so the
        # sweep's existing `cut -f5` / `cut -f11` stay put (the B935/B955 rule).
        self.assertEqual(header[-1], "mesa_driver")
        self.assertEqual(len(header), len(tsv_row))
        self.assertEqual(tsv_row[header.index("mesa_driver")], row["mesa_driver"])
        self.assertIn("driver=Mali400 (Lima):/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so:none",
                      printed)

    def test_old_log_leaves_column_empty(self):
        # B946-: no [gldriver] line in the log -> empty column; the row still
        # carries the maps-snapshot proof (stubbed ok here) and passes.
        row, header, tsv_row, printed = run_main("")
        self.assertEqual(row["mesa_driver"], "")
        self.assertEqual(tsv_row[header.index("mesa_driver")], "")
        self.assertIn("driver=-", printed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
