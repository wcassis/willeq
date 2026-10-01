# B969 -- timorous_f2: the FP16 step lines of `vWorldPos`, placed differently by Lima

GitLab #287 (class A). Branch `task/B969-timorous-edges`, cut from `fable`
`788c6b82`. Raw evidence: `/mnt/projects/fable/review/B969/` (its README lists
every file).

**Outcome: the diverging term is isolated; no fix ships.** The only change that
removes the divergence moves all 15 x86 goldens, so this unit stopped on that
part, as dispatched. It is B967's fix (#285, class C, a re-bless). This branch
changes documents only. No budget, golden, baseline or gate assertion changed.

## What the 3,175 pixels are

The camera of timorous f2 is at EQ (-9479.7, -5678.7, 25.9). Mali-400's
fragment ALU is FP16, and so is llvmpipe's `mediump` varying path (below). At
that pose:
- Irrlicht X (|x| ≈ 9,480) is in FP16's [8192, 16384) binade: one step is
  8 units.
- Irrlicht Z (|z| ≈ 5,680) is in [4096, 8192): one step is 4 units.

So `vWorldPos` reaches the fragment shader in 8 x 4-unit steps, on both GPUs.
The player light's `pLv = uPlayerLightPos - vWorldPos` jumps by a whole step at
each step line. The light is 10-60 units from the terrain there, so each jump
is a visible discontinuity in the lit colour.
- On a terrain plane, an iso-x or iso-z line is a straight line. These are the
  "polygon edges" B965 saw. They are faint lines in the x86 golden too.
- llvmpipe puts each step line where the exact interpolated value crosses the
  rounding midpoint. The line is straight.
- Lima puts it a few pixels away, with saw-teeth. Its varying interpolation is
  not llvmpipe's, so near the midpoint it rounds the other way.
- Where the surface's x (or z) changes slowly per pixel (grazing angles), a
  tiny interpolation difference moves the line many pixels. That is the
  10-20 px band with vertical teeth.

Measured (Pi = hook binary, Lima, Mesa prefix 25.2.8 proven per run in
`mesa_maps.log`; x86 = llvmpipe; same pose and args):
- Light off (`--player-light-level 0`): Pi vs x86 drops to **9 px >32** in f2.
  The player light carries the term.
- `fract(vWorldPos / 64)` read back on both GPUs: at 3,173 of the 3,175 pixels,
  the Pi's x differs from x86's by **exactly ±8 units**, one FP16 step.
  - Over the whole frame, x differs at 7,320 px (all ±8) and z at 10,424 px
    (±4, plus ±60 wraps).
  - The differing pixels lie on the step lines
    (`crops/timorous_f2_vworldpos_ulp_flips_x_yellow_z_blue_mask_red.png`).
- Character of the pixels: a COLOUR change, not a moved polygon.
  - The geometry is the same: terrain normals agree within 2/255 except 8 px.
  - Depth is not involved: a 15-bit depth quantization on x86 moves 28 px, 1 of
    them in the Pi mask.
  - The mesh does not overlap itself: a per-normal depth bias moves 3,097 px,
    3 of them in the Pi mask.
  - What moves is the FP16 rounding boundary of a varying.
- Crops (4x): `crops/timorous_f2_diagwp_pi__diagwp_x86__pi__golden_x0-120_y240-395_crop4x.png`.
  Its panels are Pi `fract(vWorldPos/64)` | x86 | Pi lit | x86 golden. The
  Pi's step line carries the same teeth as its lit image; x86's is straight.

## How llvmpipe produces the golden (emulated on x86)

Each variant runs a highp FS with the rounding written out explicitly. The
counts are against the committed golden.

| emulation | >32 px vs golden | reading |
|---|---|---|
| per-pixel FP16 round of the interpolated `vWorldPos` (`emu_v16`) | **5** | this is llvmpipe's mediump: fp32 interpolation, then a per-pixel round to FP16 |
| the same, plus the light uniform rounded (`emu_pp16`) | 7 | the light uniform barely matters |
| only the light uniform rounded (`emu_u16`) | 14,484 | no |
| `vWorldPos` rounded at the VERTEX, FS highp (`emu_vs16`) | 16,210 | no |
| FS `precision highp` (no rounding at all) | 16,079 | the golden is NOT the fp32 answer |

## Probe table (timorous f2, 800x600, the gate's exact args)

The tool is a shader-source substitution hook, `B969_SUBST=<file>`, in
`ZoneShaderManager`. It is NOT committed: the patch is
`review/B969/scripts/b969_subst_hook.patch` and the variant files are in
`review/B969/subst/`. One ARM binary ran every variant on the Pi, in the real
gate scene. The hook binary's base frame is bit-identical to B965's shipped
binary (0 px differ).

"Pi vs x86" compares the same variant on both GPUs.

