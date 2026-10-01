# B965 -- the per-pixel player light blown out on Lima

GitLab #283 (class A). Branch `task/B965-player-light-lima`, cut from `fable`
`04584a28` and merged with `fable` `421670f7` (B960) mid-unit. Raw evidence:
`/mnt/projects/fable/review/B965/` (its README lists every file).

No budget, golden, baseline or gate assertion changed. The x86 goldens are
pixel-identical (local visual gate: 0 PIXEL MISMATCH over all 15 frames).

## The diverging term

**The LUT coordinate `d2 * uLUTScale`, where `d2 = dot(pLv, pLv)`.** Mali-400's
fragment ALU is FP16. For any pixel 256 or more units from the light, d2 is past
FP16's maximum (65,504) and becomes +inf. Lima's sampler, `GL_LINEAR` with
`GL_CLAMP_TO_EDGE`, returns the LUT's NEAR end for an infinite coordinate. That
is attenuation ~1.0 and `1/d` ~0.35 instead of the far end, so every pixel past
256 units is lit at full strength. llvmpipe computes the same term in fp32 and
samples the last texel. On the Pi this is the white band in the middle distance
(B960's `review/B960/pi_vs_x86/*.png`), not the area at the camera.

### Probe -- `tools/hwcaps/gles2_player_light_probe`

One full-viewport quad per draw. Its world positions run along +X from the light,
so pixel column x is at distance `d = (x + 0.5) * 400 / 800` from the light. The
middle row is read back. The light is the gate's lightstone, level 9: colour
(2.673, 2.376, 1.566), attenuation (1, 0, 19/92^2). Output = `pLight * 0.25` as
bytes. Two surfaces are drawn:
- a wall facing the light (N = -X);
- a floor 5 units below it (N = +Y).

Two light origins are used:
- (0, 0, 0);
- (7234.6, 13.9, 5619.3), the iceclad gate pose f2 in Irrlicht coordinates.

Platforms:
- Pi: Orange Pi One, Lima, shipped Mesa prefix 25.2.8 (maps:
  `/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so`, no libLLVM).
- x86: llvmpipe, Mesa 25.2.8.

Wall, far origin. "Pi LUT atten" is G of `diag_lut` (the LUT's alpha read at the
production coordinate), and "Pi d2*scale/64" is G of `diag_coord`:

| d (units) | x86 prod R | Pi prod R | Pi fix (`prod_saturate`) R | x86 LUT atten | Pi LUT atten | Pi d2*scale/64 |
|---|---|---|---|---|---|---|
| 7.75 | 150 | 150 | 150 | 223 | 223 | 1 |
| 31.75 | 52 | 52 | 52 | 77 | 78 | 1 |
| 63.75 | 17 | 17 | 17 | 25 | 26 | 4 |
| 127.75 | 33 | 34 | 34 | 25 | 26 | 16 |
| 249.75 | 65 | 65 | 65 | 25 | 26 | 60 |
| 255.75 | 67 | **255** | 67 | 25 | **251** | 255 |
| 299.75 | 79 | **255** | 79 | 25 | **251** | 255 |
| 399.75 | 105 | **255** | 105 | 25 | **251** | 255 |

Every variant against x86 `prod`, all 800 columns, both surfaces, both origins.
These are max abs differences per band of distance
(`review/B965/probe_compare.py`):

| variant | 0-256 units | 256-400 units | reading |
|---|---|---|---|
| `prod` (the production FS) | <= 1 | **216 (wall) / 254 (floor)** | diverges at exactly d = 256 |
| `prod_highp` (`precision highp` under `GL_FRAGMENT_PRECISION_HIGH`) | same as Pi `prod` | 216 / 254 | Lima defines the macro and reports fp32 for highp and mediump FS floats (127,127,23), but the pixels equal mediump's bit for bit: **highp in the FS is silently FP16** |
| `vs_plv` (pLv formed in the VS, FP32, passed as a varying) | <= 1 | 216 / 254 | the subtraction is not the term: d2 is still formed in the FS |
| `prod_nearest` (LUT `GL_NEAREST`) | <= 1 | <= 1 | Lima's NEAREST sampler handles +inf; llvmpipe's NEAREST does not (it goes white past 256 on x86 itself) |
| `prod_lut_fp32` | -- | -- | unavailable on the Pi (no `GL_OES_texture_float`); on x86 identical to `prod` |
| `diag_lut_linear_coord` (the LUT read at d/16, i.e. finite coordinates 0..25) | <= 1 | <= 1 | large FINITE coordinates clamp correctly: only +inf breaks |
| `diag_coord` (`clamp(c)`, `clamp(c/64)`, `step(60000, d2)`) | <= 1 | <= 1 | the overflow itself compares and clamps correctly |
| `prod_clamped` (`min(c, 1.0)`) | <= 1 | <= 1 | fixed |
| **`prod_saturate`** (`clamp(c, 0.0, 1.0)`, shipped) | <= 1 | <= 1 | fixed |
| `fs_prescaled`, `vs_prescaled`, `fs_prescaled_clamped` (pLv * 1/64 before the dot) | <= 1 | <= 1 | fixed |

The FP16 LUT format, filtering in general, the attenuation constant and uniform
precision are not the term. With the light off, B960 already showed the gap
collapses.

## The fix

`src/client/graphics/zone_shader_sources_gles2.h`: in all six player-light FS
sources (solid, alpha, alpha-RGBA, atlas solid, atlas alpha, and the
`/shaderdebug` FS; B960 deleted the two dead derivatives variants), the coordinate becomes
`clamp(d2 * uLUTScale, 0.0, 1.0)`. The FS stays branchless.
- +inf becomes 1.0: the last texel, which is what llvmpipe already sampled.
  x86 is bit-identical:
  - local visual gate, 0 PIXEL MISMATCH in 15 frames;
  - probe `prod_saturate` vs `prod` on x86: 0 differing columns.
- A first version pre-scaled the vector instead (`pLs = pLv * 1/64`, coordinate
  `dot(pLs, pLs)`). It is correct too, and also x86-identical. It cost more GPU
  time on the Pi (below), so it was replaced.

## Pi -- the five gate zones, golden poses

The command was `gles2_zone_benchmark --drm` with the gate's exact args
(`review/B960/pi/gate_poses.tsv`, `--player-light-level 9`), on the Mesa prefix
(proven per run in `mesa_maps.log`, except three "before" runs whose maps
snapshot came too late). Each cell is the count of pixels differing by more than
32 from the committed x86 golden, out of 480,000.
- **Before**: CI ARM `11c8d976` (B960's binary). It reproduces B960's table
  exactly.
- **After**: this branch.
- **Light off**: B960's `--player-light-level 0` isolation.

| frame | before | after | light off (B960) |
|---|---|---|---|
| qeynos2 f0 / f1 / f2 | 0 / 2 / 2 | 0 / 2 / 2 | -- |
| gfaydark f0 / f1 / f2 | 23,842 / 202 / 4,531 | **429 / 66 / 163** | 409 / 54 / 162 |
| befallen f0 / f1 / f2 | 4 / 3 / 0 | 4 / 3 / 0 | -- |
| iceclad f0 / f1 / f2 | 19,495 / 32,245 / 6,904 | **2 / 29 / 335** | 2 / 21 / 6 |
| timorous f0 / f1 / f2 | 2 / 0 / 3,175 | 2 / 0 / 3,175 | -- |

**Bar: "light-on counts drop to the light-off order of magnitude in every
frame".** It is met in 5 of the 6 measured frames. **iceclad_f2 misses it: 335
against 6.**
- The 335 pixels lie on the edges of fan-shaped bands in the player light on the
  floor near the camera.
- Those bands are in the x86 golden too. The production FS reads a mediump
  `vWorldPos`, and at world coordinate ~7,000 it moves in steps on BOTH GPUs.
- Probe, far origin vs origin 0, the same light curve within 64 units:
  - x86 `prod` differs by up to 178;
  - `prod_highp` on x86 and `vs_plv` differ by 0.
- The two GPUs place the band edges a pixel apart.
- Removing the bands means computing pLv in the VS, which changes the blessed
  goldens: filed as **B967 (#285)**, class C.
- Crop: `review/B965/crops/iceclad_f2_pi_after__x86_golden__diff_gt32.png`.

timorous_f2's 3,175 is unchanged by the fix and is not the player light's
overflow. It shows as saw-tooth polygon edges. It is filed as **B969 (#287)**,
cause not isolated. No light-off run of timorous exists.

All three "after" builds (the pre-scaled one, the shipped clamp, and the clamp
after the fable merge) give the same >32 counts. Pi run-to-run noise is max 1.

## Pi -- frame time

Each run measures 60 frames: the gate's three poses x20, no dumps. The table
below is the clean A/B:
- one tree (this branch at the fable merge);
- one toolchain;
- the LUT clamp reverted for "nofix";
- zones interleaved nofix/fix in one Pi session.

`review/B965/pi_frame_time_ab.tsv`, columns p50 / p95 in ms:

| zone | total nofix | total fix | delta | GPU delta | CPU delta |
|---|---|---|---|---|---|
| qeynos2 | 31.54 / 32.51 | 31.83 / 32.84 | +0.9 / +1.0 % | +2.5 / +1.3 % | +0.1 / -0.8 % |
| gfaydark | 33.73 / 40.07 | 34.29 / 41.33 | +1.6 / +3.1 % | +2.7 / +3.9 % | +0.0 / +0.1 % |
| befallen | 24.57 / 42.00 | 24.77 / 42.22 | +0.8 / +0.5 % | +3.3 / +2.3 % | +0.3 / +0.2 % |
| iceclad | 27.30 / 27.80 | 27.92 / 28.46 | +2.3 / +2.4 % | +4.7 / +4.5 % | +0.2 / -0.0 % |
| timorous | 65.66 / 117.09 | 66.72 / 135.99 | +1.6 / **+16.1 %** | +2.7 / +40.1 % | +0.8 / +0.2 % |

- **p50 total is within 3 % in all five zones.**
- p95 is within 3 % in four zones; gfaydark is at +3.1 %.
- timorous p95 is +16 %, driven by its GPU tail. That tail is noisy across
  sessions: nofix/before p95 was 134.42 in session 2 and 117.09 here, while fix
  p95 was 137.05 / 135.62 / 135.99 in three sessions. It is not resolved.
- The clamp's own cost is GPU p50 +2.5..4.7 %, i.e. it is NOT free on Mali-400.
- The pre-scaled variant cost GPU p50 +4.5..9.3 %. That was in a different
  session against B960's binary, `pi_frame_time.tsv`.

Two earlier timing sessions compared against B960's `11c8d976` binary, a
different tree (`pi_frame_time.tsv`). In both, befallen showed CPU p50 +18-22 %
for the shipped build, reproduced back to back in a fourth session. The clean
A/B above does not show it (befallen CPU +0.3 %). It was not explained; the
build trees differ (fable merges) and so did the Pi's other tenants between
sessions.

## Not verified

- Live client on the Pi with `/plight` and a real lightstone. Only the benchmark
  poses and the probe were run. The client's FS sources are the same strings.
- Why Lima's LINEAR sampler maps +inf to the near end, at Mesa source level (ppir
  / texture-coordinate conversion). Not reported upstream.
- Mesa versions other than 25.2.8, and the system Mesa on the Pi (only the
  prefix was run).
- iceclad_f2 to the light-off order (B967), and timorous_f2 (B969).
- timorous p95 frame time (above).
- **CI ARM build: not produced.** `build-arm` job 1926 failed in 23 s because
  `downloads.sourceforge.net` answered 522 for the whole session; filed as
  **B970 (#288)**.
  - Every ARM binary here was built on dev-u24 with `scripts/build-arm-noble.sh`'s
    two docker steps, from a Dockerfile copy that takes `irrlicht-1.8.5.zip` from
    `/mnt/projects/irrlicht-1.8.5.zip` and libpng 1.6.37 from GitHub's tag
    tarball (`review/B965/README.md`).
  - Stripped, Mesa prefix via `scripts/pi/mesa_nollvm_select.sh`, in
    `review/B965/arm_bin/`.

## New findings (filed)

- **B967 (#285, C)**: the player light is quantized at large world coordinates
  on both GPUs (mediump `vWorldPos`). The goldens carry it; the fix needs a
  re-bless.
- **B968 (#286, C)**: past 64 units the player light gets BRIGHTER with distance
  on a surface facing it. The LUT's `1/d` clamps at 1/64 while `pLv` keeps
  growing, so pLn = d/64. Probe wall: 17 at 64 units, 105 at 400. Both GPUs; in
  the goldens.
- **B969 (#287, A)**: timorous_f2, 3,175 px >32 along polygon edges, Pi vs x86,
  not the player light.
- **B970 (#288, B)**: CI `build-arm` depends on downloads.sourceforge.net for
  the Irrlicht layer.

## Deviations

- **No CI ARM build** (B970). The ARM binaries are local docker builds of the same
  Dockerfile with local archive copies. The "before" binary is CI's `11c8d976`
  (B960) for the pixel table and the first timing sessions. The clean frame-time
  A/B uses a local "nofix" build of this tree.
- **Pi sessions: 5**, against the protocol's two.
  1. Probe x2, gate before x5, gate after (pre-scaled) x5 = 12.
  2. Timing before x5, after (pre-scaled) x5 = 10.
  3. Gate after (clamp) x5, timing x5, probe x1 = 11.
  4. befallen timing before and after = 2.
  5. Clean A/B timing x10.

  Each took and released lock `b965`. The Pi was left with the governor on
  ondemand and nothing in `/tmp`. B962 held the lock between sessions, and the
  unit waited for it each time.
- The fix changed mid-unit, from pre-scaled to clamp, after the first frame-time
  session (+4.5..9.3 % GPU).
- The trailer is `Claude Fable 5.1`, as dispatched.
