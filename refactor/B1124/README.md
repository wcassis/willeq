# B1124 (#444): spell sprite atlas packs single-image sheets whole (B1118 M8)

Chain: B1118 -> B1119 -> B1120 -> B1121 -> B1122 -> B1123 -> **B1124**. Parent #424 stays open.
Evidence: `/mnt/projects/fable/review/B1124/` (README there indexes it).

## 0. Result

| acceptance | result |
|---|---|
| 1. census every `spel*.tga` (grid vs single image) | **done**: 67 files, 48 grids (all 256x256 / 128x128), 19 single images (all 64x64); 7 of the 33 shipped sprites are single images (sec. 1) |
| 2. pack a single image as ONE frame for the whole life; header = single source | **done**: `kSheetFrames` / `framesFor()` / `cellForAge()` in `spell_sprite_atlas.h`; builder packs whole and exits 1 if its own census disagrees; renderer holds the cell (sec. 2) |
| 3. atlas + `textures.sha256` + EQE1 pack + its sha in one commit | **done**; TexturesReproducible / PackReproducible / DbReproducible **built and PASS** (sec. 3) |
| 4. oracle-owed: is the 93/38 flash spelc3 drawn whole? | **answered: NOT a single spelc3 sprite; the flash itself is unidentified.** spelc4 drawn whole IS identified, in the Source stage (sec. 4) |
| 5. lane-5 gate: the 93/38 flash appears at Act+0.5-0.9 s | **MISS.** Fable now draws whole stars, but no flash appears (strips sec. 5). Nothing regresses beyond capture spread |
| 6. tests red on the pre-fix tree, docs, pool 4096 | **done** (sec. 6); pool 4096 on every leg |

**This card is a correctness fix, not the flash.** The atlas no longer cuts seven pictures into 16x16 fragments.
The oracle's white flash needs something the art alone does not supply (sec. 4), so it stays open as a follow-up.

## 1. Census (`review/B1124/scripts/sheet_census.py`, `review/B1124/sheet_census.tsv`)

**Test.** `seam` = the share of additive mass (max(R,G,B) x alpha) that falls inside a band of cell/8 texels on each side of the
4x4 sheet's three internal seams in each direction, divided by the band's share of the area. 1.0 = a uniform texture.
A grid pads each frame inside its own cell, so it reads near 0. A single picture's body crosses the seams, so it reads above 1.
The dimension, the seam ratio and a contact sheet (`review/B1124/small_sheets.png`, grid lines drawn) agree on every file.

- All 67 files: grids read <= 0.58 (spelw1), single images >= 1.31 (spelz5). The gap is wide, so the 1.0 split is not a tuned value.
- **Every 64x64 file is a single image; every 128x128 or 256x256 file is a grid.**
- Not shipped but also single: spelb5 spelc5 spelcd speld3 speld5 speldd spele2 spelg8 spelg9 spely5 spelz5 spelz7.
- The earlier `off` metric (per-cell centroid offset) misread opaque/elongated pictures (spely4, spelcd); it is reported, not used.

Shipped sprites (33):

