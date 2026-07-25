# P08c — Entity draw-path reduction: measure-and-select the existing techniques

Approved by Will 2026-07-25 ("go ahead with the entity draw-path unit").
Slot: wt/slot1, branch task/P08c-entity-drawpath, cut from fable (211115a).
Measure-and-select unit: benchmark the four ALREADY-IMPLEMENTED entity
techniques at gameplay load, produce quality evidence for Will, map each
to production applicability, recommend a landing plan. NO production
changes in this unit; benchmark fixes only if a variant is broken enough
to block measurement (report, don't redesign).

## Motivation

P08b: at gameplay-representative density (placed N=205 → median 38
visible), entities are 64.4% of combined draws and 67.8% of Pi submission
time on the canonical befallen path; befallen p95 is 279.7ms (3.6fps).
The benchmark already carries four entity-reduction techniques from the
P05 era: `--batch-entities`, `--composite-entities` (composite textures —
single draw per entity), `--lod-entities` (composite near, batched
billboards far), `--direct-gl`. Nobody has measured them head-to-head at
gameplay load with replay-comparable frames, or assessed what adopting
each in the production EntityRenderer would take. That is this unit.

## Design

### A. Technique inventory (read the code first)

1. For each flag: what it actually does (draw entries per entity, state
   changes, texture handling, visual consequences), which combinations
   are legal/sensible, and any known caveats from P05-era reviews (search
   refactor/completed/ and the P05 review). Note per P08b: the
   `entities_visible` CSV column is only populated by the
   default/unbatched path — for cross-variant comparability rely on
   identical placed-N + identical replayed path, and record entity draw
   counts per variant instead (add an additive column only if one is
   genuinely missing and cheap).
2. **Production mapping**: read `src/client/graphics/entity_renderer.cpp`
   (+ its CLAUDE.md coverage) and determine, per technique: does
   production already have an equivalent? What would adoption touch
   (texture pipeline, animation, atlas, per-entity state)? Rough size
   (small flag-gated change / medium unit / design unit). This is a
   desk-audit deliverable, not implementation.

### B. Measurement (Pi, P02 protocol, canonical replay)

3. All runs: canonical befallen path replay (--path-file
   benchmarks/paths/befallen_seed42_1000f.path), production config
   (manifest productionConfig), placed `--entities 205 --seed 42`
   (P08b's calibrated N), static entities (NOT --animate — P08b showed
   it inflates visible counts 4.5-5x).
   Variants: baseline (default entity path; P08b's e205 run may be
   reusable if schema unchanged), `--batch-entities`,
   `--composite-entities`, `--lod-entities`, `--direct-gl`, plus at most
   ONE combination if the inventory says it's the intended pairing
   (e.g. lod+composite — decide from the code, state why).
4. Per variant: whole-run avg/p95 total_ms + phase columns (submission
   split via P08's timers), entity draw counts, and frame-indexed
   extraction at the pinned poses bf355/472/626/791/917/987 (statics
   render no entities — P08b's documented substitute).
5. Optional if budget allows: one qeynos2 canonical replay for the
   winning variant (cross-zone sanity).
6. Invocation budget: <=12/session, 2 sessions OK. Prefer one clean
   session of ~6-7 invocations.

### C. Quality evidence (Will judges, not us)

7. Frame dumps at 2-3 entity-heavy pinned poses (pick from the replay
   where entities_visible is high and entities are near the camera) for
   baseline + each variant, x86 llvmpipe (--dump-frames at the extracted
   frame indices during replay). Assemble a contact sheet →
   benchmarks/contact_sheets/p08c_entity_drawpath/ with per-variant
   pixel-diff stats vs baseline. These techniques CHANGE entity
   rendering (billboards, composite textures) — diffs are expected;
   document them honestly (close-up + distant crops if the tooling makes
   it cheap). The orchestrator presents this to Will alongside the perf
   table; no self-declared "acceptable".

### D. Deliverables

8. Head-to-head table: variant | whole-run avg/p95 | submission ms |
   entity draws | vs-baseline speedup | quality-diff summary |
   production-adoption size.
9. Recommendation: which technique (or combo) to land in production, the
   landing-unit sketch (files, interactions, risks — informed by A2),
   and what the projected befallen p95 becomes if the benchmark speedup
   transfers. If nothing clears a meaningful bar (say, <10% p95 gain or
   unacceptable visuals at every setting), say so and re-point at P08's
   lever #2.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  visual gate PASS with goldens UNTOUCHED (gate runs no entities; any
  golden diff = stop and report); scripts/build-arm-noble.sh;
  scripts/refactor/check_file_sizes.sh fable (no new FAILs).
- Replay discipline: pose columns byte-identical to the path file for
  every variant; zone geometry columns (regions_vis/regions_drawn/polys
  attributable to zone) identical across variants — entity techniques
  must not perturb zone rendering; if a variant does, that is a finding.
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O
  to Pi-local /tmp, stripped staged binary from slot1 (ARM_BUILD_BIN_DIR
  trap), drop_caches never reboot, leave Pi pristine, report blocked if
  unreachable.
- CSVs → benchmarks/pi_runs/p08c_entity_drawpath/ (plain git add).

## Out of scope

- ANY production EntityRenderer change (desk-audit only; landing is the
  next unit, gated on Will's quality verdict).
- Fixing/debugging broken variants beyond what measurement needs (if one
  crashes or renders garbage: evidence, skip, report).
- --animate work; zone-geometry levers; occlusion; lighting; goldens;
  path re-recording.

## Review

(executor fills in)
