# T292 — remove protectedRegions_ and the dead progressive paths (Will's ruling 2026-08-26)

Slot: slot1. x86. NO eqemu. Class A. Territory:
`src/client/graphics/renderer_zone_stream.cpp` (Priority 3/4 progressive
paths, ~:492 and ~:543), `renderer_zone_lifecycle.cpp` (~:200),
`renderer_sim_bridge.cpp` (~:690), the `protectedRegions_` member and its
two dead counters, tests. Do NOT touch the loot panel (T290 merged) or the
gate metric (T291 slot3).

## The ruling — and its condition
Will, 2026-08-26: **"For protectedRegions_ and the progressive path, if this
was for progressive terrain loading, it can be removed."**
**VERIFY THE CONDITION FIRST.** T79's census says the bitvector has been
permanently all-false since T67 deleted `ConstrainedMeshCache`. Establish
from the code and history what the Priority 3/4 paths were actually FOR:
- If they are progressive terrain/zone-geometry loading (the expected
  answer, given T67's cache removal): **remove them**, with the grep proof
  that nothing else reads the signal.
- If they turn out to serve something else (door culling? PVS?), **STOP and
  report** — the ruling was explicitly conditional, and `getDoorsInRegions`
  at :503 takes `protectedRegions_` as an argument, so check what that call
  degrades to when every bit is false and whether removing it changes door
  behaviour at all.

## Scope
1. Establish the purpose with evidence (T67's commit, T79's census, the
   comments at the four sites).
2. Remove the member, the two dead counters, and the Priority 3/4 branches
   that only exist to consume it. Keep anything with a live consumer.
3. Prove the removal is behaviour-neutral: the branches were dead because
   the signal was all-false, so the kept path is the one that already ran.
   A test or a before/after frame that shows identical output.
4. Line-count delta in the Review.

## Verification
Full ctest (`-j6`); **visual gate PASS with goldens untouched** — this is
renderer code, so a golden change would mean the removal was NOT
behaviour-neutral: STOP and report rather than re-bless. Review; evidence
review/T292/. Commit: `T292: protectedRegions_ and the dead progressive paths removed`.

---

# Review (executor, slot1, 2026-08-26)

## 1. The condition — VERIFIED, it was progressive terrain/asset loading

Will's ruling was conditional. It holds. Established from three
independent sources, not from T79's summary alone:

**a. The producer's own comment and its loop.**
`sim_visibility.cpp::calculateRegions()` fills
`SimulationOutput::protectedRegions` in the same loop, under the same
PVS + render-distance test, as `meshLoadQueue`:

    // Queue for lazy mesh loading (PVS + distance only ...)
    output.meshLoadQueue.push_back({rb.regionIdx, distSq});
    // Track for mesh cache protection (all PVS+distance regions)
    output.protectedRegions.push_back(rb.regionIdx);

It is the "do not evict these" set for the lazy region-mesh loader — the
region-streaming half of progressive terrain loading. It is a pure
duplicate of `meshLoadQueue`'s region indices.

**b. The deleted writer.** `git show 30e04d3b -- src/client/graphics/renderer_sim_bridge.cpp`
(T67) shows the only code that ever set a bit true, inside
`if (constrainedMeshCache_) { ... }` — a `std::fill(..., false)` followed
by setting each index from `results->protectedRegions`. The block also
copied `meshLoadQueue`. Cache and streaming were one mechanism. The cache
pointer was never constructed in any shipped build, so the bitvector was
already permanently all-false before T67 deleted the writer.

**c. The consumers are all zone-asset build paths, and nothing else.**
Full census (matches T79's, re-verified by reading the code):

| Site | What it did | Live? |
|---|---|---|
| `renderer_zone_load.cpp:421` | `.resize(n, false)` | writer, never sets true |
| `renderer_zone_lifecycle.cpp:202` | `.clear()` | writer, no-op |
| `renderer_zone_stream.cpp:503` | Priority 3 progressive **door** build | body unreachable |
| `renderer_zone_stream.cpp:551,561` | Priority 4 progressive **PVS object** build | candidate set always empty |
| `renderer_zone_stream.cpp:826,836` | `unbuiltDoors` / `unbuiltObjects` progressive-load counters | pinned to 0 |
| `renderer_sim_bridge.cpp:665` | `results->protectedRegions.size()` in a DEBUG log | last consumer of the producer |

**Nothing else consults it.** Specifically checked and found NOT to
depend on it: PVS culling (`regionVisible`, `portalVisibleRegions_`,
`pvsVisibleBoundsIndices` — separate signals), door *behaviour*
(open/close/collision, `DoorManager::doors_`), and the eager door/object
build at zone load, which goes through `getUnbuiltDoors()` /
`renderer_zone_load_objects.cpp` and never looks at regions.

**What `getDoorsInRegions()` degrades to with an all-false vector** (the
plan's explicit question): an unconditional empty result. Its only filter
is `!visual.meshBuilt && visual.bspRegion < regions.size() && regions[visual.bspRegion]`.
Note the vector was *sized* correctly (resized to the BSP region count at
zone install) — so this was not a size-0 short circuit; every element was
simply false. Removing the call changes nothing observable. With both
call sites gone the method had no callers left and was removed too. The
similarly-named `getUnbuiltDoors()` is a different method on the live
eager path and is untouched.

Conclusion: the paths served progressive terrain/zone-asset loading and
nothing else. Will's condition is satisfied; removal proceeded.

## 2. What was removed

- `IrrlichtRenderer::protectedRegions_` (member + its `.resize()` and
  `.clear()` writers).
- `renderer_zone_stream.cpp` Priority 3 (progressive door build) and
  Priority 4 (progressive PVS-object build) blocks in
  `processFrameProgressiveLoad()`.
- The `unbuiltDoors` / `unbuiltObjects` counters in
  `checkProgressiveLoadingComplete()`, their two log fields, and their
  terms in the completion test.
- `SimulationOutput::protectedRegions` and its producer lines in
  `sim_visibility.cpp` — its last consumer was the sim-bridge log line
  removed here. (T79 flagged this sibling and left it; with the log field
  gone it had zero consumers, so keeping it would have been dead weight.)
- `DoorManager::getDoorsInRegions()` — no callers left.

Each site keeps a short comment naming T292 and why the code could not
execute, so the next reader does not re-derive this.

## 3. Behaviour-neutrality

Structural argument: every removed branch was guarded, directly or
transitively, on a `protectedRegions_[i]` read that was permanently
false. Neither Priority 3 nor Priority 4 could execute its body or set
`didWork`, so the priorities after them (none — Priority 5 is already
disabled, `queueEntityPrepRequests()` is unconditional) see an unchanged
`didWork`. The completion test
`unbuiltEntities == 0 && unbuiltDoors == 0 && unbuiltObjects == 0`
reduces to `unbuiltEntities == 0`, which is what it has always evaluated
to since T67.

Empirical proof: **visual gate PASS with goldens untouched**, three zones
(qeynos2, gfaydark, befallen) rendered and compared pixel-wise against
goldens committed before this change. `git status --porcelain` lists only
the nine source files — no golden was modified. Per the plan, a golden
move would have been a STOP; none occurred.

## 4. Line-count delta

    9 files changed, 38 insertions(+), 162 deletions(-)   = net -124

The 38 insertions are entirely replacement comments; no logic was added.
`renderer_zone_stream.cpp` carries the bulk (-142/+ comments), and drops
from 991 to 873 lines.

## 5. Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` (GLES2 tree) | clean |
| `ctest --test-dir build -j6` | **3203/3204**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `bash scripts/build-gles2-x86.sh` | clean, both binaries produced |
| `bash scripts/ci/visual_gate.sh` | `=== VISUAL GATE: PASS ===`, **goldens untouched** |
| `bash scripts/build-arm-noble.sh` | clean, all ARM binaries produced |
| `bash scripts/refactor/check_file_sizes.sh fable` | 8 ok + 1 pre-existing WARN across this unit's 9 files; **no new FAIL** |

Evidence: `/mnt/projects/fable/review/T292/` (README.md,
`visual_gate_after.txt`, `grep_after_protectedRegions.txt`,
`T292_diff.patch`, `diffstat.txt`, `ctest_tail.txt`).

## 6. Deviations and notes

- **Scope extended by two items beyond the plan's literal list**, both
  because the removal orphaned them and leaving them would have shipped
  new dead code: `SimulationOutput::protectedRegions` (+ its
  `sim_visibility.cpp` producer) and `DoorManager::getDoorsInRegions()`.
  Both had exactly one consumer, and that consumer was removed here.
  Flagged rather than silently absorbed.
- `check_file_sizes.sh fable` also reported a pre-existing
  `FAIL tools/hwcaps/gles2_zone_benchmark.cpp: 3037 lines`. That is **not
  this unit's** — `fable` advanced to `bb3905eb` (T291 merged) while this
  unit was in flight, so the script diffed T291's files in as well. This
  unit did not touch `tools/hwcaps/` (territory exclusion honoured); the
  branch is based on `38d49a4b`.
- `docs/decisions.md:511` (Will's ruling text) left as written — it is the
  decision record, not a code comment.
- The protocol's stated ctest baseline (2745/2746) is stale relative to
  the current tree; the suite is now 3204 tests, same single known failure.