| file | dims | seam | verdict |
|---|---|---|---|
| spela1.tga | 256x256 | 0.061 | GRID |
| spela2.tga | 256x256 | 0.045 | GRID |
| spelb1.tga | 256x256 | 0.003 | GRID |
| spelb2.tga | 256x256 | 0.008 | GRID |
| spelb3.tga | 64x64 | 1.355 | SINGLE |
| spelb4.tga | 64x64 | 2.244 | SINGLE |
| spelc1.tga | 256x256 | 0.011 | GRID |
| spelc2.tga | 256x256 | 0.296 | GRID |
| spelc3.tga | 64x64 | 1.333 | SINGLE |
| spelc4.tga | 64x64 | 2.115 | SINGLE |
| speld2.tga | 256x256 | 0.048 | GRID |
| speld4.tga | 64x64 | 1.720 | SINGLE |
| spele1.tga | 256x256 | 0.000 | GRID |
| spelg1.tga | 256x256 | 0.211 | GRID |
| spelh1.tga | 256x256 | 0.092 | GRID |
| speli1.tga | 256x256 | 0.074 | GRID |
| spelj1.tga | 256x256 | 0.150 | GRID |
| spelj2.tga | 256x256 | 0.055 | GRID |
| spelk2.tga | 256x256 | 0.033 | GRID |
| spell1.tga | 256x256 | 0.145 | GRID |
| spelm1.tga | 256x256 | 0.234 | GRID |
| speln1.tga | 256x256 | 0.021 | GRID |
| spelo1.tga | 256x256 | 0.039 | GRID |
| spelp1.tga | 256x256 | 0.030 | GRID |
| spelr1.tga | 128x128 | 0.260 | GRID |
| spelr2.tga | 128x128 | 0.030 | GRID |
| spelu1.tga | 256x256 | 0.202 | GRID |
| spelu2.tga | 256x256 | 0.142 | GRID |
| spelv1.tga | 256x256 | 0.055 | GRID |
| spely2.tga | 256x256 | 0.011 | GRID |
| spely3.tga | 64x64 | 1.415 | SINGLE |
| spely4.tga | 64x64 | 1.898 | SINGLE |
| spelz1.tga | 256x256 | 0.000 | GRID |

**T420's "six sheets with a BLANK frame 0, and spely4 all but blank" were exactly these seven single images.** The blank
"frame 0" was the empty top-left corner of one centred picture. The header narrative and `TheBlankFirstFramesAreShippedAsBlank` are corrected (sec. 6).

Which spells use them: in the 7-spell tour, only record 0 (93/38): Source spelc2/spelc4/spelc2, Target spelc3/spelc2/spelg1.
Records 20/23/33/35 (41, 54/656, 458, 253) use no single image (`review/B1118/mechanism_notes.txt`), so their rows are the control.

## 2. Layout change

- **`include/client/graphics/environment/spell_sprite_atlas.h`** (single source):
  - `kSheetFrames[]` is parallel to `kFiles[]`: 16 for a grid, `kSingleImage` (1) for a single image. A static_assert checks the length.
  - `framesFor(sprite)`; `frameForNormalizedAge(age, frames = 16)` returns 0 when frames <= 1; `cellForAge(sprite, age)` is what the renderer samples.
  - Every sprite keeps its 16-cell slot (`kUsedCells` 528, unchanged), so no ordinal or cell moves.
    A single image occupies cell 0 of its slot; the other 15 are transparent.
  - The sheet is still 1024x1024: 26 x 16 + 7 x 1 = **423 drawn cells** in 528 reserved.
- **`tools/spell_sprite_atlas_builder.cpp`**: `measuresAsSingleImage()` is the seam census in integer arithmetic (B268).
  The builder exits 1 when the census and the header disagree. A single image is packed whole: 64x64 to 32x32 by the
  existing alpha-weighted 2:1 box average, the same factor as a 256x256 grid's frames.
- **`UnifiedParticleRenderer::render()`**: the cell is `cellForAge()`, so a single-image particle samples cell 0 for its
  whole life. The sim side (`sim_particles.cpp`) only ages particles and never selects a frame. No other consumer
  computes a cell (the legacy CPU billboard path maps every sprite index to SoftCircle).

## 3. Cascade artefacts (one commit)

| artefact | before | after | bytes |
|---|---|---|---|
| `data/textures/spell_sprite_atlas.bmp` | 2b2eeb77...a7f7 | **febf9ca9a12cb38c478ed3e2bbc5a9c44ba8983c072d0e8b641571bd00922b6f** | 4,194,426 = |
| `data/textures.sha256` | its atlas line + header comment | rewritten by `build_textures.sh --write-pins` | - |
| `willeq.eqe1` (pin `data/willeq.eqe1.sha256`) | 3844461f...0bae | **8199a8a2b179edf96e93ebd29434eead26fcb784d1a5d12b9362aefe112de4ea** | 14,173,156 = |

