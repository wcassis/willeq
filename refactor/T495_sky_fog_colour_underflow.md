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
Done offline in `wt/t5`, branch `task/T495-sky-colour-interp` from `60ced614`.
All six tasks. Nothing was blocked and nothing needed Will.

### The defect, confirmed [measured]

The diagnosis in the plan is right and I reproduced it through the PRODUCTION
code rather than an emulator: on the pre-fix commit `fd837336`, driving the real
`SkyColorsAt()` / `SkyLayerTint()` at the corpus's own game times gives tint
**(161,58,58)** and clear colour **(80,29,29)** at 17:25, fog **(171,180,0)** at
16:45 and **(172,180,0)** at 16:49, and **(0,0,0)** for all three colours at
18:30. Those are the plan's emulated numbers exactly.

I re-measured the corpus crops myself rather than take them on trust
[measured]: **(80,29,29) occupies 35,748 of the 36,000 px of B789's sky crop
and 33,893 of the 96,000 px of B790's tree band** — so the flat colour in BOTH
bands is `currentClearColor_`, the scene background showing through wherever the
dome or the geometry is not, not the fog and not a texture. Zero pixels in
either crop are magenta (R,B > 120 with G < 80). The corpus game times are
`[ZONE] Time of day: 17:25` (zone-in) and `16:49` (Leg E), read from the run
logs [measured].

### Two of the plan's evidence claims did not survive

* **The frame paths in the plan and on the board are wrong** [measured]. B789
  and B790 cite `review/T492/llmbench/players/...`; the frames are under
  `review/T492/llmbench-v1-camera-steps-bad/players/...`. Corrected on the board.
* **"EQE1 not implicated: the runs loaded 1 EQE1 entry (UI atlas) + sky.eqe1;
  zone/tree textures came from S3D" is FALSE** [measured]. `EQE1-USAGE` in
  `p2/run.log` reports **8 archives open, 48,696 KB mapped, 511 lookups**,
  including `qeynos2` (152 entries) and `qeynos2_obj` (110) — the zone and
  object textures came from EQE1. "Loaded EQE1 archive config: 1 entries" is the
  config file, not the loaded set. The conclusion (this is not a texture
  failure) is unaffected and independently supported — 0 wasted opens, 0 idle
  zone archives, no placeholder colour anywhere in the crops — but the sentence
  itself is now struck on the board rather than repeated. Similarly, "0 failed,
  0 with missing textures" is the DOOR rebuild line, not a whole-zone texture
  census.

### Tasks

1. **Red test for the table** — `tests/test_sky_color_interpolation.cpp`, 8
   cases, pure (no device, no assets, no scene manager). To make that possible
   the table and its arithmetic moved WHOLE out of `SkyRenderer`'s two private
   methods into `src/client/graphics/sky_color_table.cpp` +
   `include/client/graphics/sky_color_table.h`; `SkyColorSet` moved with them
   and `calculateSkyColors()` / `interpolateSkyColors()` are now two-line
   delegations. The dome tint and the clear colour moved too (`SkyLayerTint()`,
   `SkyClearColorFromTint()`) — they are the other two consumers and
   `updateSkyLayerColors()` held the only copy. **RED on `fd837336`: 7 of 8
   failed**, first hull failure at 07:05 (horizon red 168 between 255 and 200,
   then 0 at 07:20), plus every named case above.
2. **Red test for the fog consumer** — two cases added to
   `tests/test_sky_dome_tint_channel_order.cpp` on its existing headless-GLES2
   harness (real `sky.s3d` + `sky.ini` + `sky.eqe1`, real dome, same self-skip).
   `FogColourAtTheCorpusHoursIsInsideItsAnchorHull` RED: 16:49 fog
   (172,180,**0**) against blue anchors 200 -> 190, 17:25 fog (177,**128**,128)
   against green anchors 180 -> 140. `DomeAtTheCorpusHoursIsNotRed` RED in
   PIXELS: 16:49 dome **(91.66,120.02,0.00)**, 17:25 dome
   **(92.24,38.86,43.09)** — red 2.4x green. Bounds are the table's anchors, as
   the plan required; nothing here reads an official frame.
