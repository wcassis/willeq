#!/usr/bin/env python3
"""Fail if any executable in build/bin has NO current CMake target producing it.

T175. On 2026-08-14 the build tree accumulated STALE binaries whose CMake
targets and source files had both been deleted -- test_gl_points,
test_eq_commands_attack, test_eq_commands_assist, test_eq_commands_vendor. They
still RAN and still PASSED, exercising code deleted a week earlier. They were
cleaned up by hand; nothing stopped a recurrence. This is that guard.

WHY IT MATTERS: cmake does not remove a target's output from bin/ when you
delete the target from CMakeLists -- that is exactly how a stale binary
survives a rebuild. A test binary nobody's CMakeLists claims is not "an old
test still passing", it is a binary pinned to a source tree that no longer
exists: it cannot be rebuilt, cannot fail on a real regression, and quietly
inflates the green count. This script makes the orphan set empty and keeps it
there.

HOW IT DECIDES what a target legitimately produces: the CMake file API
(codemodel-v2), which is CMake's own machine-readable account of the configured
build. Every EXECUTABLE (and any other) target records its on-disk artifacts;
we take every artifact whose path is under bin/ as "claimed". A regular file in
bin/ that is not claimed is an orphan. See the module docstring section
"WHY THE FILE API" for why this beats `--target help` and cache scraping.

WHY THE FILE API, not `cmake --build --target help` or CMakeCache.txt:
  * `--target help` lists target NAMES only, in a generator-specific format,
    mixed with utility/phony targets, and gives no artifact path -- it cannot
    tell you what file a target lands in bin/ (OUTPUT_NAME, per-config names).
  * CMakeCache.txt has no target list at all.
  * The file API is the authoritative, generator-independent, versioned
    contract; it maps each target to its exact artifact path(s). CMake
    regenerates the reply on every reconfigure, so it tracks the current
    CMakeLists whenever the tree is built -- the deleted-target case this guard
    exists for regenerates the reply WITHOUT that target, leaving its leftover
    binary correctly unclaimed.

AUTHORITY IS THE CURRENT CONFIGURATION. The claimed set is this build tree's
present configuration. A binary left behind from a since-disabled feature (e.g.
a graphics test after a reconfigure to -DEQT_GRAPHICS=OFF that dropped its
target) is reported -- by design: it is, at that point, a stale binary in the
tree, the same hygiene concern.

Exit 0 clean, 1 if bin/ holds an orphan, 125 to SKIP when there is no
configured build tree to inspect (matches this repo's SKIP_RETURN_CODE
convention, see visual_gate in tests/CMakeLists.txt), 2 on a usage or
environment error -- never a silent pass.
"""

import glob
import json
import os
import subprocess
import sys

# Binaries deliberately allowed in bin/ with no producing target. Empty is the
# intended steady state; an entry needs a reason and the unit that decided it.
ALLOWLIST = {}


def _find_build_tree(repo: str) -> str:
    """The configured build tree to inspect. argv[1] wins, else <repo>/build."""
    if len(sys.argv) > 1:
        return os.path.abspath(sys.argv[1])
    return os.path.join(repo, "build")


def _latest_codemodel(reply_dir: str):
    """Path to the codemodel reply the current index points at, or None."""
    indices = sorted(glob.glob(os.path.join(reply_dir, "index-*.json")))
    if not indices:
        return None
    index = json.load(open(indices[-1]))
    for entry in index.get("reply", {}).values():
        if isinstance(entry, dict) and entry.get("kind") == "codemodel":
            return os.path.join(reply_dir, entry["jsonFile"])
    return None


def main() -> int:
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    build = _find_build_tree(repo)

    # No configured build tree -> nothing to inspect. SKIP, do not fail: this
    # check is meaningless without a build, and ctest may run in a fresh tree.
    if not os.path.isfile(os.path.join(build, "CMakeCache.txt")):
        print(f"SKIP -- no configured build tree at {build} "
              f"(no CMakeCache.txt); nothing to inspect")
        return 125

    bin_dir = os.path.join(build, "bin")
    if not os.path.isdir(bin_dir):
        print(f"SKIP -- {bin_dir} does not exist; no binaries built yet")
        return 125

    # Ensure the file API codemodel query exists (idempotent). CMake writes the
    # matching reply on its next reconfigure; we bootstrap one below if absent.
    query = os.path.join(build, ".cmake", "api", "v1", "query", "codemodel-v2")
    os.makedirs(os.path.dirname(query), exist_ok=True)
    if not os.path.exists(query):
        open(query, "w").close()

    reply_dir = os.path.join(build, ".cmake", "api", "v1", "reply")
    codemodel = _latest_codemodel(reply_dir)
    if codemodel is None:
        # Tree was configured before the query existed. Reconfigure once (local,
        # offline, no compile) so CMake emits the reply, then read it.
        print(f"note: no file API reply yet; reconfiguring {build} to emit one")
        try:
            subprocess.run(["cmake", build], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            print(f"ERROR could not reconfigure to obtain file API reply: {exc}")
            return 2
        codemodel = _latest_codemodel(reply_dir)
    if codemodel is None:
        print("ERROR file API produced no codemodel reply -- nothing checked")
        return 2

    cm = json.load(open(codemodel))
    configs = cm.get("configurations", [])
    if not configs:
        print("ERROR codemodel has no configurations -- nothing checked")
        return 2

    # Every artifact any target lands under bin/ is "claimed". Basenames, so
    # OUTPUT_NAME / per-config names are honoured -- we trust CMake's own path.
    claimed = set()
    for cfg in configs:
        for t in cfg.get("targets", []):
            tj = json.load(open(os.path.join(reply_dir, t["jsonFile"])))
            for art in tj.get("artifacts", []):
                p = art.get("path", "")
                norm = p.replace("\\", "/")
                if norm.startswith("bin/"):
                    claimed.add(os.path.basename(norm))

    if not claimed:
        print("ERROR codemodel lists no bin/ artifacts -- refusing to call "
              "every binary an orphan; check the build configuration")
        return 2

    present = [f for f in os.listdir(bin_dir)
               if os.path.isfile(os.path.join(bin_dir, f))]
    orphans = sorted(f for f in present
                     if f not in claimed and f not in ALLOWLIST)

    print(f"checked {len(present)} file(s) in {bin_dir} against "
          f"{len(claimed)} target artifact(s); {len(ALLOWLIST)} allowlisted")
    if orphans:
        print("")
        print("FAIL -- these binaries in bin/ are produced by no current CMake "
              "target:")
        for o in orphans:
            print(f"  {o}")
        print("")
        print("A binary no CMakeLists claims is pinned to deleted sources: it "
              "cannot be rebuilt and cannot fail on a regression, yet it still")
        print("runs and passes. Delete the stale binary (a fresh `cmake "
              "--build` will not, cmake never prunes bin/), or -- if it is")
        print("produced deliberately outside the target graph -- add it to "
              "ALLOWLIST in this script with a reason and a unit number.")
        return 1

    print("PASS -- every binary in bin/ is produced by a current CMake target.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
