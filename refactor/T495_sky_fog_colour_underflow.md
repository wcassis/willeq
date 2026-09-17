# T495 — B789/B790: the sky/fog colour lerp underflows unsigned channels (one defect, not two)

Class A, both Will-ruled bugs 2026-09-16. **They are ONE defect.**
`SkyRenderer::interpolateSkyColors()` (`src/client/graphics/sky_renderer_colors.cpp:182-215`)
lerps each channel as `a.getRed() + (b.getRed() - a.getRed()) * t`. `SColor::getRed()`
returns `irr::u32`, so on every ramp where a channel DECREASES the difference wraps to
~4.29e9; `* t` is done in `float` (24-bit mantissa zeroes the low bits); the
`static_cast<irr::u32>` then `& 0xff` leaves a channel of 0, 64, 128 or 192 — garbage.
That garbage is consumed three ways: the dome vertex tint, `currentClearColor_`, and
`getRecommendedFogColor()` → `IrrlichtRenderer::updateTimeOfDay()` → `driver_->setFog()`
→ `uFogColor`, which every fogged pixel is mixed toward. So the sky (B789) and the
"pink trees and distant blocks" (B790) are the same arithmetic reaching two consumers.

**Lane: OFFLINE.** Worktree `wt/t5`, branch `task/T495-sky-colour-interp` from fable.
Merge AFTER the T490/T491 recovery chain (Will's ordering), rebasing then.

## Evidence (planner, 2026-09-17)

- [measured] Emulating the function in float32 reproduces the corpus exactly. Game time
  at zone-in in `review/T492/llmbench/players/p2/run.log` is **17:25**; the emulator's
  tint at 17:25 is **(161,58,58)** and `currentClearColor_` = tint/2 = **(80,29,29)** —
  the flat colour filling B789's sky band AND B790's tree band (35,748 of 36,000 px in
  the sky crop; 33,893 px in the tree crop; frames
  `llmbench-v1-camera-steps-bad/players/p2/frames/15:38:13.227_…` and
  `15:46:54.538_H_cam_p2_qeytoqrg_chase_window.png`). Exact match, all three channels.
- [measured] Leg E game time **16:49**; emulated fog at 16:45/16:50 = **(171,180,0)**;
  the far field in `llmbench/players/p2/frames/16:43:43.983_H_cam_p2_qeytoqrg_chase_window.png`
  measures (170.0,178.0,0.75), blue exactly 0 over the sky and distant trees.
- [measured] **Nothing is magenta.** The only magenta in all three corpora is the
  target-highlight outline. B790's "pink/magenta" wording is wrong; its evidence frame's
  mean (88,35,38) is dark red. fable's real placeholder is a 0xFFFF00FF checkerboard
  (`ConstrainedTextureCache::getPlaceholderTextureInternal()`), mean ≈ (143,16,143),
  R≈B — no corpus band shows that.
- [measured] **EQE1 not implicated**: the runs loaded 1 EQE1 entry (UI atlas) + sky.eqe1;
  zone/tree textures came from S3D; "0 failed, 0 with missing textures". Will's EQE1
  all-or-nothing rule is not engaged.
- [measured] The official frames paired in B789/B790 show a wall/ground with the Tip
  dialog, no sky — they are not an oracle for sky colour.
- [measured] fable renders a correct blue sky with clouds at a daytime hour
  (`llmbench-legI/.../19:53:26.694_…`): 11:00-16:00 and 21:00-04:00 are constant arms.
- [measured] `tools/hwcaps/gles2_zone_benchmark.cpp` has no SkyRenderer/setFog: the
  golden gate cannot see either defect.

## Tasks

1. **Red test for the table (pure, no device).** `tests/test_sky_color_interpolation.cpp`:
   drive `calculateSkyColors()` (or an extracted `skyColorAt()`) across 05:00-21:00 at
   5-minute steps; assert per channel monotone between the two anchors and within
   `[min(a,b), max(a,b)]`. Must FAIL now at 16:15 (zenith B 0 between 220 and 210),
   17:25 (tint (161,58,58)), 18:30 (all anchors collapse to 0). Pin the corpus triples
   as named cases: 17:25 tint (161,58,58)/clear (80,29,29); 16:45 fog (171,180,0);
   18:30 tint (0,0,0).
2. **Red test for the fog consumer (offline, headless GLES2).** Extend
   `tests/test_sky_dome_tint_channel_order.cpp`'s harness (real dome, real assets,
   self-skips without a device): at 16:49 and 17:25 read `getRecommendedFogColor()`
   and assert it is inside the anchor hull; then a band test rendering the dome at
   those hours asserting sky-band blue ≠ 0 and red ≤ 2× the other channels. Bounds come
   from the TABLE's anchors, not the official frames.
3. **Fix at the root.** Rewrite `interpolateSkyColors()` to lerp in float:
   `static_cast<irr::u32>(std::lround(std::clamp(af + (bf - af) * t, 0.0f, 255.0f)))`,
   one `lerpChannel` helper for all nine lines. Do NOT touch the table's values (B781),
   `toGles2VertexColor()`, or the driver (B780 is Will's).
4. **Sweep the pattern.** `detail_texture_atlas_flora.cpp:199` (`getRed() - 30` wraps
   for channels < 30) — fix the same way or file it; say which and why.
5. **Golden impact — verify, then STOP.** Run `scripts/ci/visual_gate.py` unchanged.
   [assumed] zero pixel diff (the benchmark builds no sky/fog stack). If any golden
   moves, STOP and report — re-blessing is pre-approved only for T279.
6. **Board.** Strike B789 and B790 as one defect with the numbers above; correct B790's
   record (no magenta, no missing texture, EQE1 not implicated); narrow B781 to "are the
   anchor values right"; leave B782/B783/B780/B726 alone. Add: the flora-atlas underflow
   (if not fixed), "the corpus's official frames show no sky" (B), "SkyRenderer logs
   nothing about the colour it chose" (C) — this unit had to emulate the function to
   identify the hour.

## Rules
- Offline only: no lock, no live client, no VM, no Pi. Private scratch `scratchpad/T495/`.
- No ccache on this host; build -j8; `review/` is beside the checkout.
- Evidence (before/after crops at 16:49 and 17:25 from the headless harness, the
  emulator script, the gate log) to `review/T495/` with a README.
- Commit code, tests, board, plan Review only. Do not merge.

## Review
(executor)
