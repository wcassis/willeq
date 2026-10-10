# B997 baseline (step 1) — fable tip 1b0f8645 (ring + glFlush), lane 2, Pi

Rig: scripts/b964/pi_crowd.sh, --pi-dir ~/ci-arm/1b0f8645..., qeynos2+gfaydark,
imp200far, 800x600, cache-on default, --bo-census (B1006 shim md5
77a57cc213cd231b3245485cb0c7f270), EQEMU_HOST=172.16.200.98 (lane 2),
**--idle 180 --hold-after 75** (the documented B975-acceptance recipe; this is
what the reference imp200far legs used — the wrapper DEFAULT idle=40 lands
steady_60s after the crowd is PVS-culled and reads ~1.5-5 MiB LOW, see
"Non-conformity" below).

## Step-1 baseline columns (results.tsv + gem_owners.tsv @ steady_60s)

| zone | gpu_unnamed_steady (MiB) | pp jobs/f | wait ms/f | imp p50 (ms, steady) | fps_med |
|---|---|---|---|---|---|
| qeynos2 | **17.63** | 2.26 | 0.054 | 25.6 | 30 |
| gfaydark | **20.08** | 2.30 | 0.053 | 31.2 | 25 |

Card expected: ~17.1 / ~22.1, ~2.2 jobs/frame.
- qeynos2 17.63 vs 17.1: within run-to-run spread (ref qeynos2 legs 17.07-18.92).
- gfaydark 20.08 vs 22.14: ~2 MiB below; ref gfaydark legs spread 22.14-28.14,
  so this is at the low edge of the spread, NOT outside it. Draw composition is
  CODE-IDENTICAL to the ref (38/77 zone-merge, 187 impostors, 0 nametags); the
  ~2 MiB delta is entirely in the in-flight `draw3d:client` varying-BO snapshot
  (a churn pool, fact 5 — see "draw-count lever" below). No graphics/draw-path
  code changed between the ref build (ffecf7d3) and this tip (1b0f8645): only
  bridge intents / camera / frame-input. Self-consistent baseline: every step-4
  A/B leg runs on THIS build (1b0f8645) with THIS rig, so diffs are exact.

## Non-conformity found and fixed (the #1 gotcha)
First run used the wrapper DEFAULT idle=40/hold-after=15 (pi_crowd.sh line 48-49)
instead of the documented `--idle 180 --hold-after 75` (pi_crowd.sh line 21,
crowd_run.sh line 36). With idle=40 the char sits at the far stand through the
idle window; the 200-NPC crowd (115-160 units away) is OUT of PVS range, so it
is NOT built and stays culled (candidates 1-3) at steady_60s. The reference
imp200far legs (glFlush 22.14, B972 24.5) all used idle=180 so the crowd IS in
range/built at steady. idle=180 re-take reads 17.63/20.08 — the crowd is in
range (160/187 candidates at the steady instant). **Always pass --idle 180
--hold-after 75 for the imp200far crowd-tour rig.** (Also: the sweep forces
EQEMU_HOST=${EQEMU_HOST:-local} BEFORE sourcing eqemu/env.sh, so EQEMU_LANE
alone does NOT pick the lane — pass EQEMU_HOST=172.16.200.98 explicitly.)

## Draw census (step 2, from the Pi client.log @ steady)

Per-frame [mergedraws] (zone-merge renderMerged, pooled T504 path):
| zone | unmerged_would_be_draws | actual_draws (zone-merge) |
|---|---|---|
| qeynos2 | 32 | ~24 (p50 22, max 38) |
| gfaydark | 77 | ~38 (p50 38) |

Other per-frame sites:
| site | qeynos2 | gfaydark | notes |
|---|---|---|---|
| zone-merge (renderMerged) | 24 | 38 | the draw:zone pool |
| impostor (ImpostorBatchNode) | 1 | 1 | ONE packed-VBO draw (B972 ring) |
| nametags (NameTagBatchNode) | 1 | 0 | one VBO draw (B975 ring) |

Zone pool build (per zone):
- qeynos2: 1857 regions, 3662 slices, 126 materials, 126 generations, 32491 verts
- gfaydark: 1429 regions, 2865 slices, 32 materials, 32 generations, 126398 verts

## The draw-count lever (where the ~16-20 MiB unnamed actually sits)
gem_owners.tsv @ steady_60s, the per-draw varying-BO (fact 5) pools:
| owner row | qeynos2 (MiB, BOs) | gfaydark (MiB, BOs) | ref glFlush gfaydark |
|---|---|---|---|
| draw3d:client (entity/door/object/particle client-array 3D) | 9.66, 1465 | **11.36, 1419** | **14.06, 1581** |
| draw:zone (zone-merge pooled) | 1.27, 62 | 4.08, 148 | 3.93, 124 |
| vbo:nametags (named, NOT unnamed) | 1.86 | 1.40 | 1.48 |
| vbo:impostor (named, NOT unnamed) | 0.04 | 0.03 | 0.01 |

So the zone-merge (the original B997 target) is only ~1.3-4 MiB. The DOMINANT
unnamed pool is draw3d:client (11-14 MiB) — the entity-aux / 3D client-array
draws. The whole gap vs the ref (20.08 vs 22.14) is the draw3d:client row
(1419 vs 1581 BOs). This is a churn/snapshot variable, not a code difference.

Evidence: ~/kb-evidence/b997-draw-count/baseline-1b0f8645-20261009-164216/
(idle=180 conformant run). Superseded idle=40 run:
baseline-1b0f8645-20261009-161526/ (DO NOT use — crowd culled).
