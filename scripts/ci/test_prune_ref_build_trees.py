#!/usr/bin/env python3
"""Regression net for scripts/ci/prune_ref_build_trees.py (B1012).

Fresh temp layout per test; asserts each decision rule, that --dry-run deletes
nothing, that a root outside <build|build-gles2>/ref is refused untouched, and
that an empty live-ref list is refused (a failed ls-remote must never read as
"every branch is gone").

B1026 (#346): the host+sha build claim. ``--verify`` passes only for the host
AND sha that wrote the claim; another host (fable pipeline 809: built on
gitlab-runner-1, tested on gitlab-runner-2), another sha, a ref-only stamp and
a missing stamp are all mismatches; and the prune still reads a claim stamp.
"""
import datetime as _dt
import io
import os
import socket
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prune_ref_build_trees as p  # noqa: E402

NOW = _dt.datetime(2026, 10, 9, 12, 0, 0, tzinfo=_dt.timezone.utc)


class PruneRefBuildTrees(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.join(self.tmp.name, "build", "ref")
        os.makedirs(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def tree(self, slug, ref=None, days_old=0.0, stamp=True):
        t = os.path.join(self.root, slug)
        os.makedirs(os.path.join(t, "bin"))
        with open(os.path.join(t, "bin", "willeq"), "wb") as f:
            f.write(b"x" * 1000)
        if stamp:
            p.write_stamp(t, ref or slug, NOW - _dt.timedelta(days=days_old))
        return t

    def run_prune(self, live, **kw):
        out = io.StringIO()
        deleted = p.prune([self.root], set(live), now=NOW, out=out, **kw)
        return deleted, out.getvalue()

    def test_live_fresh_tree_is_kept(self):
        t = self.tree("task-b1-x", "task/B1-x", days_old=2)
        deleted, log = self.run_prune({"task/B1-x", "fable"})
        self.assertEqual(deleted, [])
        self.assertTrue(os.path.isdir(t))
        self.assertIn("KEEP", log)

    def test_tree_of_a_deleted_ref_is_removed(self):
        t = self.tree("task-gone", "task/gone", days_old=1)
        deleted, log = self.run_prune({"fable"})
        self.assertEqual(deleted, [t])
        self.assertFalse(os.path.exists(t))
        self.assertIn("no longer on the remote", log)

    def test_stale_live_ref_is_removed_after_max_age(self):
        t = self.tree("task-old", "task/old", days_old=20)
        deleted, _ = self.run_prune({"task/old"}, max_age_days=14)
        self.assertEqual(deleted, [t])
        t2 = self.tree("task-old2", "task/old2", days_old=20)
        deleted, _ = self.run_prune({"task/old2"}, max_age_days=30)
        self.assertEqual(deleted, [])
        self.assertTrue(os.path.isdir(t2))

    def test_unstamped_tree_is_removed(self):
        t = self.tree("legacy", stamp=False)
        deleted, log = self.run_prune({"fable"})
        self.assertEqual(deleted, [t])
        self.assertIn("no stamp", log)

    def test_dry_run_deletes_nothing(self):
        t = self.tree("task-gone", "task/gone")
        deleted, log = self.run_prune({"fable"}, dry_run=True)
        self.assertEqual(deleted, [])
        self.assertTrue(os.path.isdir(t))
        self.assertIn("DELETE", log)  # the decision is still printed

    def test_files_and_symlinks_at_root_are_ignored(self):
        with open(os.path.join(self.root, "stray.txt"), "w") as f:
            f.write("x")
        os.symlink(self.tmp.name, os.path.join(self.root, "link"))
        deleted, _ = self.run_prune({"fable"})
        self.assertEqual(deleted, [])
        self.assertTrue(os.path.exists(os.path.join(self.root, "stray.txt")))
        self.assertTrue(os.path.islink(os.path.join(self.root, "link")))

    def test_foreign_root_is_refused(self):
        other = os.path.join(self.tmp.name, "src", "ref")
        os.makedirs(os.path.join(other, "x"))
        with self.assertRaises(SystemExit):
            p.prune([other], {"fable"}, now=NOW, out=io.StringIO())
        self.assertTrue(os.path.isdir(os.path.join(other, "x")))

    def test_empty_live_list_is_refused_by_main(self):
        self.tree("task-a", "task/a")
        refs = os.path.join(self.tmp.name, "refs.txt")
        open(refs, "w").close()
        with self.assertRaises(SystemExit):
            p.main(["--root", self.root, "--live-refs-file", refs])
        self.assertTrue(os.path.isdir(os.path.join(self.root, "task-a")))

    def test_legacy_root_sweep_keeps_ref_and_hygiene(self):
        build = os.path.join(self.tmp.name, "build")
        for d in ("bin", "CMakeFiles", "hygiene", "ref"):
            os.makedirs(os.path.join(build, d), exist_ok=True)
        open(os.path.join(build, "CMakeCache.txt"), "w").close()
        out = io.StringIO()
        removed = p.sweep_legacy_root(build, out=out)
        self.assertEqual(sorted(os.path.basename(x) for x in removed),
                         ["CMakeCache.txt", "CMakeFiles", "bin"])
        self.assertEqual(sorted(os.listdir(build)), ["hygiene", "ref"])
        self.assertIn("pre-B1012", out.getvalue())

    def test_legacy_sweep_refuses_a_foreign_root(self):
        other = os.path.join(self.tmp.name, "src")
        os.makedirs(os.path.join(other, "x"))
        with self.assertRaises(SystemExit):
            p.sweep_legacy_root(other, out=io.StringIO())
        self.assertTrue(os.path.isdir(os.path.join(other, "x")))

    def test_stamp_mode_via_main(self):
        t = os.path.join(self.root, "via-main")
        rc = p.main(["--stamp", t, "--ref", "task/B1012-x"])
        self.assertEqual(rc, 0)
        ref, when = p.read_stamp(os.path.join(t, p.STAMP))
        self.assertEqual(ref, "task/B1012-x")
        self.assertIsNotNone(when)

    def test_stamp_round_trips(self):
        t = os.path.join(self.root, "s")
        p.write_stamp(t, "task/B1012-x", NOW)
        ref, when = p.read_stamp(os.path.join(t, p.STAMP))
        self.assertEqual(ref, "task/B1012-x")
        self.assertEqual(when, NOW)


class BuildClaimStamp(unittest.TestCase):
    """B1026: the host+sha claim that makes a split pipeline safe."""

    SHA = "0136137b" + "0" * 32
    OTHER_SHA = "84405cef" + "0" * 32

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = os.path.join(self.tmp.name, "build", "ref", "fable")

    def tearDown(self):
        self.tmp.cleanup()

    def verify(self, host, sha):
        out = io.StringIO()
        ok = p.verify_stamp(self.t, host, sha, out=out)
        return ok, out.getvalue()

    def test_same_host_same_sha_matches(self):
        p.write_stamp(self.t, "fable", NOW, host="gitlab-runner-1", sha=self.SHA)
        ok, log = self.verify("gitlab-runner-1", self.SHA)
        self.assertTrue(ok)
        self.assertIn("stamp MATCH", log)

    def test_other_host_is_a_mismatch_naming_both(self):
        # pl809: build job 4006 on runner-1, test job 4007 on runner-2.
        p.write_stamp(self.t, "fable", NOW, host="gitlab-runner-1", sha=self.SHA)
        ok, log = self.verify("gitlab-runner-2", self.SHA)
        self.assertFalse(ok)
        self.assertIn("stamp MISMATCH", log)
        self.assertIn("stored host=gitlab-runner-1", log)
        self.assertIn("expected host=gitlab-runner-2", log)

    def test_other_sha_is_a_mismatch(self):
        p.write_stamp(self.t, "fable", NOW, host="gitlab-runner-2", sha=self.OTHER_SHA)
        ok, log = self.verify("gitlab-runner-2", self.SHA)
        self.assertFalse(ok)
        self.assertIn(f"stored host=gitlab-runner-2 sha={self.OTHER_SHA}", log)
        self.assertIn(f"expected host=gitlab-runner-2 sha={self.SHA}", log)

    def test_missing_stamp_is_a_mismatch(self):
        ok, log = self.verify("gitlab-runner-1", self.SHA)
        self.assertFalse(ok)
        self.assertIn("no stamp", log)
        os.makedirs(self.t)
        ok, _ = self.verify("gitlab-runner-1", self.SHA)
        self.assertFalse(ok)

    def test_ref_only_stamp_claims_no_build(self):
        # The pre-configure stamp wipes an earlier claim, so a build that then
        # fails leaves a tree nothing trusts.
        p.write_stamp(self.t, "fable", NOW, host="gitlab-runner-1", sha=self.SHA)
        p.write_stamp(self.t, "fable", NOW)
        ok, log = self.verify("gitlab-runner-1", self.SHA)
        self.assertFalse(ok)
        self.assertIn("ref-only stamp", log)

    def test_prune_still_reads_a_claim_stamp(self):
        p.write_stamp(self.t, "task/B1026-x", NOW, host="h", sha=self.SHA)
        ref, when = p.read_stamp(os.path.join(self.t, p.STAMP))
        self.assertEqual(ref, "task/B1026-x")
        self.assertEqual(when, NOW)
        action, _ = p.decide(self.t, {"task/B1026-x"}, NOW, 14)
        self.assertEqual(action, "keep")

    def test_main_stamp_then_verify_exit_codes(self):
        rc = p.main(["--stamp", self.t, "--ref", "fable", "--host", "gitlab-runner-1", "--sha", self.SHA])
        self.assertEqual(rc, 0)
        self.assertEqual(p.main(["--verify", self.t, "--host", "gitlab-runner-1", "--sha", self.SHA]), 0)
        self.assertEqual(p.main(["--verify", self.t, "--host", "gitlab-runner-2", "--sha", self.SHA]), 1)
        self.assertEqual(p.main(["--verify", self.t, "--host", "gitlab-runner-1", "--sha", self.OTHER_SHA]), 1)
        with self.assertRaises(SystemExit):
            p.main(["--verify", self.t, "--host", "gitlab-runner-1"])

    def test_main_stamp_defaults_host_to_this_machine(self):
        p.main(["--stamp", self.t, "--ref", "fable", "--sha", self.SHA])
        fields = p.read_stamp_fields(os.path.join(self.t, p.STAMP))
        self.assertEqual(fields["host"], socket.gethostname())
        self.assertEqual(fields["sha"], self.SHA)


if __name__ == "__main__":
    unittest.main()
