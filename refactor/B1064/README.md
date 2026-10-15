# B1064 (#384): the target outline without a stencil buffer

**Defect.** `OverlaysComponent::drawTargetOutline()` was a two-pass stencil
rim (pass 1 wrote ref 128 over the model, pass 2 drew the model scaled 1.10
about its origin where stencil != 128). B971 made the window surface follow
`enableStencilBuffer`, which is false in every preset of
`config/constrained_presets.json`, so the surface has no stencil planes, the
stencil test always passes, and pass 2 painted the whole inflated model:
Will's XU4 screenshot (`/mnt/projects/fable/review/B1064/xu4_dionna_qeynos_2026-10-14.png`)
and the x86 reproduction below. The outline was the stencil user B971 did not
count.

**Fix.** An inverted hull that touches no stencil state
(`include/client/graphics/overlays/target_outline_pass.h`,
`src/client/graphics/overlays/target_outline_pass.cpp`):

1. a depth-only pre-pass of the unscaled mesh (colour writes off, depth write
   on, LESSEQUAL) -- the target's own depth, also when the scene drew it as an
   impostor billboard (without it the hull painted solid again on x86, see
   "Before/after");
2. the 1.10-scaled mesh with front faces culled, depth LESSEQUAL, depth write
   off, the same hue cycle and the same `normalTransform` mirror of
   `EQAnimatedMeshSceneNode::render()`.

Front-face culling and the colour mask are raw GL undone before return, so
the driver's state cache (cull off / GL_BACK, colour mask on, depth write on)
stays true; the particle renderer's saved/restored stencil state
(`unified_particle_renderer.cpp`) is untouched because the outline no longer
enables or disables the stencil test. B971's stencil-off surface stands
(`docs/decisions.md`, addendum under 2026-10-05 B971).

**Pin.** `tests/test_target_outline_pass.cpp` (registered in
`tests/cmake/renderer_guards.cmake`): `plan()` never requests the stencil for
any stencil-bit count and always plans the pre-pass + front-culled hull; the
hull transform against hand-computed matrices; a scan of
`drawTargetOutline()` that fails on any stencil call. Run against fable
ac32a25e's `overlays_component.cpp` it fails
(`/mnt/projects/fable/review/B1064/x86/unit_test_vs_fable_source.txt`); with
the fix 6/6 pass.

**Also.** The x86 surfaceless EGL path now logs `EGL (surfaceless): Config
depth=%d stencil=%d` (`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp`), the line
the DRM path already printed, so a headless run shows what B971 gave it.

## Before/after (x86, eqemu lane 2)

All in `/mnt/projects/fable/review/B1064/x86/` (README there has every file).

| run | EGL (logs/) | result |
|---|---|---|
| fable, orangepi | `Config depth=16 stencil=0` | solid silhouette |
| fix, orangepi | `Config depth=16 stencil=0` | rim |
| fable, stencil forced on (scratch preset copy) | `Config depth=24 stencil=8` | old stencil rim (reference) |
| fix, stencil forced on | `Config depth=24 stencil=8` | rim |

Rim width, Captain_Rohand from one fixed spot, outline-colour mask
(`review/B1064/x86/rim_width.txt`, `tools/rim_measure.py`): old stencil rim
median left/right run 4/3 px, 5340-5549 mask px; the fix on stencil 0 5-4/3 px,
6204-6421 px; fable on stencil 0 30-28/11-10 px, 18505-20299 px (the
silhouette). The factor 1.10 is kept: the hull rim reads the same width.

The first hull (no pre-pass, 358bab17) painted solid while the target was an
impostor: `review/B1064/x86/hull_without_prepass_target_not_in_depth.png`,
log `[impostor] frame=487 candidates=2 full=0 impostor=2`
(`review/B1064/x86/logs/arathur_after.log`). Commit 4f9fbdbe adds the pre-pass.

## Device (Orange Pi One, lane 1)

`/mnt/projects/fable/review/B1064/device/orangepi_lane1_rohand_targeted_rim_f015.png`
(and f014; untargeted f013): `ci-arm/66b1c1fd6cd74679606417fc6045c0594cfab892/bin/willeq-nollvm`, Mali400,
`--drm --renderer gles2 --constrained orangepi -r 1280 720`, log
`EGL: Config depth=24 stencil=0` and `Target outline: inverted hull (surface
stencil bits=0, uses stencil=false, scale=1.10)`
(`review/B1064/device/client_excerpt.log`): a rim around Captain_Rohand.

## Not verified / left open

- The pre-pass's effect on the impostor-target case was not observed live:
  the condition did not recur in the run with the pre-pass. Why a target
  (pinned by `impostor_policy.h`) was an impostor for ~8 s is not established.
- Inverted-hull artifacts: thin interior slivers where the mesh has gaps
  (helmet slits); the stencil rim had none. The pre-pass also leaves the
  target's depth in the buffer for the rest of the frame where the scene drew
  an impostor (target ring, particles behind it are clipped to the mesh).
- No oracle comparison (the official client's highlight shape/colour) was
  taken; the look is the old rim's.
- The XU4 (Panfrost) was not used (Will play-testing); lane 3 was not run.
- The issue's "x86 dev builds keep a stencil" is not true on this tree: the
  default preset measured stencil=0 too.
