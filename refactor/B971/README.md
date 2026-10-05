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

## Pi

Rendered sweep (qeynos2, 1280x720, `--mesa nollvm`), build
`f4c6f201a1efc9a830883784e18470571f1d085d` staged from
`/mnt/projects/fable/ci-arm/<sha>` (the new `publish_arm_build.sh` publish),
`EQEMU_HOST=172.16.200.99` VM lane: **1/1 pass, snapshot restored
byte-identically, locks released**
(`~/kb-evidence/b971-depth16/pi/sweep/`).

**The surface's `logical_depth_stencil` stays 3,686,400 B on the Pi** — but the
client IS requesting the D16/no-stencil surface, and the Pi's driver is not
giving it. The device log line (the same one the x86 fork prints) reads:

```
EGL: Config depth=24 stencil=0
```

The `stencil=0` proves `params.Stencilbuffer=false` reached
`eglChooseConfig` (the B971 code path executed); the `depth=24` is the
driver's ANSWER — the Mesa 25.2.8 / Lima config set for this rig offers no
16-bit depth window config, so the 24-bit one is chosen and the census counts
it: `depthStencilBytesFor(1280, 720, 24, 0)` = 4 B/px = 3,686,400 B
(`depth>16 or stencil>0` → 4 bpp, `gpu_buffer_census.h`). On the x86 gate,
llvmpipe DOES offer a 16-bit config for the same request, which is why the
same binary reads 960,000 B (800x600, D16) there.

**Consequence: the ~1.76 MiB saving is NOT observable on the current Pi rig.**
Will's ruling asked for the surface to STOP requesting the stencil, and that
is done and verified in the request path (config → `params.Stencilbuffer` →
`eglChooseConfig`) — the x86 gate proves the whole chain end to end on a
driver that offers 16-bit depth. Whether the Pi's driver ever offers one is
outside this unit: if a future Mesa/Lima update exposes a 16-bit depth config,
the same binary will pick it up without any further change (the request is
already there); if not, the row stays 3.52 MiB and the row is correctly
labeled. A D24S8 vs D24/no-stencil A/B on this rig would measure ZERO
difference, because Lima stores both in the same 32-bit word (the census
formula's comment says so) — so there is no Pi-side byte saving to report,
and none is claimed.

Pi gate poses (z-fighting on the real GPU, the gate's own 5 zones × 3 poses):
**DONE** (Pi lock taken 17:46 UTC after `kb-b945` released; `run_gate_poses.sh`
ran `bin/gles2_zone_benchmark-nollvm` from the published build dir, nollvm Mesa
prefix confirmed in the process maps). All 5 zones rc=0; `EGL: Config
depth=24 stencil=0` in every bench.log — the same surface the sweep measured.
Compared against B967's Pi frames (`review/B967/pi/gate/*_after`, same Mesa
25.2.8 nollvm, `EGL: Config depth=24 stencil=8`): **0 px in all 15 frames —
byte-identical** (`pi_delta.md`). Dropping the stencil changed no pixels on
the real GPU, exactly as Lima's shared 32-bit word predicts (depth precision
is 24-bit in both runs; the portal was off in both). No z-fighting, no
defect. (`~/kb-evidence/b971-depth16/pi/gate/` vs `.../pi/b967_gate/`.)

## Local visual gate (x86/llvmpipe)

Against the installed B971 goldens, and on CI (pipeline #510, fully green
2026-10-05 17:04 UTC — all jobs passed): **PASS on all 15 golden frames and
every metric / memory / ledger / memory-coverage assertion**, including the
iceclad p95 process-CPU ceiling on the CI host. The local run's single
failure (iceclad CPU 11.97 > 9.53) was a host condition proven not to be this
change — a back-to-back A/B showed the unmodified fable code (D24S8) failing
the same ceiling equally on this loaded host (see the earlier "Not verified"
note). `gate_run2.log` in `~/kb-evidence/b971-depth16/`.

## Not verified

- **Pi byte saving** — NOT achieved on the current rig, and NOT claimed: the
  Mesa 25.2.8 / Lima config set offers no 16-bit depth window config, so the
  surface reads back depth=24/stencil=0 and `logical_depth_stencil` stays
  3,686,400 B (see "Pi"). The REQUEST is in and correct (x86 gate proves the
  chain end to end on a 16-bit-capable driver); the driver not offering the
  config is outside this unit.
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
