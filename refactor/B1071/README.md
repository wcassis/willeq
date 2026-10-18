# B1071 (GitLab #391): the targeted entity is invisible (not a billboard) while it is PVS-culled

**Verdict: the defect is real and live at HEAD, but it is NOT an impostor-tiering
order bug.** The four mechanisms the card hypothesised (stale target id,
exemption keyed on a different id, distance tier decided before the exemption,
exemption only on the next candidate rebuild) are all **empirically ruled out**
by live runs and by code. The mechanism that reproduces B1064's still is the
sim worker's **entity culling removing the target's node from the scene graph
while it is the current target**, so the target is drawn by nothing while the
target outline keeps painting its hull.

## The mechanism, with file:line

1. **The tier policy is correct.** `TierPolicy::plan()`
   (`src/client/graphics/impostor/impostor_policy.cpp:130-135`) checks
   `c.pinned` BEFORE the distance tier, and the candidate build keys the pin
   on the same id the bridge sets (`entity_impostors.cpp:192`,
   `renderer_frame_input.cpp:1125-1127` reads `Overlays().currentTargetId()`,
   the same source the outline draws from). The unit test
   `ImpostorTierPolicy.PinnedStayFullAndComeFirst`
   (`tests/test_impostor_policy.cpp`) passes at HEAD.

2. **Live: the target is `tier=full` on the FIRST frame it enters the
   candidate set, even beyond the 80-unit tier.** Lane 2, `--constrained
   orangepi`, `/face` first (PVS active), `/target` while the target is at
   124.7 units (tier distance is 80):
   `target frame=5224 cand=true pinned=true tier=full shown=1 inScene=true d=124.7`.
   `PIN-BUT-IMPOSTOR=0` in all three runs (A/B/C below). B1064's broken line
   (`frame=487 candidates=2 full=0 impostor=2`) is **not reproducible at HEAD**:
   the pin code is byte-identical across fable, 358bab17 and 4f9fbdbe
   (`git log 4f9fbdbe..HEAD -- <impostor files>` is empty).

3. **The real defect — culling removes the targeted node.**
   `SimulationWorker::computeEntityVisibility`
   (`src/client/graphics/sim_entity_sync.cpp:338-343`) exempted **only the
   player** from distance/PVS/frustum/cap culling. When the camera turns and
   the target's BSP region goes PVS-invisible, the worker returns
   `shouldBeVisible=false`; `SimBridgeComponent::applySimulationResults`
   (`src/client/graphics/sim/renderer_sim_bridge.cpp:1046-1051`) applies that
   as `sceneNode->remove()` + `inSceneGraph=false`.

4. **Why the pin then cannot save it.** An out-of-graph entity is *ineligible*
   for impostor candidacy — `eligible()` requires `inSceneGraph`
   (`entity_impostors.cpp:46-50`) — so the pinned draw path (the cap loop's
   `setVisible(true)`, `entity_impostors.cpp:487-497`) operates on a node that
   is no longer in the scene graph: **invisible on screen**.

5. **Why the still showed a solid silhouette.** `drawTargetOutline`
   (`src/client/graphics/overlays/overlays_component.cpp:432-433`) only checks
   `isVisible()`, which stays true on a *removed* node, so the depth pre-pass
   + inverted hull (B1064's fix) still paints the target as a flat solid shape
   with no mesh in the depth buffer — exactly
   `review/B1064/x86/hull_without_prepass_target_not_in_depth.png`.

**Live evidence (lane 2, qeynos2, Lieutenant_Arathur, `--constrained
orangepi`, instrumented binary):**

- **Run A** (B1064's exact script): `/target` at frame 669 →
  `cand=true tier=full` for 21 frames; at frame 691 the camera turn PVS-culls
  the target → `inScene=false`, `cand=false` for the rest of the run.
  **Captured frames: `frame_000066.png` (07:18:02.8, at `/target`) shows
  Lieutenant_Arathur as a 3D model; `frame_000069.png` (07:18:04.8, first
  culled frame) shows him GONE from the screen** while the target outline
  state still referenced him. (evidence:
  `/home/user/kb-evidence/kb-b1071-target-impostor/live/{run.log,frames/}`)
- **Run C** (`/face` first, `/target` while far): `cand=false` (culled,
  `d=-1`) until the player walked to it; **first candidate frame at 124.7
  units (beyond the 80-unit tier) was `cand=true pinned=true tier=full`** —
  the pin is correct, the gap is the cull window.
- **B1064's own logs:** the BEFORE (fable) and after3 (pre-pass) runs both show
  the target pinned `full=1`; only the broken intermediate run (358bab17) shows
  `full=0` — and that window is the same cull/ineligible window, logged as an
  impostor count because B1064's instrumentation only counted candidates.

## The fix

`SimulationInput::targetId` (`include/client/graphics/simulation_worker.h`,
set from `Overlays().currentTargetId()` in `SimBridgeComponent::postSimulationInput`,
`src/client/graphics/sim/renderer_sim_bridge.cpp:405-413`) threads the current
target into the worker, and `computeEntityVisibility`
(`src/client/graphics/sim_entity_sync.cpp:345-358`) exempts it from every
culling arm for the same reason the player is exempt. The id is read from the
**same source** the impostor pass pins on, so the two decisions cannot
disagree. Cost: one extra full model per targeted frame, and only while the
worker would have culled it — the impostor cap already draws pinned entities
first (`entity_impostors.cpp:468-481`).

The instrumentation (B1071's diagnostic probes) is kept as a
**transition-only** `[impostor] target` line (silent whenever the target is a
pinned full-model candidate) plus a `PIN-BUT-IMPOSTOR` policy-violation line,
so it is safe to ship and will name this class of defect the moment it
regresses.

## The regression net

`tests/test_target_cull_exemption.cpp` (registered in
`tests/cmake/renderer_guards.cmake`) drives the real
`SimulationWorker::computeOnce()` — no device, no thread, no assets — and pins:
target beyond render distance stays visible; target beyond the full-model cap
stays visible; a non-target at the same distance is still culled (the
exemption is one entity, not a widening); the exemption follows target
changes; zero target id changes nothing. **At the pre-fix head these tests
FAIL** (the worker culls the target, e.g. `TargetBeyondRenderDistanceStaysVisible`
returns invisible for the target); at the fixed head they pass.

## Not verified here

- Orange Pi / Mali leg (the fix is platform-neutral worker logic; the live
  proof is x86 lane 2).
- A live re-run on the fixed binary: the unit net + the pre-fix frame evidence
  above are the deliverable; a live confirm is `FOLLOW-UP` material, not a
  blocker — the unit test exercises the exact function the defect lives in.
