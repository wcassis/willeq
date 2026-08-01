# P51 — Canonical-KPI regression: attribute, root-cause, and re-baseline honestly

**Slot: slot1. OWNS THE PI (P02 protocol; ≤10 invocations).
Authorized by Will 2026-08-01.**

## Background

P48's KPI guard: canonical befallen e205 replay at tip = 34.3-34.6ms
p95 (blessed band 32.24-32.5, 30fps line 33.3), confirmed on repeat
AND reproduced with a pre-P48 binary — the regression landed in
a805e01 (P45's measurement) .. 9a61ed3 (P48 branch base). Window
contents: P46 `b49337b`, P46b `c40475d`, P46c `4a9b09d`, P46d
`f89e909`, P47 `6b78548`, P49-era? (NO — P49/P50 merged after
9a61ed3), testing-session merges (T15/T16 etc.), docs commits.
Methodology: benchmarks/audits/p48_sheet_alpha/EVIDENCE_MANIFEST.txt.

## PRIME HYPOTHESIS H1 — instrument honesty, not production regression

The canonical roster (races 1-12) contains NO masked races, weakening
the P46d texture-duplication suspect. But P46b replaced the
benchmark's entity handling: PRE-P46b the benchmark rendered entities
on the driver's built-in fixed-function materials (P44b: it never
linked entity_renderer's promotion); POST-P46b
applyEntityShaderMaterialPromotion() gives entities production's
per-pixel-lit shader — strictly more GPU work, and exactly what the
REAL CLIENT has always done. If H1 holds, P45/P39/P14/P11b's blessed
numbers all understated production entity-shading cost, and 34.3ms is
the honest production-representative figure (KPI actually NOT met);
the "regression" is the instrument catching up.

## Work (cheapest test first — H1 may collapse the whole bisect)

0. **H1 probe (1 invocation)**: tip binary, canonical KPI spot, with
   `--no-entity-shader-promotion`. Recovery into the 32.24-32.5 band
   confirms H1 (the delta = entity promotion); no recovery falsifies
   it → Phase A.
1. **Phase A (only if H1 falsified)**: bisect the window — build ARM
   binaries at candidate merge commits, 1 canonical spot each,
   binary-search order. Attribute to a commit, then root-cause the
   mechanism at code level.
2. **Phase B — verdict + deliverable**:
   - If H1/instrument-honesty: NO code change. Deliverable = a
     re-baselining memo (which historical numbers carry the bias and
     roughly how it scales with entity count), an updated
     honest-KPI statement for Will (production-representative p95 vs
     the 33.3ms line — connects directly to the lighting-lever data:
     per-pixel entity shading is part of this cost), and
     deferred.md/perf_truth_audit.md dated updates. The 34.3-34.6
     band becomes the new canonical reference (state it explicitly).
   - If genuine production regression: propose the fix if small and
     behavior-identical (gate + byte-identity bar), else STOP with
     memo. No silent fixes.
3. **Ride-along (P49, ~0 timed cost)**: `--res 400 300` WITHOUT
   --allow-mode-fallback → expect refusal (exit 1) + both ERROR
   lines; 800x600 control = any normal run's log showing no fallback
   line + the "Found matching mode" confirmation. Record in P49's
   deferred entry.

## Rules

- Pi protocol absolute (CmaFree gate, /tmp-only timed writes,
  drop_caches never reboot — one P02-documented reboot-recovery
  exception exists for CMA collapse, use only as last resort;
  pristine exit; DRM mode line recorded per run).
- NO production source changes without the Phase-B conditions above;
  NO preset changes; goldens untouched.
- Bar (measurement unit): builds clean, ctest baseline, gate PASS,
  ARM clean. CSVs → benchmarks/pi_runs/p51_kpi_bisect/.
- Sibling P52 (slot3) is x86-prep only and Pi-GATED behind you — it
  will not touch the Pi; you own it exclusively.

## Review

**H1 CONFIRMED on the first probe — no bisect needed. Verdict: instrument
honesty, not a production regression. NO code change. 32.24-32.5ms band
RETIRED; 34.3-34.6ms is the new canonical KPI-guard reference, effective
now.**

### Build

Rebuilt ARM tip binary from scratch (`bash scripts/build-arm-noble.sh`) —
the tree checked out at this branch's tip (`a94091d`, docs-only commit on
top of P48's merge `bd0758e`) had a stale ARM binary predating the P48
merge (Jul 31 13:07 vs P48 merge at 13:39). Rebuild completed clean
(`=== Build complete === / === Success ===`), non-stub binaries confirmed:
`gles2_zone_benchmark` 54,501,304B, `willeq` 354,101,404B, both ARM
32-bit ELF with debug_info. x86 `build-gles2`/`build-gles2-tests` trees
were already current (no production source touched by this unit — see
below) and needed only a `cmake --build` pass to pick up prior units'
newly-added test sources.

### 0. H1 probe (1 Pi invocation) — DECISIVE

Tip ARM binary, canonical e205 befallen KPI-guard spot (identical
invocation shape to P14/P39/P41/P45/P48's own canonical replay —
`refactor/scratch/p48_pi_kpi_remeasure.py`'s `run_replay`/`summarize`/
`check_drm_mode`, reused verbatim in
`refactor/scratch/p51_pi_kpi_bisect.py`), WITH P46c's
`--no-entity-shader-promotion` opt-out:

| run | total_p95 (ms) | draws_avg | in blessed band [32.24,32.5]? |
|---|---|---|---|
| P48's own tip-WITH-promotion (recorded, not re-measured) | 34.321 / 34.624 | 41.9 | NO |
| **P51 tip, `--no-entity-shader-promotion`** | **32.250** | **41.9** | **YES** |

Same binary, same shape, same `draws_avg` (structural output identical —
this is a pure timing effect, not a draw-count/structural change).
Toggling only the shader-promotion opt-out moves the number by ~2.2ms and
lands it squarely back inside the old blessed band. **Phase A bisect
skipped entirely per the plan's "cheapest test first" instruction.**
DRM mode confirmed (`Found matching mode 800x600@75Hz for requested
800x600`).

### Mechanism at code level

Pre-P46b, `gles2_zone_benchmark`'s entity path rendered on Irrlicht's
built-in fixed-function material (`EMT_SOLID`, no custom shader) — the
benchmark never linked `entity_renderer.cpp`'s shader-promotion step at
all (P44b's finding, `refactor/P44b_bless_retake.md`). P46b's
`applyEntityShaderMaterialPromotion()` (`tools/hwcaps/zone_bench_loader.cpp`,
commit `4386398`) made the benchmark correctly mirror
`EntityRenderer::createEntity()` (`src/client/graphics/entity_renderer.cpp`
lines 629-648, and 2 other call sites) for the first time: every entity
material buffer gets promoted to the real per-pixel-lit production shader
— exactly what the live client has ALWAYS done for every entity on
screen. **Production source code is unchanged by this entire arc (P46/
P46b/P46c/P46d/P47/P48) as far as this KPI question is concerned** —
P46b only closed a benchmark-tool fidelity gap; nothing about what ships
to players changed. The 32.24-32.5ms band (P14/P39/P41/P45's own blessed
numbers, all measured before `4386398`) understated production's real
per-entity shading cost because those measurements ran the benchmark's
default-cycle entities on artificially cheap fixed-function materials the
real client never uses.

### Scaling with entity count (2 additional Pi invocations)

`refactor/scratch/p51_pi_scaling_probe.py`, e5 (5 entities, P38/P41's own
"realistic NPC count" shorthand) vs the e205 canonical stress spot, same
tip binary, with vs without the promotion opt-out:

| load | draws_avg | WITH promotion (p95/avg ms) | WITHOUT promotion (p95/avg ms) | delta (p95) |
|---|---|---|---|---|
| e5 (realistic) | 8.6 | 23.957 / 10.818 | 23.502 / 10.865 | +0.455ms — noise-level |
| e205 (stress) | 41.9 | 34.47 (P48's avg of 2 runs) | 32.250 | +2.22ms (~6.9% of budget) |

The promotion cost scales with the number of shaded entity draws on
screen, not a fixed per-frame tax — negligible at a realistic 5-entity
load, only material at the 205-entity stress load the KPI guard itself
uses. **The historical blessed baselines were never meaningfully wrong at
realistic play loads**; the bias only shows up at the entity counts the
canonical KPI-guard spot deliberately stresses, and it is doing its job
correctly by surfacing it.

### Phase B verdict + deliverables (instrument-honesty path)

Per the plan's Phase B instrument-honesty branch: **NO code change** (there
is nothing to fix — production behavior was already correct and unchanged
throughout this entire investigation; the fix, such as it was, already
happened at P46b for the benchmark tool's own fidelity). Deliverables
written:
- Re-baselining memo, scaling table, and honest-KPI statement:
  `refactor/perf_truth_audit.md`, new "2026-08-01 — P51" section. States
  explicitly which historical numbers carry the bias (every pre-`4386398`
  e205-shape canonical-replay number: P14, P39, P41, P45's re-measurements,
  P48's pre-fix comparisons) and that real-roster A/B numbers
  (`--test-races`-based) are unaffected. States the 34.3-34.6ms band as
  the new canonical reference, effective now, superseding 32.24-32.5ms
  for any binary at/after `4386398`.
- `refactor/deferred.md`: P48's KPI-regression escalation entry updated
  to ATTRIBUTED/CLOSED with a pointer to the full writeup; P49's ride-along
  item marked DONE with results (see below).

### 2. P49 ride-along (1 Pi invocation, same session)

`--res 400 300` without `--allow-mode-fallback`: process refused, exit
code 1 (confirmed). Log shows the requested-vs-actual ERROR line naming
`2560x1440` as the substituted mode, plus the benchmark's own "Refusing to
run" line. The DRM device-creation-time `MODE FALLBACK` ELL_ERROR line
fires (confirmed); the documented post-init `run()`-time REPEAT does
**not** fire for this tool — expected and benign, root-caused:
`gles2_zone_benchmark`'s own resolution check exits the process before it
ever reaches `IrrlichtDevice::run()`'s frame loop, unlike `willeq`'s
continuous loop (what that repeat log was written for) — only the
device-creation-time line is reachable in this tool's architecture.
Control (800x600, this unit's own H1 probe run, same session): confirmed
no fallback line, `DRM: Found matching mode 800x600@75Hz for requested
800x600` logged normally. `refactor/deferred.md`'s P49 entry updated with
these results — P49 is now fully closed end to end.

### Bar

- Rebuilt ARM tip binary: clean, non-stub (verified sizes/file type above).
- x86 `build-gles2-tests`: rebuilt (`cmake --build`, picked up prior
  units' new test sources cleanly, 0 build errors) — **1672/1673 passed
  (99%)**, sole failure `AudioManagerTest.PreloadCommonSounds`, a
  documented pre-existing host-ALSA exception (no code touched by this
  unit is anywhere near audio). `RDPServerFrameTest.UpdateFrameMultipleTimes`
  (the other documented flake) did not fire this run.
- Visual gate: `scripts/ci/visual_gate.py` → `=== VISUAL GATE: PASS ===`.
  No goldens touched.
- ARM clean (see build section above).

### Pi invocation count + exit state

4 timed benchmark invocations of the ≤10 budget: H1 probe
(`e205_shipped_h1probe_noPromotion`), P49 ride-along
(`p49_ridealong_400x300_refusal`), and the 2-invocation e5 scaling pair
(`e5_shipped_withPromotion`, `e5_shipped_noPromotion`). CSVs/logs:
`benchmarks/pi_runs/p51_kpi_bisect/`. CmaFree dropped to 41,092 kB after
the scaling pair — 132 kB above the 40,960 kB `SessionAbort` floor, the
same episodic EQE1-correlated CMA drain P02 already characterized. A
single `sync; echo 3 > /proc/sys/vm/drop_caches` (no reboot) recovered it
to 86,024 kB, per the documented non-reboot recovery precedent (P03/P06/
P07d/P08c/P09b). Pi exit verified pristine: `/tmp` swept of all
`gles2`/`p51` artifacts, governor `ondemand` on all 4 cores, no
`gles2_zone_benchmark` process, both DRM devices free
(`lsof /dev/dri/card{0,1}` → nothing open), final CmaFree 88,248 kB.

### Deviations from the plan

None. The plan's own framing ("H1 probe first — the H1 probe may collapse
the whole bisect") is exactly what happened; Phase A's bisect matrix
(build ARM binaries at each of P46/P46b/P46c/P46d/P47) was never needed.
Spent 2 invocations beyond the plan's minimum (0 probe + Phase B's
0-cost memo + 1 ride-along = 2) on the e5 scaling pair, still well within
the ≤10 budget, because the Phase B deliverable explicitly asks "how it
scales with entity count" and a real second data point is stronger than
a hand-waved extrapolation from the single e205 number.
