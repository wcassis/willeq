# B971: the orangepi window surface gets a 16-bit depth buffer, no stencil

Will's ruling (2026-10-01, in chat, ruling 2 of the hand-off #297), quoted
verbatim: **"orangepi uses a 16-bit depth buffer (no stencil user there).
Goldens will move; do it AFTER B967 lands."**

Issue: #289. Precondition met: B967 (MR !74) merged into fable (`2c7d5be4`,
2026-10-02); this branch cut from fable and re-merged to `7a621b9c` before work.

## What was measured

### The surface (the whole point)

The window surface's depth/stencil buffer is requested once, at device
creation, and its format is selected end to end by
`ConstrainedRendererConfig::enableStencilBuffer`:

1. `renderer_init.cpp` sets `params.Stencilbuffer =
   config_.constrainedConfig.enableStencilBuffer` (Irrlicht's public API);
2. the EGL device (`docker/irrlicht-drm/CIrrDeviceFB.cpp` on the Pi,
   `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` + `_X11.cpp` for the x86 fork the
   gate benchmark links) now chooses its `eglChooseConfig` from that request:
   **`Stencilbuffer` true → D24S8 (unchanged); false → EGL_DEPTH_SIZE 16,
   EGL_STENCIL_SIZE 0.**

Before B971 the device hardcoded D24S8 for every window/surfaceless/X11
surface and the orangepi preset had `enableStencilBuffer: true` — so the Pi
paid for a stencil buffer whose only consumer (`/portal` occlusion) is OFF in
the shipped config (the JSON block sets `portalOcclusion: false`), and the
`/portal` ON case would have mis-rendered (it would have gotten D24S8, which
is correct, but for the wrong reason: the flag was on to pay for a surface,
not to express that a stencil user is active).

**Size of the surface** (`[memgpu]` `logical_depth_stencil`, measured from
`glGetIntegerv(GL_DEPTH_BITS/GL_STENCIL_BITS)` at FBO 0 by
`tools/hwcaps/zone_bench_gpu_tally.h`):

| surface | bytes/pixel | 800x600 (x86 gate) | 1280x720 (Pi, card target) |
|---|---|---|---|
| D24S8 (before) | 4 | 1,920,000 (1.83 MiB) | 3,686,400 (3.52 MiB) |
| D16, no stencil (after) | 2 | **960,000 (0.92 MiB)** | **1,843,200 (1.76 MiB)** |
| saving | 2 | 960,000 | 1,843,200 (1.76 MiB) |

The 1,920,000 → 960,000 B step is observed in this branch's gate benchmark
logs (`golden_regen/run1/*.log`, e.g. `point=zone_parse ...
logical_depth_stencil=960000`); the 1280x720 Pi figure is the card's target
and is what the Pi sweep measures (section "Pi", below).

### The preset flag and the invariant

- `constrained_renderer_config.cpp` `fromPreset(OrangePi)`:
  `enableStencilBuffer = true` → `false`.
- `config/constrained_presets.json` orangepi block:
  `"enableStencilBuffer": true` → `false` (the JSON wins on top of the C++
  case, so both had to move; the JSON is the shipped source of truth).
- **New one-way invariant in `loadJsonOverrides()`: `portalOcclusion` on
  implies `enableStencilBuffer` on** (with a `LOG_WARN`). Portal occlusion is
  the ONLY stencil user in the tree, so a config that turns it on must get
  D24S8 back automatically — acceptance bullet 2 ("`/portal` with portal
  occlusion ON must still get its stencil") is now structural, not a
  two-key footgun. The implication is one-way (stencil on does not force
  portal on), so a stencil-less config stays stencil-less. The shipped
  orangepi block (`portalOcclusion: false`) is a no-op for it.

### The benchmark mirrors production

`tools/hwcaps/zone_bench_loader.cpp` set `params.Stencilbuffer = true`
hardcoded. It now reads
`ConstrainedRendererConfig::fromPreset(OrangePi).enableStencilBuffer`,
so the gate benchmark renders on the same surface the client ships (its
`logical_depth_stencil` row and its depth precision measure what ships).

### Diagnostics

`DiagnosticsComponent::togglePortalOcclusion()` (the manual `/portal` path;
the auto-enable paths were already gated on `enableStencilBuffer`,
`renderer_zone_load.cpp`) now returns with a `LOG_WARN` when the preset has
no stencil buffer, instead of silently building a portal system that can
never render.

## x86 goldens (regenerated per Will's ruling)

Regenerated with the gate's own invocation —
`scripts/b971/regen_goldens.py <worktree> <out>` calls
`scripts/ci/visual_gate.py run_benchmark()` per manifest zone, twice
(`run1` vs `run2` = **0 px in all 15 frames**, deterministic) — and installed
into `tests/goldens/*.png`. The committed goldens are B967's (D24S8 surface,
mesa 24.04.2); these are the B971 ones (D16 surface, **mesa 24.04.4** — see
the mesa note below).

**Caveat — mesa moved under us (isolated, inert):** the VM's mesa was
upgraded 24.04.2 → 24.04.4 between B967's bless and this run. To attribute
it, the B967 code (origin/fable `7a621b9c`, D24S8 device) was rebuilt on
mesa .4 and re-rendered with the same regen script
(`golden_regen_baseline/`): **0 px in all 15 frames** against the committed
B967 goldens (byte-identical PNGs, verified independently with `cmp`). The
mesa point release is therefore pixel-inert on this rig, and **every pixel in
the table above is the D16/no-stencil surface** — no mesa confound. The
surface itself is confirmed by the benchmark's own census on both trees:
`[memgpu] logical_depth_stencil` **1,920,000 B** (D24S8) → **960,000 B**
(D16, no stencil) at the gate's 800x600 — exactly the 2x of the bytes/pixel.

x86 BEFORE (committed B967 goldens) vs AFTER (B971), per frame
(`golden_regen/golden_delta.tsv`; >32 / >8 / any = pixels whose max channel
diff exceeds it, of 480,000; mean = mean abs over all RGB values):

| frame | >32 | >8 | any | max | mean | all >32 darker? |
|---|---|---|---|---|---|---|
| qeynos2_f0 | 18 | 231 | 460 | 111 | 0.008 | no |
| qeynos2_f1 | 69 | 110 | 114 | 152 | 0.007 | no |
| qeynos2_f2 | 44 | 433 | 572 | 66 | 0.014 | no |
| gfaydark_f0 | 0 | 369 | 8,313 | 25 | 0.033 | -- |
| gfaydark_f1 | 46 | 109 | 592 | 133 | 0.008 | yes |
| gfaydark_f2 | 0 | 92 | 4,488 | 12 | 0.017 | -- |
| befallen_f0 | 39 | 105 | 151 | 210 | 0.006 | yes |
| befallen_f1 | 57 | 76 | 92 | 111 | 0.008 | yes |
| befallen_f2 | 0 | 0 | 0 | 0 | 0.000 | -- |
| iceclad_f0 | 104 | 124 | 338 | 107 | 0.011 | no |
| iceclad_f1 | 75 | 134 | 62,574 | 95 | 0.117 | yes |
| iceclad_f2 | 21 | 21 | 91 | 88 | 0.003 | yes |
| timorous_f0 | 5 | 92 | 226 | 45 | 0.003 | no |
| timorous_f1 | 6,468 | 100,686 | 168,393 | 61 | 2.885 | yes |
| timorous_f2 | 66 | 886 | 74,407 | 149 | 0.341 | no |

>32 / >8 / any = pixels whose max channel diff exceeds it, of 480,000;
mean = mean abs over all RGB values; "all >32 darker?" = on the max-diff
channel of each >32 pixel, is the new frame darker? (`golden_regen/golden_delta.tsv`
carries the darker/brighter split per frame.)

**Z-fighting check:** no frame shows z-fighting. The largest >32 count is
timorous_f1 (6,468 px, mean 2.88): the diff is a broad low-frequency,
uniformly-DARKER band across the water surface in the lower half of the
frame (y=353–599, i.e. the bottom third; 15 clusters, largest 1,529 px; all
darker on the max-diff channel) — the D24→D16 depth-precision change on a
distant flat surface, the expected pixel signature of a coarser depth buffer. The other
>32 frames are small (≤210 px) and thin/clustered, not checkerboard.
Per-frame delta vs B967 goldens, and the mesa-isolation split, below.

## Manifest

`tests/goldens/manifest.json`: ONLY the `approval` block changed (Will,
2026-10-01, the ruling quoted at the top). No metric, memory baseline, CPU
baseline or memoryCoverage value edited — per the card's "edit NO
metric/memory/CPU/coverage value". Fields the golden-image run would bless
that differ from the committed manifest (`golden_regen/manifest_fields.tsv`):

- `memory.textureCacheBytesLimit` 67,108,864 → 22,020,096 in all five zones:
  NOT this unit (B978's 2026-10-01 texture-cache ratchet, 51 → 21 MiB; the
  field is display-only, never asserted). Left unchanged, not blessed.
- `blessedUnder.glVersion` 24.04.2 → 24.04.4: the VM mesa point release.
  The manifest's own note says a glVersion mismatch means "mesa was upgraded
  on this VM — re-bless with --bless, do not treat a mismatch alone as a
  regression." The goldens ARE the re-bless Will asked for, so the version
  string is updated to match what they were actually captured under.
  [left unchanged / updated — decide; default: update, it is the gate's own
  "mesa upgraded" bookkeeping, not a metric]

## Pi [to run after the x86 bar is met]

`[memgpu]` `logical_depth_stencil` at 1280x720 on qeynos2 via
`scripts/pi_rendered_zone_sweep.sh --zones qeynos2 --allow-vm-lane
--pi-dir ~/ci-arm/<sha> --owner kb-b971` with `EQEMU_HOST=172.16.200.99`:
expected 3,686,400 B → ~1,843,200 B.

Pi gate poses (z-fighting on the real GPU, the gate's own 5 zones × 3 poses):
`scripts/pi/publish_arm_build.sh` now also publishes
`bin/gles2_zone_benchmark` (+ `-nollvm`, Mesa-prefix rpath copy, same shape as
the B975 nametag tool) because the ARM benchmark was not published by CI;
`scripts/b971/run_gate_poses.sh` (copied from
`/mnt/projects/fable/review/B967/pi/run_gate_poses.sh`, output paths →
`review/B971/pi/gate/`) runs it against B967's poses.

## Local visual gate (x86/llvmpipe)

Against the installed B971 goldens: **PASS on all 15 golden frames and every
metric / memory-budget / ledger / memory-coverage assertion** — the only
failure is the iceclad p95 process-CPU ceiling, a host condition proven not to
be this change (see "Not verified", where the back-to-back A/B against the
unmodified fable code is recorded). `gate_run2.log` in
`~/kb-evidence/b971-depth16/`.

## Not verified

- **Pi** — sweep + gate poses not yet run (see "Pi"). The 1280x720
  3,686,400 → 1,843,200 B measurement, the Pi's own mesa prefix proof, and
  the 5-zone/15-pose z-fighting check on the real GPU are the remaining bar.
  Requires: push this branch → `glab ci trigger build-arm` → stage the
  published build → Pi lock → sweep + `scripts/b971/run_gate_poses.sh`.
- **/portal ON on the Pi with the new invariant**: covered by
  `test_constrained_renderer` (the invariant flips `enableStencilBuffer` on
  and the D24S8 surface math), NOT exercised on the Pi (it would re-bless a
  D24S8 surface this card deliberately removed).
- **Local gate's CPU ceiling (host condition, not this change)**: the local
  gate run passed all 15 pixel frames and every metric/memory/ledger
  assertion, and failed ONLY on iceclad p95 process-CPU 11.97 ms > the 9.53
  ceiling. A back-to-back A/B on this host showed the UNMODIFIED fable code
  (D24S8, origin/fable) failing it equally (12.0–12.3 ms in the same minute
  vs B971's 12.3–12.4 ms; a quiet load window gave B971 11.05 vs baseline
  11.80), so the breach is a host condition (a `node test/run.js` from a
  sibling session sat at 108 % during the runs), not a B971 regression. Same
  class B967's README recorded (CPU ceilings under load, pixels clean, merged
  green on CI). The CI pipeline (this host, quieter) is the authority on it;
  a repeat there would be a host-load retry, per the gate's own docstring.

## Evidence

Raw logs/TSVs/PNGs: `~/kb-evidence/b971-depth16/`
(`golden_regen/`, `golden_regen_baseline/`, `sheet/`,
`irrlicht_build2.log`, `build_gles2_r2.log`, …). Committed here: the delta
tables.
