# B960 -- which production draws reached the built-in alpha test on the Pi

GitLab #278 (class B). Branch `task/B960-alpha-test-reach`, cut from `fable`
`c9f8ffb4` (B958 merged). Raw evidence: `/mnt/projects/fable/review/B960/` (its
README lists every file).

No budget, golden, baseline or gate assertion changed.

## Answer

Before B958, the Pi drew the built-in `AlphaTest3D` program inverted. In
production two things reach that program:

1. **The inventory paperdoll (`CharacterModelView`), for a character whose model
   has a WLD-masked material.**
   - Measured live on the Pi with the playable Iksar (race 128, `Oracleecho`):
     opening the inventory drew `ikmhe0102.bmpmasked` through AlphaTest3D in 59
     frames per run. That is the head texture, at the mouth.
   - The world drew 0 AlphaTest3D draws in the same run.
   - Before the fix, the opaque mouth texels were dropped and the background showed
     through (crop below).
   - None of the playable races 1-12 has a masked base material. The Iksar does.
2. **A newly built masked entity, for one frame** (filed as **B966**, #284).
   - Measured live in qeynos2: `a_whiskered_bat` drew `batft0001.bmpmasked`
     through AlphaTest3D once in 1,137 frames, during its build.
   - `SceneNodeCreation` puts the node in the scene and `NodeSetup` promotes its
     materials a frame later.

Nothing else was measured reaching it. In 10 of the gate's 15 golden poses on the
Pi, plus live qeynos2 / gfaydark / cabeast rendering outside those two cases, the
count was 0. With the knob, the gate frames are bit-identical before and after.

## Instruments added

These are test knobs: they are never on by default.

- `WILLEQ_ALPHATEST_FWIDTH=1` (`docker/irrlicht-drm/COpenGLES2Driver.cpp`
  `initDriver()`, `COGLES2Shaders.cpp`) compiles the pre-B958 fwidth() sources,
  byte-identical, only when `GL_OES_standard_derivatives` is exposed. That is the
  old switch, so "before" and "after" are one binary.
- `WILLEQ_ALPHATEST_TRACE=1` prints to stderr:
  - each texture AlphaTest3D draws, once each;
  - every frame that drew with it (the first 200);
  - every 60th frame;
  - an atexit summary.
- `scripts/pi_rendered_zone_sweep.sh` gained `--dump-every/--dump-max` and
  `--account/--char`. The defaults are unchanged.

## Census (static, then confirmed by the trace)

The driver selects AlphaTest3D in `COpenGLES2Driver::applyMaterialState()` for
a built-in `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` material with a texture. Custom
shader ids return earlier. `AtlasAlpha3D` has no selector (B958).

| draw | shader at draw time | affected on Lima before B958 | reach |
|---|---|---|---|
| zone regions, placed objects, doors | zone shader custom alpha-test (constant 0.5 on GLES2 since long before B958) | no | the built-in program is used only when `ZoneShaderManager` is absent: a preset without `enableShaders` (voodoo1/2/custom), or a solid/alpha compile failure, or for a non-ETC1 masked texture if the RGBA variant failed. orangepi never takes this path. |
| trees (wind) | zone wind alpha-test | no | same |
| in-world entities (NPC/PC, corpses), held weapons | zone shader after `applyAlphaTestMaterialType()` | **one frame per new masked entity** | B966 |
| **paperdoll body + paperdoll weapons (RTT)** | **built-in AlphaTest3D**: `character_model_view.cpp` never promotes | **yes** | when the inventory popup is open AND the player's model or item has a masked material. Measured: Iksar head. Human male + the ten tool test items: 0 draws (x86 `ui_inventory_capture`). |
| name tags | `CTextSceneNode` -> font sprite batch -> UI2D | no | -- |
| sky, sun/moon, spell FX, particles, detail objects, weather, boids, footprints | VERTEX_ALPHA / ADD_COLOR / ALPHA_CHANNEL (blend) / SOLID | no | -- |
| UI 2D | UI2D / Color2D | no | -- |
| impostors, `--no-entity-shader-promotion`, entity viewer RGBA leg | built-in AlphaTest3D | yes | benchmark/tools only |

Other derivative + discard patterns in production GLSL: none.
`zone_shader_sources_gles2.h` held two fwidth() alpha variants
(`*_ALPHA_SRC_DERIVATIVES`). They had no caller and are deleted here. The
driver's other built-in programs and the zone, wind and LW shaders all discard on
a constant 0.5.

## Pi measurements

The Pi is an Orange Pi One with Lima on the shipped LLVM-less Mesa 25.2.8 prefix.
Each run's maps show `libgallium-25.2.8.so` and no libLLVM. The ARM build is CI
`11c8d976` of this branch.

### Gate golden poses (`gles2_zone_benchmark --drm`, the gate's exact args)

- AlphaTest3D draws: 0 in every run, with the knob both ways.
- Pi before vs Pi after: **max abs 0, 0 differing pixels in all 15 frames**.

Pi after vs the x86 golden: per-frame counts of pixels that differ, and of those
differing by more than 32.

| frame | differing | >32 | frame | differing | >32 |
|---|---|---|---|---|---|
| qeynos2_f0 | 250,912 | 0 | iceclad_f0 | 219,354 | 19,495 |
| qeynos2_f1 | 335,108 | 2 | iceclad_f1 | 241,559 | 32,245 |
| qeynos2_f2 | 288,257 | 2 | iceclad_f2 | 177,336 | 6,904 |
| gfaydark_f0 | 238,690 | 23,842 | timorous_f0 | 252,067 | 2 |
| gfaydark_f1 | 200,302 | 202 | timorous_f1 | 231,282 | 0 |
| gfaydark_f2 | 146,957 | 4,531 | timorous_f2 | 353,684 | 3,175 |
| befallen_f0 | 312,038 | 4 | | | |
| befallen_f1 | 314,542 | 3 | | | |
| befallen_f2 | 288,647 | 0 | | | |

Pi before vs x86 is identical to this table, because before = after. **The fix
does not move the Pi gate frames toward x86. Nothing there reached the inverted
path.**

The large rows come from the per-pixel player light. Isolation with
`--player-light-level 0` on both sides gave these >32 counts: gfaydark 409 / 54
/ 162, iceclad 2 / 21 / 6. The cause is not isolated; it is filed as **B965**,
#283, and is not the derivative/discard class.

### Masked-material draw, deterministic (P47 entity viewer, RGBA leg = built-in AlphaTest3D, 4 angles)

The rows are pixels differing by more than 32, out of 480,000.

| race | Pi before vs after | Pi before vs x86 | Pi after vs x86 |
|---|---|---|---|
| 128 Iksar (`ikmhe0102`, mouth) | front 46, right 0, back 43, left 86 | 46 / 0 / 43 / 86 | **0 / 0 / 0 / 0** |
| 60 skeleton (5 masked textures) | 16,851 / 17,489 / 17,250 / 14,695 | 16,837 / 17,494 / 17,245 / 14,683 | 40 / 46 / 33 / 46 |

After the fix, the Pi matches x86 where it did not before. Crops: the Iksar mouth
holes and the skeleton legs missing before the fix
(`review/B960/crops/ev_r*_crop4x.png`, panels before | after | |diff|x4).

### Live (rendered client, local eqemu lane, 800x600, `/inventory` tour)

| run | AlphaTest3D draws | textures |
|---|---|---|
| Iksar `Oracleecho`, cabeast, fixed | 59 frames / 59 draws in 1,859 frames, all while the inventory was open | `ikmhe0102.bmpmasked` |
| same, pre-B958 knob | 59 / 59 in 1,941 frames | same |
| Testchar (human), qeynos2, fixed | 1 draw in 1,137 frames, during entity build (B966) | `batft0001.bmpmasked` |
| Testchar, gfaydark, fixed | 0 in 1,402 frames, including inventory open | -- |

The paperdoll idles through an animation, so frames from the two live runs are
not pose-matched and are not diffed per pixel. The crop sheets
`review/B960/pi/live/crops/iksar_paperdoll_k{0,1}_*.png` are for looking at. The
pixel numbers are the entity viewer's above, which uses the same program and the
same texture.

### What a Pi player saw wrong before B958 (measured)

- An Iksar's paperdoll had holes where the mouth texture is opaque. In the
  viewer this is 43-86 px at 800x600 per angle; in the paperdoll's small render
  target it is a handful of pixels.
- Any masked NPC drew inverted for its first frame after being built.
- A player model of a heavily masked race would lose most of the masked parts in
  the paperdoll (skeleton: ~15-17k px per view in the viewer). This race is not
  playable, and illusions were not tested.
- Nothing in the world otherwise.

## x86

- x86 `ui_inventory_capture` (human male, DAF, IT1/4/5/10/45/48/157, shield):
  0 AlphaTest3D draws, with the knob both ways.
- The entity viewer RGBA leg found masked base materials only for races 60 and 128
  among 1-12, 60, 128.
- Races 130, 330 and 522 have no model in this asset set.

## Not verified

- Illusions, and equipment beyond the ten tool items (helm eyeholes): whether any
  playable appearance other than the Iksar head reaches AlphaTest3D.
- Whether the paperdoll gets the ETC1 companion for a masked ETC1 texture. The
  built-in AlphaTest3D reads only `uTexture.a`. The Iksar texture came through as
  `*.bmpmasked`, with its cutout correct on the Pi after the fix.
- B965's cause (FP16 attenuation LUT is a lead only).
- B966's visibility to a player, and its Solid3D half.
- Mesa versions other than 25.2.8.

## Deviations

- The census subagent was read-only, and its table was checked against the trace.
- Pi: three sessions under lock `b960`, in this order:
  - gate 10 invocations plus 2 isolation runs, plus one `gpu_texture_formats` run
    (unused: it creates a desktop-GL context);
  - entity viewer, 4 invocations (a first attempt of 4 failed on a missing
    `willeq.db` path and was rerun);
  - live, 4 client runs via the sweep rig.
- The `Oracleecho` account (`willeq_e`) was used for the Iksar, with the lane's
  shared test password.
- The dispatch's trailer was `Claude Fable 5.1`, and the commits use it.
