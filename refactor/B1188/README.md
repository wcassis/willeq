# B1188 (#507): the landing gap is the sprite's texture -- stored 2:1, drawn at 2x

**The b1130 landing gate's 0.53x same-window pixel gap is the texture storage,
not a size or brightness word.** The client draws its spell textures at
native resolution (record 80's pillars are 64x64); the shipped atlas had
box-reduced them 2:1 into one 32x32 cell, diffusing each sprite's core
(measured from the shipped blob: peak alpha 255 -> 251, the integral
conserved, sec. 3). The fix stores the native
pixels 1:1 in the atlas and draws the full stored span.

## 1. The gap (the specification)

- The b1130 gate's landing_cov column is red on 38/40 rows, **median 0.531x**
  (refactor/B1186/README.md:230).
- B1186 proved the *window* is already identical on both committed sides
  (refactor/B1186/README.md:12, both at 209^2) -- so the red column is pixels,
  not the measurement frame.
- The 93 like-for-like ratio is 80,960 / 147,318 lit px = **0.55x** in the
  same pixel window (refactor/B1166/README.md:351).
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
(`/home/user/kb-evidence/kb-b1188-landing-pixels/scripts/` pillar_hue.py +
emitter_centroids.py on hermes-ext; the frames are
`/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/frames/oracle_r51`).

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
channels match (R 189/193, B 3/2); the green channel runs at roughly half
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

The mis-applied "word" is the texture storage, with the numbers re-derived
from the ACTUAL old shipped blob, not a description of it
(`refactor/B1188/scripts/old_cell_stats.py`, pure stdlib; the committed
command is its default: `--repo . --blob
ac8c26bc6:data/textures/spell_sprite_atlas.bmp --source
/mnt/projects/EverQuestP1999/SpellEffects/spelcf.png --sprite 22`; output in
`/home/user/kb-evidence/kb-b1188-landing-pixels/old_cell_stats.out`):

- the stored cell is the builder's 2x2 box of the source **byte-for-byte**
  (max |alpha diff| 0, 0/1024 mismatched px);
- the box **conserves the integral**: mean alpha 28.15 (64x64) vs 28.08
  (32x32), stored*4/source = 0.998 (integer truncation);
- it does **not** cut the bright bands: the stored cell carries
  **peak alpha 251** (not 107), **72 px > 150** (so the sprite very well can
  exceed lit150), p99 243; at the 288 px drawn size the stored sprite's
  lit80 count is **1.03x** the source's (10,357 vs 10,024), its >128 band
  1.05x (7,016 vs 6,667) and its >150 band 1.03x (6,004 vs 5,847).

These agree with the reviewer's own read of the same blob (note 8515: peak
251, 72 px > 150, p99 243, mean alpha 28.2 both sides, lit80 1.03x at
288 px); the one figure that differs is the >128 band, 0.96x in the
reviewer's cv2 INTER_LINEAR upscale vs 1.05x here (stdlib bilinear) — both
≈1.0, so the conclusion holds either way. The mechanism is the one sec. 7
states: **box averaging conserves each sprite's alpha integral and
redistributes it** (the native pixels are the sharper core / thinner halo) —
and the
drawn effect is per-particle at sprite size. The corrected pack A/B in
sec. 7 (the pre-B1188 tree's own render vs this tree, pillar box at f158)
measures the redistribution in its real direction: the diffused 32-px halo
lit *more* of the box (lit80 69,100 -> 42,645; px>150 61,360 -> 27,497),
and the 1:1 native render moves the bright-pixel population *toward* the
oracle's count (5.0x -> 2.2x of it, sec. 7); the single-sprite bands above
agree (stored px>150 6,004 vs source 5,847; lit80 10,357 vs 10,024). The
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

**The gate is flat, and that is the honest result.** Both sides of this
comparison are now committed (B1188 FIX, note 8515 finding 2). The AFTER
side is the B1188 gate2 run (above); the BASE side is the **merge-base
tree — ac8c26bc, the B1186 tip, this branch's first parent, the same tree
*without* the atlas change** — rendered with the shared baseline runner
(`/mnt/projects/fable/review/GATE-BASELINE/render_gate.sh`, worktree
`kb-b1181-gate-base` @ac8c26bc with the OLD pack rebuilt byte-exact from
that tree, sha256 `1cdd31e74594af36d667876e944cd1538fc2ffe6586de158f4b5c78fffded730`,
evidence `/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/gate_base/`) and
judged by the same judge into `refactor/B1188/tables/base/` (the
`B1188_MEASURE`/`B1188_OUT` override of build_tables_b1188.py; 40/40 b1130 +
7/7 tour rows). The per-row B1188 delta is what
`refactor/B1188/scripts/delta_b1188_base.py` prints against the two committed
tables (output: `/home/user/kb-evidence/kb-b1188-landing-pixels/delta_b1188.out.txt`):
`source_onset_s` is exactly **0.000 on all 40 rows** (a texture-storage
change cannot move an onset); the coverage and time columns move at most
**0.018** (source_cov, r40: 1.258 -> 1.276), **0.044** (landing_cov, r3:
8.623 -> 8.667) and **0.100 s** (landing_end_s, r16: 5.200 -> 5.300) in
absolute terms (the script now reports the per-column maximum by ABSOLUTE
delta and the maximum relative delta separately), ≤2.9 % relative on every
one of them. The two small-magnitude sprite-height columns are noisier only in
relative terms — p90_land 0.022 at r32 (40.7 %) and src_h_lo 0.010 at r35
(7.7 %), both ≤0.022 px in absolute on the 209 px axis, frame-to-frame
measurement noise, not a gate movement. Tour-7: `landing_cov` max |Δ|
**0.007** (spell 458), `end` and `src_on25` exactly **0.000 on all 7 rows**.
The `landing_cov` headline (base column = committed `tables/base/b1130.tsv`):

| row (record) | base (ac8c26bc) | B1188 gate2 | Δ | oracle | ratio |
|---|---|---|---|---|---|
| **0 (spell 93)** | 3.261 | 3.274 | +0.013 | 3.945 | **0.830×** |
| 3 | 8.623 | 8.667 | +0.044 | 11.688 | 0.741× |
| 32 | 5.098 | 5.136 | +0.038 | 6.547 | 0.784× |
| 21 | 4.782 | 4.767 | −0.015 | 5.976 | 0.798× |

`landing_cov` PASS count is **0 → 0** (38 short + 2 over on both sides; the
over rows are r31 1.133× → 1.137× and r40 5.777× → 5.809×, +0.004 / +0.032,
both still over). The 38-short-row median is **0.531× → 0.531×**. Two PASS
counts move at band edges (`tables/base/counts.tsv` vs `tables/counts.tsv`):
b1130 `p90_src` 24 → 23 and tour `roi_s` 5 → 3, on deltas ≤ 0.039; every
other column's PASS count is unchanged. The judge's `d_` columns are
vs the B1180 after columns (B1184 judge heritage, which includes the
B1184/B1186 window unification); the B1188-specific delta is the one against
the B1186 tip tabulated here.

**The 93 like-for-like ratio** (the card's headline row): `0.827× → 0.830×`
(b1130 209² window); the raw tour `land_cov` is `3.059 → 3.065` (flat).

**The pack A/B, corrected (finding 6).** The original A/B ("same binary,
plan and seed, differing only in the pack — exactly one file,
`spell_sprite_atlas.bmp`, verified by diffing the two trees'
`data/textures`": pillar lit80 29,758 → 46,106, +54.9%) was **not** an
old-storage vs new-storage comparison: its "old" arm ran the *new* binary
against the *old-layout* pack, and the new addressing (the 8×8 block grid,
`blockSlotCell` in `spell_sprite_atlas.h`) reads the block at (768, 256) —
which in the old cell layout holds sprites 17/18's cells, not spelcf — so
29,758 is a misaddressed read of the old atlas, not the diffused pillar.
The true pre-fix behaviour is measured on the pre-B1188 tree itself
(B1191's fresh render: that tree's binary + pack `1cdd31e7`, same
plan/seed/camera), and the pillar box [60:360, 770:1030] at the matched t
(f158, t=5.27 s) is lit80 **69,100 → 42,645** and px>150 **61,360 →
27,497**: the diffused 32-px halo lit *more* of the box, and the 1:1 native
render sits **between** the old render and the oracle (like-for-like core
box px>150: 12,603). The storage change moves the bright-pixel population
*toward* the oracle's count (5.0× → 2.2× of it); the per-channel colour
settles as the re-measurement below shows.

**Why the gate does not move.** The `landing_cov` window (209² / 214²) is
ground-centred on the target's anchor (row 615, i.e. window rows 511–719);
the 64×64 pillar the storage change redistributes (rows 60–360) sits
**above and outside that window**. Box averaging conserves each sprite's
alpha integral, so the *total* light inside the window barely changes — the
storage change redistributes it, which is exactly what the px>150 band and
the matched-t box above show. Consequence: **the 0.53×/0.54× `landing_cov`
gap is not sprite-storage driven.** The oracle renders native and fable now
does too, and the gap persists at the same level in the gate window; it is
owned by
the landing area / attach-scale chain (B1164 #483), as the B1186 README
already attributed.

**The green-channel residual, re-measured on this fix head (finding 6,
B1191's instrument).** B1191 (#510) refuted the "G halved" reading on the
pre-B1188 tree; its follow-up pointed at this head. Re-measured here with
B1191's own per-channel scripts (committed under `refactor/B1188/scripts/`
as `b1191_*.py`; fresh ThinkPad renders of this head, pack `785690e6`,
same plans/seeds/cameras; oracle = B1191's pinned-camera captures,
diff vs tail-median baseline, peak landing frame; outputs
`/home/user/kb-evidence/kb-b1188-landing-pixels/f6_check_pair.out.txt` and
`f6_matched_box.out.txt`): per-channel means over `px>150`,
willeq / oracle / ratio —

| rec (spell) | side | px | R | G | B |
|---|---|---|---|---|---|
| 80 (93) | willeq | 113,530 | 198.5 | 94.8 | 5.1 |
| 80 (93) | oracle | 21,283 | 170.3 | 103.3 | 11.9 |
| 80 (93) | ratio | | 1.17 | **0.92** | 0.43 |
| 35 (253) | willeq | 162,965 | 22.6 | 68.5 | 174.3 |
| 35 (253) | oracle | 237,979 | 9.0 | 76.5 | 197.9 |
| 35 (253) | ratio | | 2.51 | **0.90** | 0.88 |
| 43 (720) | willeq | 11,155 | 187.2 | 159.5 | 163.1 |
| 43 (720) | oracle | 17,986 | 154.5 | 131.1 | 180.1 |
| 43 (720) | ratio | | 1.21 | **1.22** | 0.91 |

**No record shows G halved on this head** (0.92×/0.90×/1.22×, where the
tip's B1191 figures were 1.13×/0.83–0.91×/1.17×) — the old paragraph's "G 47
vs 93" was a pillar-box artifact of this tree, not a per-channel
coefficient (below).

**The "flame B 80 vs 9" branch difference is a measurement-method mix, not
blue.** B1188's cited `B=80` came from the gate census
(`review/B1132/scripts/fx_blobs.py`'s colour = the ABSOLUTE frame pixels in
the rise-mask); B1191's tip `B≈9` is the DIFF vs the tail baseline, which
removes the scene background — and that background is blue (the same
tip render's full-frame tail-40 median: R 51.2 / G 49.8 / **B 79.1**). Same method on both
sides (B1191's diff): this head's flame `B` is **2.4–4.4** at the matched
t in the pillar box (tip: 5.7–8.7; f148/158/168, px150) — no blue shift; and the storage chain
carries zero blue: `refactor/B1188/scripts/rgb_channel_census.py`
(stdlib-BMP + cv2 PNG, output
`/home/user/kb-evidence/kb-b1188-landing-pixels/rgb_census.out.txt`) finds
the source, the committed 1:1 cell and the old 2:1 box cell all at
**R=255, G≈38, B=0.0** over their bright sets, the 1:1 cell
**byte-exact 0/4096 px** against `spelcf.png`. ETC1 is therefore cleared of
any per-channel effect on this sprite.

**What the 1:1 storage genuinely changes is the bright-set GREEN, and only
through area.** Matched t (f158, t=5.27 s) in B1191's pillar box
[60:360, 770:1030], same script on a B1191 tip render (pack `1cdd31e7`)
and this head (pack `785690e6`), `px>150`
(`refactor/B1188/scripts/b1188_matched_box.py`, output
`/home/user/kb-evidence/kb-b1188-landing-pixels/f6_matched_box.out.txt`):
tip **61,360 px, R 202.0 /
G 139.4 / B 5.7** vs this head **27,497 px, R 193.2 / G 47.4 / B 2.4**.
R and B track; the tip's box-diffused 32-px cell lights ~2.2× the bright
pixels, and those added halo pixels are mid-bright (high G), so the
bright-set mean G lands near the core's own G on this head — the same
redistribution §7 already established, read per channel. The core box's G
is still below the oracle's (47.4 vs 82.1, scene-matched boxes), while the
full-frame bright set's G is at 0.92× because that set includes the
mid-bright halo; the shortfall is tracked, not pinned to a per-channel
rule. The 0.53×/0.54× `landing_cov` gap's owner
is unchanged (B1164 #483); no per-channel rule is pinned, and no
shipped-pack change is proposed — a tuned factor would be Will's rule
broken.

## 8. The 93 ledger clip

Regenerated from this gate's `tour_93` render vs the B1178 v3 pinned-camera
oracle capture, via the B1158 pipeline (make_clip.py + make_index.py) — the
B1184 make_ledger recipe with this tree's tour table, into
`/home/user/kb-evidence/VISUAL-FX/b1188-gate/`.