- `build_textures.sh` (in place) changed ONLY the atlas BMP; the other 71 regenerated byte-identically.
- **Memory: zero change.** The atlas geometry and the pack size are unchanged, so every zone's `eqe1MappedBytes` and the texture ledger are unchanged.
- **Pack install.** NOT installed to the shared `/mnt/projects/EverQuest_ETC1/willeq.eqe1` (B1094: the orchestrator installs at merge).
  A copy of the built pack is at `review/B1124/pack/willeq.eqe1`, beside symlinks to every other entry of the shared directory.
  The gate ran with `--eqe1-path review/B1124/pack`.
  - `InstalledPackMatchesPin` passes off fable (print-and-pass). **It fails on fable until the pack is installed at merge:**
    `scripts/build_willeq_pack.sh` from the merged tree; expected sha 8199a8a2.

## 4. The oracle's 93/38 flash (`review/B1124/oracle_flash/`)

Stepped frames: `o93_steps.png` (oracle_druid f7533-7552) and `o38_steps.png` (oracle_leg f9755-9774), plus zooms.
`identification.png` puts the sprites beside the oracle crops.

- **spelc4 drawn whole: IDENTIFIED, in the Source stage.** The oracle's Source cloud (e.g. f7533, `o93_f7533_source_flares_x2.png`)
  is full of small orange vertical diamond flares: shape, colour and aspect are spelc4's single picture. This is the record-0 Source slot.
- **The Act+0.5-0.9 s flash is NOT one spelc3 sprite.**
  - **Timing.** It starts at f7537-7538 (Act+0.51) / f9758 (Act+0.45) at the target's feet, at the bottom of the frame, and grows over ~10 frames.
  - **Size.** It is about 1.1 caster heights wide and at least 0.7 tall (clipped by the chat window).
    That is about 10x fable's record-0 landing sprite (ROI p90 0.10-0.12 caster heights).
  - **Colour.** The core is saturated WHITE (min channel >= 245 over 25-29 k px, against a 1.6-6.2 k px yellow fringe; f7538/7541/9760/9763).
    spelc3's texels carry at most 19/255 of blue (centre 255,216,21), so even stacked additively it cannot turn white over grass
    without about 10 overlapping layers. Of record 0's Target art, only spelg1 (the sword, 255,255,255 max) carries blue.
  - **Verdict.** The flash is consistent with a dense, large heap of record-0 Target sprites. Its yellow fringe matches spelc3/spelc2,
    and its white core needs spelg1 or something else. **Which art, at what size and count, is unidentified:** the frames
    saturate, so no single sprite outline can be read. No counterpart was invented.

## 5. Gate (ThinkPad lane 5)

- **Build.** ThinkPad `~/src/willeq`, branch `b1124-fx-atlas-single` = this branch's WIP 918718e0 (same code, tests and data as
  this commit), `scripts/build-gles2-x86.sh` JOBS=12 Release.
  - The binary is `404f205f...`. The tree's atlas BMP is febf9ca9.
  - Every leg's `pack.mapped` (`/proc/<pid>/maps`) shows `review/B1124/pack/willeq.eqe1`, sha `8199a8a2` (`fable_*/pack.sha256`, `tree.sha256`).
- **Legs.** `fable_A` (class 12: 38 54 656 458), `fable_B` (6: 253 93), `fable_C` (14: 41), 11:12-11:24 UTC.
  apt-daily runs at 20:56 and apt-daily-upgrade at 06:58, so the legs were clear of both.
  - The plans now hold `#set time` >= 36 s before the first cast (B1123 follow-up 5).
- **Capture.** x11grab 30.00 fps; max frame gap 34 ms; 0/59 stale grabs in every landing window. No `--dump-frames-dir`.
- **Frame time.** After the first cast, every window has 60 frames. Window-mean 0.78-1.15 ms; worst frame 2.2 ms; 0 `slow!`.
- **Pool.** `pool initialized (size=4096)` on every leg.
- **Lane.** The lock is FREE, char 67 is class 12, and the stack was brought back up (`final_up.log`). No willeq/ffmpeg/tcpdump left on the ThinkPad.

