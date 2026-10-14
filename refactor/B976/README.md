# B976: timorous f2 bottom plane — it is not one plane, and the normal never diverges

Issue #294. Found by B969. This unit measures what the plane actually is and where
the x86-vs-Pi difference comes from. **It comes from neither the normal
interpolation nor the shader: the bottom plane is two real, overlapping,
near-coplanar floor layers, and the depth buffer picks a different winner per
platform.** The +Y normal on llvmpipe and the −Y normal on Lima are the source
normals of two different WLD regions that both cover the pixel.

## What the plane is

At the gate pose f2 (camera EQ −9479.7, −5678.7, 25.9; 800×600), the flat plane in
the bottom of the frame (rows ~460–600) is covered by **two** zone regions at
nearly the same world location, with opposite unit normals and different
textures:

| layer | region | draw entries | source normal | texture | view-z at (400,520) | debug5 pixel |
|---|---|---|---|---|---|---|
| A (up) | 10298 | 196 | (0, +1, 0) | `sand3.bmp` | 186.87 (probe hit) | (128,255,128) |
| B (down) | floor under 10298 | 158–163 | (0, −1, 0) exact | `w1.bmp` | ~240 (see below) | (128,0,128) |

Layer B is the flat **−Y `w1` floor quad** — the pixel's exact (0,−1,0)
normal is its source normal (debug5 (128,0,128) decodes to (0,−1,0) exactly;
the adjacent `dredsnow` slope quad, region 10302, is (0,−0.769,−0.097) and
would not decode to that pixel). The probe's per-entry bboxes pin the `w1`
quads over the pixel (entries 158–163, `bbox y-max=463`, `behindNear=2`).
Their exact view-z was not read back (see Not verified); what the frames
establish directly is the depth *relation*: on the D16 surface the two
layers' rasterized depths fall within one 16-bit step of each other (the
per-row z-fight), while on 24-bit surfaces they separate into a stable
winner — sand3 on x86, w1 on the Pi.

The regions are stacked within a few world units vertically (the B969 finding:
"the terrain normals agree within 2 except 8 px"), i.e. near-coplanar, not
coplanar. The −Y `w1` layer's two near corners sit **behind the near plane**
(probe: `behindNear=2` of 4 verts on every `w1` quad); the GPU near-clips them
and renders the visible half, so the −Y layer does rasterize over the whole
bottom of the frame.

Measured with a throwaway point-in-triangle probe (near-plane clipping,
barycentric normal interpolation) added to `tools/hwcaps/zone_bench_modes.cpp`
in this session and **not committed** — it existed only to take the numbers in
the tables below; the per-entry evidence is in `dbgin6_083659/bench.log`:

```
entry=158 verts=4 behindNear=2 bbox=(22,463)-(708,463)  avgN=(0.000,0.500,0.000) tex=w1.bmp
entry=159 verts=4 behindNear=2 bbox=(22,463)-(708,463)  avgN=(0.000,-0.500,0.000) tex=w1.bmp
...
entry=196 verts=4 behindNear=0 bbox=(247,481)-(803,721) avgN=(0.000,1.000,0.000) tex=sand3.bmp
  zview=186.87 entry=196 tri=0 region=10298 tex=sand3.bmp
  zview=240.48 entry=29  tri=5 region=10302 tex=dredsnow.bmp   (adjacent −0.78Y quad, not the winner)
```

## The decisive A/B: same binary, same args, only the depth format differs

