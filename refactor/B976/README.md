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
| `gateargs_d16_081207` | **D16**, no stencil — `logical_depth_stencil=960000` (0.92 MiB @800×600) | **(128,0,128) = −Y** | **z-fights**: (128,0,128)/(128,255,128) alternate every ~2 px from y=464 to 598 |
| `d24s8_gateargs_081816` | **D24S8** — `logical_depth_stencil=1920000` (1.83 MiB) | **(128,255,128) = +Y** | single boundary at y=464, uniform +Y below |

The alternating rows in the D16 run are the fingerprint of two surfaces landing
in adjacent quantization cells — a pure depth tie, not a normal. The same
D24S8 result matches B969's x86 frame byte-for-byte at this pixel
(`b969_evidence/x86/timorous_f2.png` → (128,255,128)).

## The Pi leg (this session, current tip)

Ran the published benchmark `/mnt/projects/fable/ci-arm/by-branch/fable/bin/
gles2_zone_benchmark-nollvm` (build sha `c7abab9c`, an ancestor of this tip:
the 5 commits between them are B1005 camp/charselect and B502 tombstone
comments — no zone-loading, shader or depth-path change) with the same gate
args, 800×600, `--shader-debug 5` and `4`:

- surface: `EGL: Config depth=24 stencil=0` — the B971 request (no stencil)
  reached the driver, but **Lima 25.2.8 answers depth=24**, exactly as B971
  recorded;
- (400,520) debug5: **(128,0,128) = −Y**, uniform — the whole sampled plane
  block (10 000 samples, rows 470–570) is 8 974× (128,0,128); a single boundary
  at y=440, no alternation (unlike x86-D16);
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

## Fix status

**No production change applied.** Any fix (a depth bias between the two
layers, merging the coincident region quads, a polygon-offset pass, or a
WLD-authoring question of whether the zone ships a double floor at all) moves
golden pixels, and per the card such a fix stops at the measured fix + delta
table and blocks for Will's bless. The candidate fixes and their measured
surface:

1. **Per-region depth bias** (e.g. a small polygon offset for the lower-
   elevation `w1` layer, or draw-order + depthFunc tweak in the zone pass) —
   makes the +Y `sand3` win everywhere and kills the D16 z-fight; changes
   timorous_f1/f2 goldens (the f1 frame already carries 168k "any-diff" px
   from the D24→D16 change, B971 table).
2. **Data-side** — if the double floor is a WLD authoring artifact (a −Y
   underlayer under the +Y top), the clean fix is upstream data, not client
   bias; needs Will's call since it changes what the official-client match is.

Both are blessed-golden changes, so this unit stops here.

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
- Whether the double floor exists in other zones (timorous-specific
  measurement; a zone-wide census of stacked opposite-normal quads would be a
  follow-up unit).

## Evidence

Raw (this run): `~/kb-evidence/b976-timorous-plane/`

- `gateargs_d16_081207/` — x86 D16, debug5/4, bench.log (`logical_depth_stencil=960000`)
- `d24s8_gateargs_081816/` — x86 D24S8, debug5/4, bench.log (`logical_depth_stencil=1920000`)
- `pi_current/` — Pi current tip, dbg5+dbg4 frames/CSVs, `EGL: Config depth=24 stencil=0`
- `dbgin6_083659/bench.log` — per-entry probe lines (entries 158–163 `w1` ±Y, entry 196 `sand3` +Y, behindNear counts, zview per hit)
- `d10_d16_084125/`, `d10_d24s8_084135/` — debug10 depth-visualization pairs (same quantized value at (400,520) in both formats: the tie is sub-quantum on the winner, visible in the loser's cells)
- `b969_evidence/{pi,x86}/` — B969 reference frames (pixel-identical to this run's D24S8 x86 and Pi at (400,520))
