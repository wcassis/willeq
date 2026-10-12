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
        # B1013 appended three more after mesa_driver (PoolColumnsTest pins
        # the full list), so mesa_driver is no longer the last column.
        self.assertEqual(header.index("mesa_driver"), 70)
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


# B1013 (#334): the three pool columns are APPENDED AT THE END of HEADER
# (the B935/B955 rule -- the sweep's `cut -f` positions must not move), and
# every pre-existing column keeps its index. The pre-B1013 header (71 columns,
# as of origin/fable e9d85d02, verified against git show
# origin/fable:scripts/pi/rendered_zone_summarize.py) is pinned here as a
# literal, not re-derived: a shift in any of them is a regression.
HEADER_PRE_B1013 = [
    "zone", "expansion", "s3d_bytes", "attempt", "verdict", "signature",
    "exit_code", "kill_reason", "wall_s", "phase_reached", "ready_zone",
    "reached_ready", "reached_steady", "time_to_ready_s", "stats_blocks",
    "frames_rendered_min", "fps_median", "fps_min", "dumped_frames",
    "nonblank_frames", "zone_mesh_ready", "textures_ready",
    "irrlicht_scene_ready", "gpu_driver_ready", "rss_anon_ready",
    "rss_anon_steady", "peak_rss_anon", "peak_rss_anon_point", "vm_hwm",
    "owned_peak", "over_64MB_owned_peak", "over_256MB_peak",
    "driver_so_anon_ready", "driver_so_rss_ready", "sys_mem_used_delta_steady",
    "top5_ready", "top5_steady", "oom", "oom_line", "fatal_count",
    "error_count", "first_fatal", "error_signatures", "rcvbuf",
    "mesa_expected", "mesa_gallium", "llvm_mapped", "mesa_check",
    "gpu_buffers_src", "gpu_buffers_ready", "gpu_buffers_steady",
    "gpu_logical_ready", "gpu_logical_steady", "gpu_residual_steady",
    "dumped_png_bytes_steady", "gpu_buffers_net_steady", "memcg_kernel_steady",
    "drm_mapped_steady", "owned_steady", "owned_rss_anon_steady",
    "scanout_fb_bytes", "gpu_depth_stencil_steady", "gpu_unnamed_steady",
    "owned_rss_anon_peak", "gpu_buffers_at_owned_peak", "gem_frames_steady",
    "gem_pp_jobs_per_frame", "gem_gp_jobs_per_frame", "gem_waits_per_frame",
    "gem_wait_ms_per_frame", "mesa_driver",
]


class PoolColumnsTest(unittest.TestCase):
    def test_header_extends_at_end(self):
        self.assertEqual(summary.HEADER[-3:],
                         ["gem_pool_growth_mib", "gem_pool_last_create_s",
                          "gem_pool_creates_steady_window"])
        self.assertEqual(len(summary.HEADER), len(HEADER_PRE_B1013) + 3)
        self.assertEqual(summary.HEADER[:-3], HEADER_PRE_B1013)

    def test_row_carries_the_pool_columns(self):
        # The run_main rig has no bo_events.log / gem_owners.tsv -> pool is
        # None -> all three columns are empty strings (B1013-: old shim runs).
        row, header, tsv_row, printed = run_main(LINE)
        for col in ("gem_pool_growth_mib", "gem_pool_last_create_s",
                    "gem_pool_creates_steady_window"):
            self.assertEqual(row[col], "")
            self.assertEqual(tsv_row[header.index(col)], "")
        # Same TSV, plus the two fixture inputs -> the real numbers.
        row, header, tsv_row, printed = self._run_with_pool_fixture()
        self.assertEqual(row["gem_pool_growth_mib"], 7.91)
        self.assertEqual(row["gem_pool_last_create_s"], 10.744)
        self.assertEqual(row["gem_pool_creates_steady_window"], 0)
        for col, want in (("gem_pool_growth_mib", "7.91"),
                          ("gem_pool_last_create_s", "10.744"),
                          ("gem_pool_creates_steady_window", "0")):
            self.assertEqual(tsv_row[header.index(col)], want)

    def _run_with_pool_fixture(self):
        import shutil
        fixture = Path(__file__).resolve().parent.parent.parent / "tests" / "data" / "b1013"
        lines = ([LINE] if LINE else []) + [
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
            shutil.copy(fixture / "qeynos2_default_bo_events.log",
                        path / "bo_events.log")
            shutil.copy(fixture / "qeynos2_default_gem_owners_ready_steady.tsv",
                        path / "gem_owners.tsv")
            args = ["summary", "--run", root, "--zone", "qeynos2",
                    "--attempt", "test", "--tsv", str(path / "results.tsv")]
            with patch.object(sys, "argv", args), \
                    patch.object(summary, "frame_check", return_value=(1, 1, [])), \
                    patch.object(summary, "mesa_proof", return_value=([], "n", "ok")), \
                    contextlib.redirect_stdout(io.StringIO()) as out:
                summary.main()
            row = json.loads((path / "details.json").read_text())["row"]
            tsv = (path / "results.tsv").read_text().splitlines()
            return row, tsv[0].split("\t"), tsv[1].split("\t"), out.getvalue()


if __name__ == "__main__":
    unittest.main(verbosity=2)