**Strips** `review/B1124/strips/strip_93.png`, `strip_38.png` show ORACLE / B1123 gate (before) / B1124 at Act+0.4/0.5/0.7/0.9/1.2 s.
- The oracle's white flash is at Act+0.5-0.9.
- B1124 draws whole yellow-white stars where B1123 drew orange fragments, and the landing is the same field.
- **No flash: acceptance 5 is NOT met.**

**Before / after** (`gate_table.tsv`; caster heights; B1123 = its gate on the same config):

| spell | landing cov B1123 -> B1124 (oracle) | end | ROI p90 landing (B1119 band / oracle) | Source cov / ext / cen | hue land, src |
|---|---|---|---|---|---|
| 93 | 2.833 -> **2.921** (+3.1 %) (5.847) | 4.922 -> 4.836 | 0.115 -> **0.098** (0.063-0.085 / 0.075) | 0.394->0.413 / 2.18->2.49 / 0.80->0.87 | 36->34, 28->27 |
| 38 | 1.901 -> **1.928** (+1.4 %) (3.889) | 4.836 -> 4.794 | 0.093 -> 0.092 (0.049-0.067 / 0.069) | 0.405->0.429 / 2.32->2.40 / 0.94->0.85 | 33->32, 28 = |
| 54 | 1.574 -> 1.596 | 5.165 -> 5.053 | 0.087 -> 0.086 | 0.533->0.546 / 1.52 = / 0.70->0.71 | 220->218, 224 = |
| 656 | 1.288 -> 1.268 (-1.6 %) | 5.118 -> 5.016 | 0.073 -> 0.075 | 0.596->0.597 / 1.51 = / 0.69 = | 220->221, 224->223 |
| 41 | 3.684 -> 3.700 | 6.352 -> 6.326 | 0.163 -> 0.155 | 1.085->1.133 / 2.71->2.40 / 0.94->0.92 | 70 =, 75->74 |
| 253 | 1.862 -> 1.865 | 6.477 -> 6.461 | 0.067 -> 0.066 (0.050-0.068) | 3.235->3.271 / 3.89->3.90 / 1.04->1.07 | 202 =, 198 = |
| 458 | 1.530 -> 1.576 | 5.003 -> 4.999 | 0.072 -> 0.076 (0.057-0.077) | 0.062->0.065 / 0.74->0.76 / 0.58->0.55 | 34 =, 62->50 (sat 0.02) |

