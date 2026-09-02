# J22 — Lift the zone config table out of detail_manager.cpp (one cut clears the file)

**Slot: slot3. Branch `task/J22-detail-manager-split` cut from `fable`.
x86 only. No Pi. No eqemu.**

Wave 6, and the cheapest unit in the batch: `src/client/graphics/detail/detail_manager.cpp`
is **1,754 lines** and **`DetailManager::createDefaultConfig()` at `:1203` is a
~548-line data table** — a per-zone list of foliage/detail settings. Lifting that
one method into its own TU clears the file in a single cut, with no cluster
analysis needed.

**MOVE-ONLY.** The method moves verbatim, signature unchanged, still a
`DetailManager` member — this is a file move, **not** a conversion to a free
function, a data file, or a lookup table. **No method decomposition** (that is
Batch G's shape and is out of scope for every Batch J unit).

## The cut

| range | goes to | ≈ lines |
|---|---|---|
| `createDefaultConfig` (from its leading banner/comment through its closing brace, around `1203`-`1751`) | **`src/client/graphics/detail/detail_zone_configs.cpp`** (new) | ~600 incl. preamble |
| everything else | **stays** `detail_manager.cpp` | ~1,210 |

⚠ **Re-derive both boundaries at your tip** — twenty-three units merged today
and any line number here is indicative only. Confirm the real end of the method
rather than trusting `1751`.

⚠ **~1,210 lines still leaves the stays-file over the 1,500 soft limit? No — it
does not, but CHECK.** If your measured split leaves either file over 1,500, say
so and propose the second cut rather than declaring victory; a split that leaves
an oversize file is not done.

## Standing rules — each has caught a unit today

- **`python3 scripts/refactor/verify_move_only.py "$(git merge-base fable HEAD)..HEAD" --repo /mnt/projects/fable/wt/slot3`**, run **after committing**. Three ways it lies: no `--repo` (verifies `main/`), wrong base rev (shows another unit's work as removals), uncommitted tree (prints "No C++ files changed", **exits 0**).
- **Correction 21:** confirm every file-local helper the method uses is in the SAME range as its consumer. `createDefaultConfig` is a data table and may reference file-scope constants — **if it does, they must travel with it or be promoted**; J08's plan split a helper from its only caller and would not have compiled. Paste the grep.
- **Correction 26:** `grep -rn` across `tests/` for add-sites — **never name `tests/CMakeLists.txt`**, which J20 split into `tests/cmake/*.cmake` fragments. The root site is `CMakeLists.txt:548` (verify; CMake commands here are **UPPERCASE**, so `grep -i`).
- Run `grep -rn 'detail_manager' tests/ scripts/` before cutting — a test that reads a source file as *text* joins your territory (J11 and J13 each found one; one would have silently asserted nothing).
- Cut at a blank line whose predecessor closes a method; verify comment depth 0.
- **Both proofs**: the verifier, plus a byte-identical re-concatenation / line-multiset check.
- After a build that **exits 143** (different from a slow build the tool backgrounded), scan every binary for valid **ELF magic — not size**: today's truncated binaries were 21–377 MB with all-zero headers, four separate instances.
- A visual-gate failure on the **CPU p95 metric with zero image mismatches** is wave contention — re-run it. ⚠ **This file drives foliage rendering, so a genuine image mismatch is a real risk here** — if pixels move, STOP and report rather than re-blessing.
- `chmod 644` created files. Stale `tests/` line-number citations are pre-existing doc rot owned by J21 — note and move on.
- The scratchpad is shared across slots: `rm -f` your exit file before launching, and a `wait_for.sh` MET after 0 seconds is a stale file, not a fast build.

## Acceptance

Build by exit code · `ctest -j6` (**NOT -j24**) with zero new failures vs
`refactor/test_baseline.txt` · both proofs · **visual gate PASS with goldens
untouched** · `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` ·
`check_file_sizes.sh --files` **0 warn, 0 fail** on both files.

## Territory

`src/client/graphics/detail/`, `CMakeLists.txt`, this file. Concurrent: slot1
J13 (`eq_bridge.cpp` + a 3-line repoint in `tests/test_reachability_sweep.cpp`)
and slot2 T323 (the CRC flag, live server).
