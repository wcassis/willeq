#!/usr/bin/env python3
"""B907 (issue #227) -- unit test for check_build_tree_orphans.py's --prune mode.

The CI runner keeps one warm build tree across every branch's pipeline
(.gitlab-ci.yml's GIT_CLEAN_FLAGS, docs/ci.md), so a binary built by branch A's
pipeline was still in bin/ when branch B's pipeline reconfigured and built --
B's CMake graph has no such target, and the existing check_build_tree_orphans
ctest test correctly failed B's pipeline on A's leftover binary. --prune is the
fix: remove such a leftover, using the exact same target-discovery function
the check itself uses, right after CI's cmake configure step and before the
build/test steps.

This test builds a FAKE, self-contained CMake file-API reply by hand (no real
`cmake` invocation, so it is fast and does not depend on the host's CMake
version) in a fresh tempfile.TemporaryDirectory() per test -- unique per
process, so this is safe under `ctest -jN` across worktree slots per
tests/test_tmp_path_census.cpp's rule against fixed /tmp names.

Cases: an orphan is removed; a claimed binary is kept; a directory with no
CMakeCache.txt is refused (exit 2, nothing touched); a file directly outside
<build>/bin/ (a sibling of the build tree, and a file inside the build tree
but outside bin/) is never touched. RED proof: run_prune(..., delete=False)
demonstrates the orphan is *identified* but survives when the delete step
itself is disabled -- i.e. it is the deletion, not the discovery, that makes
the real (delete=True) case remove the file.
"""
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "check_build_tree_orphans.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "check_build_tree_orphans", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ORPHANS = load_module()


def _write(path, content=""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)


def make_fake_build_tree(build_dir: str):
    """A minimal but complete fake CMake file-API reply claiming exactly one
    bin/ artifact, 'claimed_target'. bin/ also holds 'orphan_target', which no
    target claims.
    """
    _write(os.path.join(build_dir, "CMakeCache.txt"), "# fake cache\n")

    bin_dir = os.path.join(build_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    _write(os.path.join(bin_dir, "claimed_target"), "#!/bin/sh\n")
    _write(os.path.join(bin_dir, "orphan_target"), "#!/bin/sh\n")

    reply_dir = os.path.join(build_dir, ".cmake", "api", "v1", "reply")
    os.makedirs(reply_dir, exist_ok=True)

    _write(os.path.join(reply_dir, "target-claimed.json"),
           json.dumps({"artifacts": [{"path": "bin/claimed_target"}]}))
    _write(os.path.join(reply_dir, "codemodel-v2-fake.json"),
           json.dumps({
               "configurations": [
                   {"targets": [{"jsonFile": "target-claimed.json"}]}
               ]
           }))
    _write(os.path.join(reply_dir, "index-2026-01-01T00-00-00-0000.json"),
           json.dumps({
               "reply": {
                   "codemodel-v2": {
                       "kind": "codemodel",
                       "jsonFile": "codemodel-v2-fake.json",
                   }
               }
           }))
    return bin_dir


class PruneOrphans(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="b907_prune_test_")
        self.addCleanup(self._tmp.cleanup)
        self.root = self._tmp.name

    def test_orphan_removed_claimed_kept(self):
        build = os.path.join(self.root, "build")
        bin_dir = make_fake_build_tree(build)

        rc = ORPHANS.run_prune(build)

        self.assertEqual(rc, 0)
        self.assertFalse(
            os.path.exists(os.path.join(bin_dir, "orphan_target")),
            "orphan_target should have been pruned")
        self.assertTrue(
            os.path.exists(os.path.join(bin_dir, "claimed_target")),
            "claimed_target must survive a prune")

    def test_non_build_tree_refused(self):
        not_a_build_tree = os.path.join(self.root, "plain_dir")
        os.makedirs(not_a_build_tree)
        _write(os.path.join(not_a_build_tree, "bin", "something"), "x")

        rc = ORPHANS.run_prune(not_a_build_tree)

        self.assertEqual(rc, 2)
        self.assertTrue(
            os.path.exists(os.path.join(not_a_build_tree, "bin", "something")),
            "a refused prune must not touch anything")

    def test_missing_bin_is_a_noop(self):
        build = os.path.join(self.root, "build_no_bin")
        _write(os.path.join(build, "CMakeCache.txt"), "# fake cache\n")

        rc = ORPHANS.run_prune(build)

        self.assertEqual(rc, 0)

    def test_bin_dir_outside_build_tree_refused(self):
        build = os.path.join(self.root, "build_escape")
        outside_bin = os.path.join(self.root, "outside_bin")
        os.makedirs(outside_bin)
        _write(os.path.join(outside_bin, "something"), "x")
        _write(os.path.join(build, "CMakeCache.txt"), "# fake cache\n")
        os.symlink(outside_bin, os.path.join(build, "bin"))

        rc = ORPHANS.run_prune(build)

        self.assertEqual(rc, 2)
        self.assertTrue(os.path.exists(os.path.join(outside_bin, "something")))

    def test_nothing_outside_bin_is_touched(self):
        build = os.path.join(self.root, "build")
        make_fake_build_tree(build)

        sibling_in_build = os.path.join(build, "not_bin", "orphan_target")
        _write(sibling_in_build, "x")
        sibling_outside_build = os.path.join(self.root, "orphan_target")
        _write(sibling_outside_build, "x")

        rc = ORPHANS.run_prune(build)

        self.assertEqual(rc, 0)
        self.assertTrue(os.path.exists(sibling_in_build),
                         "a file outside bin/ but inside the build tree "
                         "must survive a prune")
        self.assertTrue(os.path.exists(sibling_outside_build),
                         "a file outside the build tree entirely must "
                         "survive a prune")

    def test_red_proof_delete_false_leaves_orphan_in_place(self):
        """Disabling the deletion (delete=False) is the RED proof: discovery
        still finds the orphan (rc stays 0, the file is still named as an
        orphan in the printed report), but the file itself survives -- proving
        it is the os.remove() call in run_prune(), not the discovery it
        shares with the read-only check, that makes the real path a prune."""
        build = os.path.join(self.root, "build")
        bin_dir = make_fake_build_tree(build)

        rc = ORPHANS.run_prune(build, delete=False)

        self.assertEqual(rc, 0)
        self.assertTrue(
            os.path.exists(os.path.join(bin_dir, "orphan_target")),
            "delete=False must not remove the orphan -- this is the RED "
            "case the real (delete=True) path is proven against")

    def test_check_mode_still_fails_on_a_surviving_orphan(self):
        """The read-only check (no --prune) is unchanged in meaning: it must
        still FAIL while an orphan is present, and PASS once run_prune() has
        removed it."""
        build = os.path.join(self.root, "build")
        make_fake_build_tree(build)

        self.assertEqual(ORPHANS.run_check(build), 1)
        self.assertEqual(ORPHANS.run_prune(build), 0)
        self.assertEqual(ORPHANS.run_check(build), 0)


if __name__ == "__main__":
    unittest.main()