Both runs are this session's, on this host (x86, llvmpipe surfaceless EGL), the
gate's exact args (`--occluder-budget 16 --adaptive-lighting --zone-pp-budget 20
--pp-budget-mode count --player-light-level 9`, 110 regions / 197 draws),
`--shader-debug 5`. The only variable is the window surface's depth format,
forced with a throwaway env gate (`B976_FORCE_STENCIL`), matching what the
production preset requests.

| run (evidence dir) | surface depth | (400,520) | col-x=400 signature |
|---|---|---|---|
| `gateargs_d16_081207` | **D16**, no stencil — `logical_depth_stencil=960000` (0.92 MiB @800×600) | **(128,0,128) = −Y** | **z-fights**: 22 ±Y switches in the x=400 column (y=463→598) — +Y stripes of 2–3 px over −Y stripes of 9–10 px (period ~12 px); block cols300–500/rows464–598 = 6 030 +Y / 21 105 −Y |
| `d24s8_gateargs_081816` | **D24S8** — `logical_depth_stencil=1920000` (1.83 MiB) | **(128,255,128) = +Y** | single boundary at y=463, uniform +Y below (block cols300–500/rows464–598 = 27 135 +Y / 0 −Y) |

The alternating rows in the D16 run are the fingerprint of two surfaces landing
in adjacent quantization cells — a pure depth tie, not a normal. The same
D24S8 result matches B969's x86 frame at this pixel
(`b969_evidence/x86/timorous_f2.png` → (128,255,128)), and the Pi frame
below matches B969's Pi frame at this pixel (→ (128,0,128)).

## The Pi leg (this session, current tip)

Ran the published benchmark `/mnt/projects/fable/ci-arm/by-branch/fable/bin/
gles2_zone_benchmark-nollvm` (build sha `c7abab9c`, an ancestor of this tip:
the 5 commits between them are B1005 camp/charselect and B502 tombstone
comments — no zone-loading, shader or depth-path change) with the same gate
args, 800×600, `--shader-debug 5` and `4`:

- surface: `EGL: Config depth=24 stencil=0` — the B971 request (no stencil)
  reached the driver, but **Lima 25.2.8 answers depth=24**, exactly as B971
  recorded;
- (400,520) debug5: **(128,0,128) = −Y** — the whole sampled plane block is
  uniformly −Y (G=0 on all 10 000 px of cols350–449/rows470–569; zero +Y
  pixels). It is **not** a per-row ±Y z-fight like x86-D16: the column
  alternates only between (128,0,128) and (127,0,127) — a 1-LSB rounding
  ripple on the *same* (0,−1,0) normal — 5 000 of each; the −Y plane's top
  edge at x=400 is at y=381;
- (400,520) debug4 (pLight): **(0,0,0)** — the −Y face points away from the
  player light, so the lit plane is unlit on the Pi;
- identical to B969's Pi frame at this pixel.

## So why does x86 light it and the Pi not

The player light (`--player-light-level 9`) illuminates whatever surface wins
the depth test at the pixel:

- **x86, D24S8 surface**: the +Y `sand3` layer wins the depth test over the
  whole block → normal (0,+1,0) → the light hits it → debug4 non-zero on that
  plane (44,39,26 at (400,550), B969's x86 measurement).
- **x86, D16 surface** (post-B971): the two layers' view-z values land in the
  coarse 16-bit grid close enough that the test flips pixel by pixel → the
  z-fight above.
- **Pi (Lima, depth=24 either way)**: the driver's depth comparison selects
  the −Y `w1` layer over the whole block → normal (0,−1,0) → the player light
  (above the plane) dots to ≤ 0 → pLight (0,0,0).

Note the two 24-bit runs disagree with each other (x86 D24S8 → +Y, Pi D24 →
−Y), so the winner at this near-tie is a function of **both** the depth
format and the driver's depth-compare semantics, not of format alone. That is
why the symptom moved after B971 on x86 (D24S8→D16 flipped x86's winner to
the −Y side, making x86 *agree* with the Pi in debug5) while the Pi, whose
driver kept depth=24, did not move at all (B971: 0 px vs B967 on all 15 gate
frames).

No normal is interpolated wrongly anywhere: every measured normal is the
bit-exact source WLD normal of the region that won the depth test at that
pixel (the region matrices are pure translations, so the interpolated varying
is just the per-vertex source normal). The X and Z components of the
"divergent" normal agree between platforms within the plane's own noise; only
Y flips sign, and it flips because the **region** changes, not the
interpolation. B969's FP16 hypothesis is dead: nothing rounds +0.79 to −0.79;
two regions with (0,±1,0) source normals both render, and the winner changes.

The gate barely sees any of it because in production (no `--shader-debug`) the
plane is nearly black on both platforms (x86 (1,3,3), Pi (0,0,0), issue
body) — the ±Y normal flip changes the diffuse term of a mostly-ambient dark
floor, which is well inside the gate's per-pixel budget.

## Fix (2026-10-14, Will ruled option 1: the top-facing region wins a depth tie)

**The implemented rule.** When two zone regions tie in depth at a pixel, the
region whose face normal points UP (+Y in Irrlicht space) wins, on every
platform. The bias is a per-vertex clip-space depth offset in the zone REGION
vertex programs, gated by a per-material flag so nothing outside zone-region
geometry is ever touched:

- `include/client/graphics/zone_depth_tie_break.h` — the rule, stated and the
  pure C++ decision (`downFacingSelector`, `ndcDepthTieShift`,
  `biasedStoredDepth`, `uDepthTieBiasForFlag`), unit-tested without a GPU
  (`tests/test_zone_depth_tie_break.cpp`, 7 tests), plus the one GLSL line:
  `EQT_DEPTH_TIE_VS_LINES`.
- The line: `gl_Position.z += uDepthTieBias * step(0.5, -worldN.y) * gl_Position.w;`
  — spliced into exactly the two region vertex programs
  (`VERTEX_SHADER_SRC`, `VERTEX_SHADER_LIGHTWEIGHT_SRC` in
  `src/client/graphics/zone_shader_sources_gles2.h`); the source-scan test
  pins its presence there and its absence from the atlas/wind programs.
- The flag: `SMaterial::MaterialTypeParam2 = 1.0`, stamped by
  `ZoneMeshBuilder::setZoneGeometryDepthTie(true)` for zone-REGION buffers
  (`zone_geometry.cpp`, enabled in `renderer_zone_load.cpp` step 4 and the
  benchmark's region builder). Objects, doors and trees never set it, and the
  atlas/wind programs (which trees DO use) neither declare nor read
  `uDepthTieBias`.
- `B = kNdcDepthTieBias = 2 * (1/32768)` NDC-z — exactly 2 D16 cells: the
  smallest value that breaks the measured within-~1-cell tie with a 1-cell
  margin over the per-row quantization ripple (the README's D16 z-fight is
  the test case), and too small to reorder surfaces a few cm apart.

**Why this form (two properties, and two corrected draft errors).**
1. The offset is B in clip-z, which is B in NDC-z *at every fragment*: the
   rasterizer perspective-correctly interpolates z/w, and adding B*w to
   every vertex's clip-z adds B to the interpolated z/w regardless of w.
   x/w and y/w are untouched, so every triangle's screen-space footprint is
   bit-exact — only its stored depth moves. (A world-space nudge would decay
   into sub-quantum at range on D16.) At the clip plane (w=0) B*w = 0, so
   near-clipped edges are unchanged too.
2. LARGER stored depth = farther and the test is GL_LEQUAL (nearer wins), so
   pushing the down-facing layer +B makes the top layer strictly nearer — in
   every depth format, on every driver. The selector `step(0.5, -worldN.y)`
   is 1 only for down-facing faces (worldN.y ≤ −0.5); a top-facing floor is
   untouched, and region matrices are pure translations, so a floor's −1.0
   source normal never wavers by interpolation.

Corrected from the first draft (pinned by `TheMacroIsPlusZFartherOnTheDownFacingLayer`):
adding B to `gl_Position.w` would move the vertex NEARER (z/w shrinks) — the
wrong direction — and `step(0.5, worldN.y)` selects the UP-facing layer.

### Acceptance measurements (this run, x86 llvmpipe, the gate's exact args)

**(A) Unit test + goldens.** `test_zone_depth_tie_break`: 7/7 green (the pure
decision: the +Y quad strictly wins a within-<1-cell tie; two down-facing
surfaces more than B apart keep their order; the material gate; B = 2 D16
cells; the GLSL splice scoped to the two region programs). The zone dispatch /
renderer order/coverage goldens for the touched TUs are green; hygiene
(`ctest -L hygiene`, 32/32) and DocCounts (0 failures) are green.

**(B) The pose, both surface formats** (timorous f2, 800×600, `--shader-debug 5`,
x=400 column y=463..598; D24S8 via the recreated throwaway
`B976_FORCE_STENCIL=1` gate in a scratch lib, never committed):

| run | surface | (400,520) | col x=400 |
|---|---|---|---|
| before, D16 (committed surface, `logical_depth_stencil=960000`) | D16 | (128,0,128) −Y | **z-fights**: 22 ±Y switches, +Y runs 2-3px / −Y runs 9-10px — matches the pre-fix README run byte-for-byte (before run is 0 px vs the blessed golden on all 15 frames) |
| after, D16 | D16 | **(128,255,128) +Y** | **0 switches**: 136 +Y / 0 −Y, single boundary at y=463 |
| after, D24S8 (scratch `B976_FORCE_STENCIL=1`, `logical_depth_stencil=1920000`) | D24S8 | **(128,255,128) +Y** | **0 switches**: 136 +Y / 0 −Y |

+Y wins on BOTH formats now — the tie is broken deterministically.

The strip image `refactor/B976/floor_before_after_diff.jpg` is gitignored
(`*.jpg` is in `.gitignore`), so it is a local evidence artifact in the
checkout, not committed — the full-resolution frames below are the committed
record. Floor region y 430..600 of the 800×600 frames: rows 1–2 timorous_f1
before/after, rows 3–4 timorous_f2 before/after, row 5 the f2 before→after
diff — the before rows are dark floors, the after rows the lit +Y floor.
Full-resolution frames: `gate_before/run/`, `gate_after/run/` in the
evidence dir.

**(C) Delta table — and the finding that changes the bless scope.**

Draws, state changes and poly counts are IDENTICAL before/after for all five
gate zones × 3 frames (per-frame CSV, the gate's own invocation; see
`gate_before/run/`, `gate_after/run/` in the evidence dir). The bias changes
no draw, state or poly count anywhere:

| zone | f0 d/s/p (b→a) | f1 d/s/p | f2 d/s/p |
|---|---|---|---|
| qeynos2 | 236/31/885 = 236/31/885 | 119/22/542 = 119/22/542 | 228/16/663 = 228/16/663 |
| gfaydark | 143/15/3338 = 143/15/3338 | 134/13/3633 = 134/13/3633 | 203/16/4421 = 203/16/4421 |
| befallen | 368/13/1241 = 368/13/1241 | 75/9/384 = 75/9/384 | 170/9/526 = 170/9/526 |
| iceclad | 35/5/1129 = 35/5/1129 | 36/3/979 = 36/3/979 | 32/3/533 = 32/3/533 |
| timorous | 586/20/11258 = 586/20/11258 | 432/20/7624 = 432/20/7624 | 197/7/6802 = 197/7/6802 |

Per-pixel delta, before (origin/fable, 0 px vs blessed on all 15 frames) vs
after (this run), production frames (no `--shader-debug`):

| zone/frame | px changed (of 480 000) | what changed |
|---|---|---|
| qeynos2 f0/f1/f2 | 134 444 / 0 / 320 | lit ground plane (was black) |
| gfaydark f0/f1/f2 | 273 / 876 / 953 | floor edges |
| befallen f0/f1/f2 | 28 / 0 / 0 | floor corner |
| iceclad f0/f1/f2 | 1 116 / 124 677 / 13 642 | lit ground plane (was dark) |
| timorous f0/f1/f2 | 114 759 / 171 726 / 121 222 | the lit floor (the target) |

**The card expected only timorous_f1/f2 to change. They do not — every
zone's floor pixels move. This is NOT a leak in the mechanism; it is the
double-floor defect being more common than the README's "timorous-specific
measurement; a zone-wide census would be a follow-up unit" anticipated.**
The debug5 normal renders of the changed regions (evidence `mech/`) show
100% of the changed pixels are clean −Y→+Y normal flips — zero wrong-direction
flips, zero non-normal (lighting/color) changes — in both qeynos2 f0 and
iceclad f1, and the changed region is the flat ground plane in each. Those
zones ALSO ship a −Y underlayer under a +Y top layer; before the bias, D16
picked the −Y (or z-fought) and the floor was unlit/dark, so the gate never
saw it. Will's ruling covers this: option 1 "makes the +Y `sand3` win
everywhere and kills the D16 z-fight". The bless scope is therefore the
**floor pixels of all five zones, on both platforms**, not just timorous
(Will's call, stated here). The two timorous goldens the card named remain
the largest deltas; the other zones' deltas are the same ruling's
consequence on the same defect.

## Not verified

- The per-pixel **depth values** at (400,520) were not read back from the FBO
  (the probe reconstructed them CPU-side from the draw entries; the
  rasterizer's own written depth is what the A/B frames show indirectly via
  the winner). The z-fight/alternation and the uniform winners on each
  platform are read from the rendered debug5 frames themselves, which is the
  acceptance measurement.
- Pi run at 800×600 (to match B969 and the x86 A/B pixel-for-pixel); the
  gate's 1280×720 Pi pose was not re-taken for this pixel — the surface line
  and the per-pixel normal at 800×600 are the acceptance items the issue
  names.
- **The Pi leg of the FIX** (lane 1, same pose, `--shader-debug 5` + debug4
  lit floor on the biased binary) — this card is the x86 leg only; the Pi
  run is the named FOLLOW-UP after Will's bless. The mechanism's platform
  independence is argued (the bias is in NDC-z, format- and
  driver-independent), not re-measured on Lima here.
- The double-floor census is now PARTIALLY verified: the gate's five zones
  all show the −Y→+Y flip on flat ground (qeynos2 f0/f2, gfaydark f0/f1/f2,
  befallen f0, iceclad f0/f1/f2, timorous f0/f1/f2 — the Fix delta table),
  so the defect is not timorous-specific. A census of the NON-gate zones is
  still open (FOLLOW-UP).
- The D24S8 A/B of the FIX used a scratch libIrrlicht with a throwaway
  `B976_FORCE_STENCIL` gate (recreated out-of-tree; the committed driver only
  honors the preset's stencil request, post-B971). It proves the +Y win on
  the 24-bit surface on x86; it is not a committed code path.

## Evidence

Raw (pre-fix measurement, 2026-10-07): `~/kb-evidence/b976-timorous-plane/`

- `gateargs_d16_081207/` — x86 D16, debug5/4, bench.log (`logical_depth_stencil=960000`)
- `d24s8_gateargs_081816/` — x86 D24S8, debug5/4, bench.log (`logical_depth_stencil=1920000`)
- `pi_current/` — Pi current tip, dbg5+dbg4 frames/CSVs, `EGL: Config depth=24 stencil=0`
- `dbgin6_083659/bench.log` — per-entry probe lines (entries 158–163 `w1` ±Y, entry 196 `sand3` +Y, behindNear counts, zview per hit)
- `d10_d16_084125/`, `d10_d24s8_084135/` — debug10 depth-visualization pairs (same quantized value at (400,520) in both formats: the tie is sub-quantum on the winner, visible in the loser's cells)
- `b969_evidence/{pi,x86}/` — B969 reference frames (pixel-identical to this run's D24S8 x86 and Pi at (400,520))

Raw (the fix, 2026-10-14): `~/kb-evidence/kb-b976-depth-tiebreak/`

- `before_d16/`, `after_d16/` — the f2 pose, production D16 surface, `--shader-debug 5`, before (origin/fable) vs after (branch) frames + CSVs
- `after_d24s8/` — the f2 pose on the forced D24S8 scratch surface, after code, `--shader-debug 5` (`logical_depth_stencil=1920000`)
- `gate_before/run/`, `gate_after/run/` — all five gate zones × 3 frames, the gate's exact invocation (production frames + CSVs + memory JSON); `baseline_check.py` output: before is 0 px vs the blessed goldens on all 15 frames
- `mech/{qe_before,qe_after,ic_before,ic_after}/` — debug5 normal renders of the non-timorous changed regions; `mech_check.py` output: 100% of changed pixels are −Y→+Y flips
- `build_d24s8_irrlicht.sh` + `patch_gate.py` — the throwaway D24S8 gate (out-of-tree)
- `delta_table.py`, `before_after_px.py`, `f2_colstats.py` — the delta and column statistics above