- **93 / 38 (the card's rows).** Landing coverage +3.1 % / +1.4 %, on the order of B1123's +1 % lit-area census.
  93's landing ROI p90 fell 0.115 -> 0.098, toward the oracle's 0.075: a whole star is smaller than a magnified fragment.
  The record-0 hue is unchanged within 2 degrees (the art is the same texels).
- **Controls (no single image in their records).** They moved -1.6..+3.0 % in coverage, inside B1123's measured
  same-config spread (3.8 %, B1123 sec. 4c). Ends moved -0.11..-0.02 s; every end is within 0.13 s of the oracle (band +-0.4).
  ROI p90: 253 and 458 are still in their B1119 bands.
  41's Source extent 2.71 -> 2.40 is inside its own run spread (B1122 read 2.43); record 20 is untouched.
- **Act-0.1..+0.5 hold** (`gap_table.tsv`). Intact. 93 reads 0.42/0.42/0.39/0.40/0.43/0.57 (oracle 0.41/0.43/0.41/0.42/0.35/0.59);
  the other six are within 0.08 of B1123's gate (largest: 41 at Act-0.1, 1.11 -> 1.19).
- **No re-band was needed.** No test pins a measured 93/38 coverage or ROI number, and the config is byte-identical.
  The pinned values are config values (sizes, rates, lifetimes), which this card does not change.

## 6. Tests

New or changed pins:
- `tests/test_spell_sprite_atlas.cpp`:
  - `TheSingleImageSheetsArePackedWholeInTheirFirstCell` replaces T420's `TheBlankFirstFramesAreShippedAsBlank`.
  - New: `TheCentreOfASingleImageIsTheCentreOfItsCell`, `TheGridOrSingleCensusMatchesTheHeader` (the census re-derived from the TGAs).
  - `EveryCellEqualsItsSourceFrame`, `EverySpriteHasVisibleFrames...` and `TheSixteenFramesOfASprite...` now honour the frame count.
- `tests/test_spell_target_stage_textures.cpp`: `ExactlyTheSevenCensusedSingleImagesAreOneFrame`,
  `ASingleImageHoldsItsOnlyFrameForTheWholeLife`, `AGridSpriteStillWalksAllSixteenCells` (the negative).
- `tests/test_spell_sprite_sheet_draw.cpp`:
  - New: `ASingleImageSpriteDrawsItsOnePictureForItsWholeLife` (real GLES2 draw, spelc3/spelc4 at ages 0..3.7).
  - The overrun test moved off spelb3 (a single image) to spelg1.
  - `EveryShippedSpriteDrawsItsOwnPixels` expects cell 0 for single images.
- `tests/test_spell_colour_and_overrides.cpp`: the sprite-colour model time-weights a single image's one frame x16.

**Red on the pre-fix tree** (`review/B1124/tests/`):
- `prefix_atlas_old_bmp.txt`: the new atlas tests against the pre-fix BMP. 4 FAILED: PackedWhole, CentreOfCell,
  EveryCellEqualsItsSourceFrame, EverySpriteHasVisibleFrames. The census test passes, because it reads the TGAs and the header.
- `prefix_renderer_old_code.txt`: `test_spell_sprite_sheet_draw` built against the b79cc31b renderer. 2 FAILED:
  ASingleImageSpriteDraws..., EveryShippedSpriteDrawsItsOwnPixels.
- `prefix_layout_old_header.txt`: the layout/animation pins do not compile against the b79cc31b header (no `framesFor` / `cellForAge`).

**Runs** (scratchpad Release build):
- `ctest -R "Spell|Sim|Particle|Landing|DataPath|ConfigKey|B1106|CasterOwn|Atlas|Sprite"`: **100% tests passed, 0 tests failed out of 486** (32 skipped, EQ-file fixtures, as before).
- `ctest -L hygiene`: **100% tests passed, 0 tests failed out of 48**.
  **TexturesReproducible PASS, PackReproducible PASS (65.9 s), DbReproducible PASS.** The generator tools were built this time.

**Docs.**
- CLAUDE.md "The spell sprite atlas": "all sixteen frames of all 33 sprites, 528 used" and the "fade-in cycles" narrative are struck.
  The grid/single rule and `cellForAge()` are stated.
- CLAUDE.md is 59,969 bytes (was 59,986). `check_doc_counts.py`: OK.
- The header narrative and the pin-file comments are updated.

## 7. FOLLOW-UP (for the orchestrator; workers do not cut cards)

1. **The 93/38 white flash is still missing (acceptance 5).** It is oracle-owed: which art (spelg1? spells.eff's
   secondary list, M7?), what size (~1 caster height), how many, and for how long. Measure it on an unsaturated capture
   (lower gamma or a darker zone) before any change; do not size-boost record 0 on a guess.
2. **Install the pack at merge**: `scripts/build_willeq_pack.sh` from the merged fable tree, expected sha 8199a8a2.
   `InstalledPackMatchesPin` is red on fable until then.
3. **Log defect.** `EQE1Loader: mmap'd '%s' (v%u, ...)` prints its printf format unformatted (a fmt/printf mismatch), so the
   fable log cannot name the pack it loaded. This card used `/proc/<pid>/maps` instead.
4. The 12 unshipped single images (spelb5, spelg8/9, ...) are not in `kFiles`. If a later card ships one, `kSheetFrames`
   must say 1 for it, and the builder refuses otherwise.