| variant | x86 vs golden >32 | Pi vs x86 >32 | Pi vs golden >32 | reading |
|---|---|---|---|---|
| base | 0 | 3,175 | 3,175 | reproduces B965 |
| `--player-light-level 0` | -- | **9** | -- | the term is in the player light |
| `--shader-debug 4` (pLight) | -- | 38,396 (3,174 of the mask) | -- | the term is inside pLight |
| `--shader-debug 5` (normal) | -- | terrain: 8 px >2 | -- | the normal is not the term; the bottom plane differs: B976 #294 |
| `--shader-debug 1` (texture) | -- | 18 | -- | the texture is not the term |
| `lut_analytic` (closed-form attenuation, no LUT texture) | 0 (max 1) | 3,173 | 3,175 | the LUT sampler is not the term |
| `varying highp vec3 vWorldPos` in the FS | 16,085 | 17,174 | **3,175** (Pi output bit-identical to base) | Lima ignores a highp varying, as it ignores a highp FS (B965) |
| `diag_wp` / `diag_plv` (`vWorldPos`, `pLv` read back) | -- | x: 7,320 px, all ±8 units | -- | **the diverging term: the FP16-rounded interpolated `vWorldPos`** |
| `vs_plv` (pLv = light - worldPos in the VS, fp32, passed as the varying) | **16,085** | **11** (>8: 1,968, from 8,656) | 16,146 | fixes the divergence, but moves the golden |

## The fix, and why it stops here

`vs_plv` is B965's "vs_plv" and B967's proposed fix. The varying then carries
small numbers (tens of units, FP16 step ≤ 0.25), so neither GPU quantizes
visibly and the two agree:
- timorous f2, Pi vs x86 with the same shader: **3,175 → 11** px >32.
- f0 and f1 stay at 2 and 0.

The x86 output moves in every gate frame (`review/B969/x86_vs_plv_vs_golden.tsv`).
Counts are >32 / >8 / any:

| frame | f0 | f1 | f2 |
|---|---|---|---|
| qeynos2 | 0 / 0 / 60,313 | 0 / 0 / 86,350 | 0 / 0 / 14,339 |
| gfaydark | 0 / 0 / 41,719 | 0 / 0 / 103,741 | 0 / 0 / 1,795 |
| befallen | 0 / 0 / 92,822 | 0 / 0 / 177,797 | 0 / 0 / 16,481 |
| iceclad | 0 / 0 / 5,667 | 0 / 0 / 64,070 | **546** / 36,827 / 203,301 |
| timorous | 0 / 79 / 184,697 | 0 / 0 / 199,303 | **16,085** / 70,476 / 230,347 |

The dispatch rule is "if any x86 pixel moves, STOP on that part". No variant
that leaves llvmpipe bit-identical can move Lima's rounding boundaries:
- llvmpipe's golden IS the fp32-interpolate-then-round-to-FP16 result.
- Lima rounds a differently interpolated value.
- Lima's FS cannot hold more than FP16, whatever the qualifier (B965, and the
  highp-varying row above).
- Any change to what is interpolated moves llvmpipe's boundaries too.

So the fix is B967's re-bless, which is Will's call. B969 is now recorded as the
Pi half of B967.

## Pi -- the 15 gate frames

No fix shipped, so "after" equals "before". These are B965's shipped binary
(`review/B965/pi_vs_x86_golden.tsv`), with this unit's hook binary agreeing
bit for bit on timorous. Counts are >32 vs the x86 golden:

| zone | f0 | f1 | f2 |
|---|---|---|---|
| qeynos2 | 0 | 2 | 2 |
| gfaydark | 429 | 66 | 163 |
| befallen | 4 | 3 | 0 |
| iceclad | 2 | 29 | 335 |
| timorous | 2 | 0 | 3,175 |

iceclad_f2's 335 is B967's banding, and the same mechanism: the pose is at x ≈ 7,234, so the step is 4 units.

## Frame time

Not measured. No shader shipped, so there was nothing to A/B.

## Not verified

- `vs_plv` on the Pi in the other four zones, and its frame time: stopped
  before, per the STOP rule. Only timorous was run on the Pi.
- Lima's varying interpolation at Mesa source / hardware level: how many bits,
  and whether it is perspective-correct in the same order as llvmpipe. Only
  its effect was measured, as rounding flips of one FP16 step.
- Mesa versions other than 25.2.8, and the system Mesa.
- The bottom plane's normal divergence (B976, #294).

## New findings (filed)

- **B976 (#294, B)**: in timorous f2, `--shader-debug 5` shows the bottom plane
  with normal +Y on llvmpipe and G=0 on Lima. The player light lights it on
  x86 only. It is nearly black in the gate frame.

## Deviations

- **No fix and no MR closing #287.** The MR says `Refs #287`, not
  `Closes #287`, because the defect stays open until B967 is decided.
- The probe is the gate scene plus an uncommitted shader-substitution hook, not
  a new `tools/hwcaps` program. The patch and the variant files are kept in
  `review/B969/` for reruns.
- **Pi: one session, 11 benchmark invocations, lock `b969`.** The lock was
  taken and released twice; B964 held it before. The Pi was left with nothing
  in `/tmp`. The governor was not touched.
- The ARM binary is a local docker build (`scripts/build-arm-noble.sh`, the
  image's cached layers), not CI `build-arm` (B970).
