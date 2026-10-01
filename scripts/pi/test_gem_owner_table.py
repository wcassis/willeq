#!/usr/bin/env python3
"""B962 (GitLab #280): ctest GemOwnerTable -- the per-owner fold of a GEM census
dump (scripts/pi/gem_owner_table.py) over dumps CAPTURED on the Orange Pi
(tests/data/b962/: qeynos2 at steady_60s, the client run under
scripts/pi/lima_bo_census.c, with and without WILLEQ_GPU_BUFFERS_LEGACY=1).

What it pins:
  * every live BO lands on exactly ONE owner row and the rows sum to the dump's
    own total (a block shared by several owners is never counted twice);
  * the regular-BO sum reconciles with the client's [memgpu] gpu_buffers row
    (its memory cgroup's shmem) within RESIDUAL_BOUND -- the heap pages the GP
    grew and the rig's frame dumps are all that may sit outside the BOs;
  * the depth/stencil owner equals the client's own logical_depth_stencil row;
  * the B962 texture defect and its fix as captured: before, every 256x256
    EQE1 texture held TWO buffers (level 0 alone plus a second allocation);
    after, one per texture, and the texture rows sit within TEXTURE_ROUNDING
    of the client's storage tally (logical_textures).
"""
import os
import sys
import unittest
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gem_owner_table as g  # noqa: E402

DATA = os.path.join(HERE, "..", "..", "tests", "data", "b962")
RESIDUAL_BOUND = 4 * 1048576      # heap pages + frame dumps, measured 1.6-3.0 MiB
TEXTURE_ROUNDING = 1.10           # page/tile/level rounding over the storage tally


def load(stem):
    points, _ = g.read_dump(os.path.join(DATA, stem + "_gem_owners.tsv"))
    memgpu = g.read_memgpu(os.path.join(DATA, stem + "_memgpu.txt"))
    return points["steady_60s"], memgpu["steady_60s"]


class GemOwnerTableTest(unittest.TestCase):
    def check_partition(self, stem):
        block, memgpu = load(stem)
        t = g.fold(block, memgpu)
        regular = sum(b["size"] for b in block["bos"] if not b["heap"])
        self.assertEqual(sum(r["bytes"] for r in t["rows"].values()), regular)
        self.assertEqual(t["regular_bytes"], regular)
        self.assertEqual(sum(r["count"] for r in t["rows"].values()), len(block["bos"]))
        self.assertEqual(len(block["bos"]), block["meta"]["live"])
        self.assertEqual(sum(b["size"] for b in block["bos"]), block["meta"]["bytes"])
        self.assertEqual(t["residual_bytes"], memgpu["gpu_buffers"] - regular)
        self.assertGreaterEqual(t["residual_bytes"], 0)
        self.assertLessEqual(t["residual_bytes"], RESIDUAL_BOUND)
        return block, t

    def test_fixed_partition_and_reconciliation(self):
        block, t = self.check_partition("qeynos2_steady")
        depth = t["rows"]["egl: window surface depth/stencil"]
        self.assertEqual(depth["count"], 1)
        with open(os.path.join(DATA, "qeynos2_steady_memgpu.txt")) as f:
            line = f.read()
        self.assertIn("logical_depth_stencil=%d" % depth["bytes"], line)
        tex = sum(r["bytes"] for k, r in t["rows"].items() if k.startswith("textures:"))
        self.assertGreaterEqual(tex, t["logical_textures"])
        self.assertLessEqual(tex, t["logical_textures"] * TEXTURE_ROUNDING)
        self.assertNotIn("lima: upload-thread context (PLB, streams)", t["rows"])

    def test_legacy_partition(self):
        self.check_partition("qeynos2_steady_legacy")

    def test_one_buffer_per_texture_after_two_before(self):
        def per_name(stem):
            block, _ = load(stem)
            c = Counter(b["tag"].split("|")[-1] for b in block["bos"]
                        if b["tag"].split("|")[-1].startswith(("tex:etc1 ", "tex:etc1a ")))
            return c
        before, after = per_name("qeynos2_steady_legacy"), per_name("qeynos2_steady")
        self.assertTrue(before and after)
        self.assertGreater(sum(1 for n in before.values() if n >= 2), len(before) // 2)
        self.assertEqual(max(after.values()), 1)


if __name__ == "__main__":
    unittest.main()