3. **Fix at the root** — one `lerpChannel()` helper for all nine lines: both
   endpoints to `float` before subtracting, then `std::clamp` to 0-255 and
   `std::lround`. Anchor values, `toGles2VertexColor()` and the driver all
   untouched. **GREEN: 8/8 and 5/5.** 17:25 tint (161,151,156), clear
   (80,75,78); 16:49 fog (172,180,192); 18:30 zenith (105,95,150); 16:49 dome
   **(91.66,120.02,150.85)**, 17:25 dome **(92.24,101.25,115.97)**. 11:00 is
   byte-identical (140,175,220), so T484's pixel case does not move.
   **Red is identical before and after at both hours** — only channels that were
   DECREASING across their segment were ever corrupted, which is the signature
   of the underflow and is itself worth keeping in mind for B780.
4. **Sweep** — FIXED, not filed. `detail_texture_atlas_flora.cpp` `drawShell()`
   was the only other instance, and it is two lines. It was producing CORRECT
   pixels: all three shell base colours are >= 180 in every channel, so nothing
   underflowed; the clamp is a guard against a future darker colour, and
   `TexturesReproducible` is GREEN, so `data/textures/detail_atlas.bmp` did not
   move and no pin had to be rewritten. Census [measured]: a grep for a
   subtraction involving `get{Red,Green,Blue,Alpha}()` over `src/`, `include/`,
   `tools/` and `benchmarks/` returned exactly ten hits — the nine lerp lines and
   this one — and now returns none outside a comment. Recorded as B806, struck on
   arrival, so a swept pattern leaves a trace.
5. **Golden impact** — `scripts/ci/visual_gate.py` unchanged: **PASS, exit 0,
   zero pixel diff**, all three zones, plus the memory-coverage run (I symlinked
   `data/eq_client` and `data/eqe1_client`, both gitignored, so the gate had its
   assets; without the second it SKIPs the coverage half). The plan's
   `[assumed]` zero diff is now `[measured]`. **The STOP condition did not fire
   and nothing was re-blessed.** Note the reason this defect survived: the gate's
   `gles2_zone_benchmark` builds no SkyRenderer and calls no `setFog()`, so the
   goldens could not see it — a real coverage hole, not a lucky pass.
6. **Board** — B789 and B790 struck as ONE defect with the numbers above; B790's
   record corrected on four points (no magenta, the missing-texture lead is
   dead, the EQE1 sentence was wrong, and the frame paths); B781 narrowed to "are
   the anchor values right" with the note that its 11:00 figure sits on a
   constant arm and is unaffected; B782/B783/B780/B726 untouched. Added B806
   (flora, struck on arrival), **B807** (B — the corpus's official frames contain
   no sky, so they cannot answer B781; B789's official crop measures
   (19.31,20.55,19.12), near-black) and **B808** (C — SkyRenderer logs neither
   the hour nor the colour it chose, which is why this unit had to emulate the
   function to find the hour).

### Verification

* Full ctest on `build` (Release, GLES2, tests on): **4,840 registered, 4,839
  passed, 1 failed, then 0**. The one failure was `DocCitations` on a board
  citation I wrote (`renderer/irrlicht_renderer.cpp` instead of its full path);
  fixed and re-run green. No test outside this unit changed state. The parallel
  run needed no exclusions — `RDPServerFrameTest.UpdateFrameMultipleTimes`
  passed in the `-j8` run.
* Every build verified by `BUILD_EXIT`/exit file, never by grepping the log.

### `[assumed]` in this Review

* That `DocCitations` was green on `60ced614` before this unit. Not re-run on
  the base commit; the failure it reported was a line this unit added, and it is
  green now.
* That the two `DomeAtTheCorpusHoursIsNotRed` bars (blue > 10, red <= 2x the
  others) are the right SHAPE to pin. They are deliberately loose — they encode
  "not the B789/B790 symptom", not a colour match, because a colour match would
  be asserting B781's unmeasured anchors.
* That nothing outside `SkyRenderer` consumed the pre-fix garbage in a way that
  was compensating for it. Grep found the three consumers named in the plan and
  no fourth, but no live client was run (offline lane).

### Not done here

B781 (are the anchors right), B782 (pale trees), B783 (the live frame at T478's
pose) and B780 (the driver's R/B convention) all still need the oracle or Will.
B807 is what B781 has actually been waiting for since T484.
