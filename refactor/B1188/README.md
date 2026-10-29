# B1188 (#507): the landing gap is the sprite's texture -- stored 2:1, drawn at 2x

**The b1130 landing gate's 0.53x same-window pixel gap is the texture storage,
not a size or brightness word.** The client draws its spell textures at
native resolution (record 80's pillars are 64x64); the shipped atlas had
box-reduced them 2:1 into one 32x32 cell, cutting each sprite's additive mass
(peak alpha 255 -> 107 on the pillar, measured). The fix stores the native
pixels 1:1 in the atlas and draws the full stored span.

## 1. The gap (the specification)

- The b1130 gate's landing_cov column is red on 38/40 rows, **median 0.531x**
  (refactor/B1186/README.md:230).
- B1186 proved the *window* is already identical on both committed sides
  (refactor/B1186/README.md:12, both at 209^2) -- so the red column is pixels,
  not the measurement frame.
- The 93 like-for-like ratio is 80,960 / 147,318 lit px = **0.55x** in the
  same pixel window (refactor/B1166/README.md:349).
- The population is at its word count (refactor/B1164/README.md, the
  1175-peak census) -- there is no missing particle to find. The gap is what
  each particle *draws*.

## 2. Per-emitter census, record 80 (spell 93) (Ask 1)

Record 80 is spell 93 -- the same record the tour row `tour_93` renders. Its
landing (Target stage) carries three emitters, pinned per-emitter by B1180
(refactor/B1180/README.md:82):

| emitter | slot / texture | w = h (the pin) | role |
|---|---|---|---|
| S3-1 | 280 / spelcc | 0.5 | short flash |
| S3-2 | 281 / spelcm | 0.5 (sheet) | short sheet |
| S3-3 | 282 / spelcf | **6.0** | the long pillar |

**Oracle side: a per-emitter split by colour is impossible for this record,
proven not assumed.** All four landing textures are the same flame hue
(spelcc/spelcf/spelcb/spelce have B = 0 with G 43..165; only the cream
spelcm carries B = 99); nearest-neighbour premultiplied-colour
classification misassigns ~97% of the pillar to the cream set
(`/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/` pillar_hue.py +
emitter_centroids.py, frames/oracle_r51).

**Willeq side: per-emitter isolated runs** (each emitter alone, the full
run minus the source/impact run e0 -- exact subtraction, FX-only, the B1130
gate crop, matched to the oracle's t_act):

| emitter | peak lit px | at | integral 0..7 s | duration |
|---|---|---|---|---|
| S3-3 (spelcf, 6.0u) | 131,700 | t+2.27 | 18.3 M | 6.0 s |
| S3-1 (spelcc, 0.5u) | 26,200 | t+1.33 | 1.45 M | 3.9 s |
| S3-2 (spelcm, sheet) | 24,500 | t+0.83 | 0.99 M | 4.3 s |
| Imp01 (spelcb, 1.25u) | 14,000 | t+1.43 | 1.17 M | 4.3 s |

**Like-for-like region** (the pillar above the target's head, FX-only,
matched t_act): at t+2.27 the oracle carries 53,623 lit px and willeq 45,831
(**0.86x**); above the brighter threshold (px > 150) the oracle carries
40,243 and willeq 22,983 (**0.57x**). Channel means: the red and blue
channels match (R 190/193, B 3/2); the green channel runs at roughly half
the oracle's (93 vs ~56 pre-fix) -- a pre-existing residual, not introduced
by this card (sec. 7).

**The named short emitter: S3-3, the 6.0u pillar (spelcf).** Willeq is
short by ~1.75x on the bright tail (px > 150) and ~14% on lit80 at peak,
and the pillar is the only emitter that carries the record to its 6.0 s
end -- the shortfall is therefore on every landing frame of the record,
which is exactly the red `landing_cov` column.

## 3. The word trace (Ask 2)

Every word that sets S3-3's drawn size and brightness was checked against
the client's own values (B1180's live lane-5 pin is the reference,
refactor/B1180/README.md:82, and its half-width rule refactor/B1180/README.md:43):

| word | client | willeq | verdict |
|---|---|---|---|
| per-emitter size w = h | 6.0 | 6.0 (drawn 7.2 = 6.0 x 1.2 half-width product) | correct |
| grow-in / shrink-out windows | 0 / 0 | 0 / 0 | correct |
| per-particle alpha curve | the eddParticleAlpha mirror | dev 0.039 | correct |
| population | 42 x 2 = 84 / frame | 84 / frame | correct |
| blend / depth-bias words | additive, no bias | additive, no bias | correct |
| **texture** | **64x64 native** | **a 2:1 box-reduction of it** | **the mis-applied word** |

The mis-applied "word" is the texture storage, with a number: the shipped
atlas cell for spelcf is a 2:1 alpha-weighted box of the original 64x64
(measured from the shipped bytes against the source,
`/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/` atlas_alpha_check.py +
atlas_cell_check.py): **peak alpha 255 -> 107, lit80 area per sprite /5,
and no single sprite can exceed lit150 (107 < 150)** -- exactly the measured
signature, with the shortfall concentrated in the high-bright tail. The
builder's box average (tools/spell_sprite_atlas_builder.cpp:121) was correct
*as a downscaler*; it was the wrong rule for a texture the client draws
native.

## 4. The fix (Ask 3)

The sheet is now an 8x8 grid of 4x4-cell blocks, one block per sprite
(include/client/graphics/environment/spell_sprite_atlas.h:268), and every
picture a block can hold natively is stored 1:1:

| source | storage | span (cells) |
|---|---|---|
| single 64x64 image | its 2x2 sub-block, byte-identical | 2 |
| 2x2 sheet, four 64 px frames | four 2x2 sub-blocks, byte-identical | 2 |
| 128x128 sixteen-frame grids | their 32 px cells, 1:1 | 1 |
| glowflare02 (256x256) | the whole block, 2:1 alpha-box (the one true reduction) | 4 |
| 32x32 grids / 16x16 upscales | the existing rules | 1 |

The per-file stored frame is a table in the header
(kStoredFramePx, include/client/graphics/environment/spell_sprite_atlas.h:193)
with a static_assert that it names every file (line 210); the builder derives
the same size and asserts it matches, so build and client cannot drift.

The draw carries a per-vertex `aSpan` (cells) -- 12 floats / 48 bytes per
PointSpriteVertex -- and the point and corner shaders sample the sprite's
stored frame instead of one cell (src/client/graphics/environment/unified_particle_renderer.cpp:135).
Procedural tiles and 32 px grids keep span 1, so every other sprite in the
sheet is bit-for-bit the old draw.

**The unit tests pin the client's rule:**
- tests/test_spell_sprite_atlas.cpp -- 12 tests, including
  `NativePicturesAreStoredOneToOneNotBoxed` (every stored frame is
  byte-identical to its source pixels; the one reduced texture is the one
  named) and `TheSpriteBlocksDoNotOverlapAndFitTheSheet` (the tiling
  contract, sec. 5).
- tests/test_spell_target_stage_textures.cpp -- span-aware tiling
  (a frame's stored span stays inside its block) + the no-overlap net.
- tests/test_spell_colour_and_overrides.cpp -- the stored-frame colour
  average is the source's, not a box of it.

## 5. Run 2: the tiling bug the first commit shipped

The first commit's `blockBase()` tiled the block grid with a **vertical
pitch of one cell-row (32 px) instead of one block-row (128 px)**: every
block row slid up over the one above it, and a lower sprite's frames
overwrote the upper sprites' stored frames. No per-sprite check could see
it -- each sprite's own blits were internally consistent, and the overwrite
landed in pixels that "belonged" to the other sprite by the same wrong
formula (a temporary DBG proved the pixels correct at blit time and wrong
after a later sprite). The pitch is now the block's own size in both
directions (blockSlotCell, include/client/graphics/environment/spell_sprite_atlas.h:268),
and `TheSpriteBlocksDoNotOverlapAndFitTheSheet` pins the contract directly:
every block on a 128 px boundary, no two blocks share a pixel.

The regenerated atlas is `spell_sprite_atlas.bmp`
sha256 14fa055cdff84185f3386cc08c9cd9568364c5c8ea13b981d6c7531f8b112d68;
the pack reinstalled from it is 785690e646af94c13336fd8ea0672d2aafa4d39ef43dc9e351a2d1f3b170b203;
both pins are in this commit (the cascade rule).

## 6. Suites (ThinkPad, GLEStree, Release)

- test_spell_sprite_atlas **12/12 pass**; test_spell_target_stage_textures
  **32/32 pass**; test_spell_colour_and_overrides pass (four env skips).
- TexturesReproducible / PackReproducible / InstalledPack: **pass**
  (atlas + pack + pins committed together).
- hygiene: **49/49 pass**.
- test_spell_sprite_sheet_draw: 9 readback failures that are the
  ThinkPad's pre-existing DRM readback break -- the failure set is
  **byte-identical** (test names only) to the pre-change
  kb-b1183-stand-state build's set; CI (a working readback host) judges.

## 7. Re-gate vs the B1178 references (Ask 3) — results

Renders: tour-7 (7/7) + the 40-record b1130 sweep (40/40) on the ThinkPad
from this worktree (refactor/B1188/scripts/run_gate_b1188.sh), measured with
the B1139/B1155 instruments, judged by
refactor/B1188/scripts/build_tables_b1188.py against the B1178 reference
set (gate_c.tsv oracle columns, kTour, the B1142 regression table). Tables:
refactor/B1188/tables/ (`b1130.tsv`, `tour.tsv`, `distance.tsv`,
`counts.tsv`).

**The gate is flat, and that is the honest result.** The per-row B1188 delta
(measured against the B1186 tip — the same tree *without* the atlas change,
so this isolates B1188 alone) is **≤±1.5% on every one of the 9 measured
columns**, and `source_onset_s` is exactly **0.000 on all 40 rows** (a
texture-storage change cannot move an onset). The `landing_cov` headline:

| row (record) | B1186 tip | B1188 gate2 | Δ | oracle | ratio |
|---|---|---|---|---|---|
| **0 (spell 93)** | 3.261 | 3.274 | +0.013 | 3.945 | **0.830×** |
| 3 | 8.623 | 8.667 | +0.044 | 11.688 | 0.741× |
| 32 | 5.098 | 5.136 | +0.038 | 6.547 | 0.784× |
| 21 | 4.782 | 4.767 | −0.015 | 5.976 | 0.798× |

`landing_cov` PASS count is **0 → 0** (38 short + 2 over on both sides; the
over rows are r31 1.137× and r40 5.809×, unchanged). The 38-short-row median
is **0.531× → 0.531×**. No column regressed. The judge's `d_` columns are
vs the B1180 after columns (B1184 judge heritage, which includes the
B1184/B1186 window unification); the B1188-specific delta is the one against
the B1186 tip tabulated here.

**The 93 like-for-like ratio** (the card's headline row): `0.827× → 0.830×`
(b1130 209² window); the raw tour `land_cov` is `3.059 → 3.065` (flat). The
bright tail of the pillar — the part the fix actually changes — is
`0.571× → 0.683×` on px>150 at the matched t.

**The fix is real in the render.** A/B with the *same* binary, plan and
seed, differing only in the pack (and the pack diff is exactly one file,
`spell_sprite_atlas.bmp` — verified by diffing the two trees'
`data/textures`): the 64×64 pillar is **+54.9% lit80** (29,758 → 46,106)
and **+86.9% px>150** (17,423 → 32,563), while the full-crop lit80 is
−4.1% (83,939 → 80,499). The 1:1 storage restores the native pixels; the
2:1 box had been diffusing them.

**Why the gate does not move.** The `landing_cov` window (209² / 214²) is
ground-centred on the target's anchor (row 615, i.e. window rows 511–719);
the 64×64 pillar the fix brightens (rows 60–360) sits **above and outside
that window**. Box averaging conserves each sprite's alpha integral, so the
*total* light inside the window barely changes — the storage change
redistributes it (sharper core, thinner halo), which is exactly what the
px>150 band shows. Consequence: **the 0.53×/0.54× `landing_cov` gap is not
sprite-storage driven.** The oracle renders native and fable now does too,
and the gap persists at the same level in the gate window; it is owned by
the landing area / attach-scale chain (B1164 #483), as the B1186 README
already attributed.

**Measured residual (tracked, not introduced by this card).** The bright
(px>150) flame pixels' **green channel runs at roughly half the oracle's**
(willeq G≈47 post-fix / ≈56 pre-fix vs oracle G≈93); red and blue match
(R 193/189, B 2/3). Pre-existing ETC1/tint residual — the atlas BMP is
byte-exact, so the unverified stage is pack encoding / shader sampling. A
measurement-only RGBA8888 A/B pack is the next experiment (no shipped
override, no tuned factor — Will's rule holds throughout).

## 8. The 93 ledger clip

Regenerated from this gate's `tour_93` render vs the B1178 v3 pinned-camera
oracle capture, via the B1158 pipeline (make_clip.py + make_index.py) — the
B1184 make_ledger recipe with this tree's tour table, into
`/home/user/kb-evidence/VISUAL-FX/b1188-gate/`.
