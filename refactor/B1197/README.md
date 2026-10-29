# B1197 (#516): record 35 (spell 253) landing G 0.83-0.91x the oracle -- diagnose leg

DIAGNOSE leg, report only (the card: name the owner with an A/B number, or the two best
candidates with the separating A/B; a FIX leg is cut on the merge of this MR).
Branch cut at origin/fable ff782a71f (the B1188 merge). Render on the ThinkPad
(lane data `--client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1`,
whose sha256 equals this tree's pin in `data/willeq.eqe1.sha256`), measured on
hermes-ext. WILL'S RULE: a measured gap is a
defect until a named cause owns it; never a tuned factor; never "within noise".

## 1. Method (committed scripts, exact commands)

Renders (`refactor/B1197/scripts/render_all.sh`, ThinkPad, CONC=2; A/B renders
`b1197_ab_render.sh`, appended to the same frames dir):

```
full   -- no WILLEQ_FX_LANDING_EMITTERS (every drawn slot)
e0     -- WILLEQ_FX_LANDING_EMITTERS=9999 (source+impact only: the Target-free scene)
e432/e433/e434 -- WILLEQ_FX_LANDING_EMITTERS=<n> (B1188's per-emitter isolation)
ab-L   -- e432 with spellsnew.edd record 432 +0x7C (life) patched 3.5 -> 8.0
ab-T   -- e432 with spellsnew.edd record 432 +0x78 (run) patched -5.0 -> -8.0
```

All rows: `model_viewer --spell-test --client <dir> --eqe1 <pack> --plan r35.plan
--fps 30 --seconds 16.8 --sim-hz 60 --seed 1 --headless --camera b1130`, 504 frames
each. `r35.plan` (B1191's, `/home/user/kb-frames/kb-b1191-green-channel/plans/r35.plan`
on the ThinkPad): `t_begin_s=1.5, spell 253, target other, dist 0, cast 1750 ms,
level 60`.

Measurement (hermes-ext): `refactor/B1197/scripts/b1197_chan_series.py` (the extended
Ask-1 table), `b1197_word_dump.py` (the EDD words, 1-based record index = the reader's
convention, `entryBytes` at `src/client/spell/spells_new_edd_reader.cpp:135`),
`b1197_emitter_delta.py` (per-emitter and A/B per-frame contribution). Exact commands:

```
python3 refactor/B1197/scripts/b1197_chan_series.py \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/full ~/kb-evidence/kb-b1197-rec35-blue/frames/oracle_e35 \
  'frame_*.png' 'f*.png' 30 38 \
  ~/kb-evidence/kb-b1197-rec35-blue/full_vs_oracle.tsv   # table -> full_vs_oracle.txt
python3 refactor/B1197/scripts/b1197_emitter_delta.py \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e0 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e432:e432 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e433:e433 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e434:e434 \
  ~/kb-evidence/kb-b1197-rec35-blue/emitter_delta.tsv
python3 refactor/B1197/scripts/b1197_emitter_delta.py \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e432 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/ab-L:ab-L \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/ab-T:ab-T \
  ~/kb-evidence/kb-b1197-rec35-blue/ab_delta.tsv
python3 refactor/B1197/scripts/b1197_word_dump.py \
  /mnt/projects/EverQuestP1999/spellsnew.edd 432 433 434 \
  > ~/kb-evidence/kb-b1197-rec35-blue/word_dump.out.txt
python3 refactor/B1197/scripts/b1197_word_dump.py \
  /mnt/projects/EverQuestP1999/spellsnew.edd 280 281 282 432 433 434 \
  > ~/kb-evidence/kb-b1197-rec35-blue/word_dump_280_434.out.txt
python3 refactor/B1197/scripts/b1197_tex_census.py   # spelab.tga census (section 3)
python3 refactor/B1197/scripts/b1197_sprite_census.py \
  > ~/kb-evidence/kb-b1197-rec35-blue/sprite_census.txt   # F2
python3 refactor/B1197/scripts/b1197_oracle_base.py   # F1 (the oracle baseline check)
```

Oracle side: B1191's committed record-35 capture (branch task/kb-b1191-green-channel),
201 frames at 25 fps (8.04 s), frames `oracle_e35/`.

## 2. Ask 1: the extended table both sides

Full table: `~/kb-evidence/kb-b1197-rec35-blue/full_vs_oracle.txt` (+ `.tsv` per-frame
histograms). Peak-frame and window ratios (willeq / oracle), committed method:

| set | peak px ratio | per-px R | per-px G | per-px B | window px ratio |
|---|---|---|---|---|---|
| lit80 | 255,695/365,799 = **0.70x** | 17.2/7.7 = 2.21 | 49.7/61.1 = **0.81** | 151.9/171.8 = 0.89 | 35.3M/49.2M = 0.72 |
| px150 | 162,965/248,766 = 0.66 | 22.6/9.0 = 2.51 | 68.5/76.2 = 0.90 | 174.3/201.0 = 0.87 | 22.4M/32.5M = 0.69 |

The per-frame time-aligned G ratio (willeq t - oracle t = +1.85 s, each side's own
lit80 peak) runs 0.86-0.90 across the willeq lit80 window f58..f269, 0.93-0.97 in the
late tail -- the card's 0.83-0.91 (B1191's f2 finding) reproduced. B-channel ratio
0.86-0.89 across the window. ROI split (each side, its own peak px150 centroid;
core 209x209 / halo 418x418-minus-core / ground the rest, window lit80 px):
core 2,925,471/4,807,309; halo 7,832,625/12,905,123; ground 24,583,221/31,457,101 --
the oracle's extra pixels are in every ROI, largest in ground (+6.87M) then halo
(+5.07M), core ratio 0.61.

## 3. Ask 2: the four landing emitters, word by word

Record 35's Target stage names EDD 432/433/434 (`GlowGlyphs3-1/-2/-3`, the 1-based
reader index). `word_dump.out.txt` (432-434) and `word_dump_280_434.out.txt` (the
record-80 contrast, 280-282). **All three Target emitters sample the SAME texture,
`SpellEffects/spelab.tga`** (the blue glyph), and the words agree on the essentials:
additive blend (+0x68 = 1), white start/end colour (+0xD4..+0xE8 = 255,255,255),
432: run +0x78 = -5.0, life +0x7C = 3.5, burst +0x80 = 2, per-tick +0x84 = 3,
ticks/s +0x88 = 75 (225/s), delay +0x8C = 0, fade-in 0.1 / fade-out 3.25,
peak alpha 1.25, size 1.5x1.5 fixed, spawn shape 5 (rX 5, rY 4), radial 1..2,
gravity 2, driftX 25, frames 16 @ 12 fps, +0x19C = 1.4e-45 (denormal; every emitter
of both records carries it, so it is not a size scale -- record 80's verified-correct
emitters do too).

The texture itself (measured from the P1999 file, 256x256x32-bit TGA, 18-byte header,
bottom-up BGRA): alpha = 255 on every texel; 14.0% of texels are lit80 (9,188 of
65,536); their mean RGB = **3.9 / 50.1 / 189.6** (B/G = 3.8); **no texel has R > 48**
(97% have R < 1). The per-pixel added light of the ORACLE at its aligned peak, core
ROI (10.1, 68.7, 187.0), is this texel census times about 1.15 (additive overlap) --
the oracle draws the file's own texels. The willeq per-pixel (21.3, 57.7, 153.0) is
2.1x the R of the file's lit80 texels at a similar B -- see F4.

Per-emitter isolated renders (B1188's method): e432 lit80 window f108..f268
(t 3.60..8.93), peak f147 (t 4.90) n=95,249, window n=9,630,120; e433 peak f154
n=80,838, window n=7,599,493; e434 peak f154 n=79,819, window n=7,418,849
(`emitter_delta.txt`). e432 carries the most G of the three (its per-px G at the
peak is 13% above e433/e434) but all three are the same light -- the count gap is
the whole Target stage's, not one emitter's.

## 4. Findings

**F1. The oracle capture never shows the pre-light scene.** The light is already at
102,809 lit80 px in oracle frame 1 (t 0.00) and still at 1,033 px in the last frame
(t 8.00): the 8.04 s capture sits entirely inside the light. The committed tail-38
baseline (the last 38 frames, t 6.52..8.04) therefore contains decaying light:
last-38 mean minus frame 1 = (0.3, -8.2, -28.4) in R/G/B. Every oracle "rise" number
in section 2 is light(t) minus a light-contaminated baseline -- a LOWER BOUND. The
oracle's true per-px G is at least 0.81-0.90x willeq's, probably less; the fix leg
needs a clean pre-light oracle frame (a re-capture with an earlier start) before any
per-px ratio is final. `oracle_e35/`, measured `full_vs_oracle.txt` lines 19-22.

**F2. Structure: the oracle light is one continuous core; willeq's is scattered
glyphs.** At the aligned peak, the oracle lit80 mask is 471 components of which ONE
is 333,770 px (92% of the 362,843 total) with a fine halo (median component 3 px);
the willeq full run is 928 components, largest 7,518 px, median 8 (`sprite_census.txt` -- `b1197_sprite_census.py`, re-runnable from the frames). The oracle's light is a continuous ~50
world-unit glow; willeq's is individual ~20 px minified glyph sprites that never
merge. Same texture, same blend, white tint: the difference is overlap density
(population x placement x size), not encoding.

**F3. The willeq Target light is ~2.5 s shorter than the B1145 model, and the EDD
timing words do not move it.** The model (`include/client/spell/spell_fx_timing.h:127`,
`SpellFxSlotTimingFor`: +0x78 < 0 => fixed run = |+0x78| = 5.0, start = +0x8C = 0,
window = 5.0; lifetime = +0x7C = 3.5; the last particle dies at window + life =
8.5 s from the stage start) predicts the isolated e432 light 3.55..12.05 (the stage
starts ~3.55: cast 1.5 + 1.75 + the measured stage latency; first lit frame 3.60).
Measured: isolated e432 3.60..8.93 (window 5.33 s); the full run's Target-only light
(full minus e0, which strips the caster glow) 3.60..9.20, peak t 6.33 n=104,728
(`full_vs_e0.txt`) -- 2.5-3.1 s short of the model, and the full path is 0.3 s
longer than the isolated path (the harness filter changes the end). A/B on the
isolated path (patch bytes verified in the loaded file: `spellsnew_ab-L.edd` differs
from P1999 first at byte 179847 = record 432's +0x7C..+0x7F L-word; the reader is
1-based per `entryBytes`, `src/client/spell/spells_new_edd_reader.cpp:135-137`;
symlink chain `eq253-ab-L/spellsnew.edd -> spellsnew_ab-L.edd`):
- ab-T (+0x78: -5.0 -> -8.0): **bit-identical to baseline** (0 differing pixels on
  11 sampled frames).
- ab-L (+0x7C: 3.5 -> 8.0): window lit80 integral +4.4% (10.05M vs 9.63M), last lit
  frame 8.97 vs 8.93 (+1 frame). Neither word's documented effect (extend the window
  to 8.0 s / the particles to live 8.0 s) is visible.

**F4. The per-px hue gap is a population/sampling mix, not a tint.** Both sides draw
the same file's texels white-tinted additive (section 3). Willeq's per-px R is 2.2x
the oracle's across the whole window (histograms: willeq R mass in bins 0-1 =
20.9M of 35.3M, oracle 40.7M of 49.2M -- the oracle's added light is mostly
zero-R; willeq's is not). Willeq's lit80 set is the ~900 scattered, minified
single-slate sprites (F2): minification averages each 20 px sprite over ~10 texels,
raising the per-px R above the file's 3.9 while the B falls under the file's 189.6
(the 80-threshold keeps the dim, red-tinged averaged texels that the oracle's dense
core does not show). The G deficit (0.81-0.90) and the B deficit (0.87-0.89) are the
same population gap seen per pixel.

## 5. Owner: two candidates, the separating A/B

**C1 (primary): the Target emission window is not the run word's 5.0 s.** The code
path says window = 5.0 (`src/client/graphics/environment/spell_emitter_presets.cpp:430`,
`c.emitterLifetime = std::max(t.window(), kOneStep)`;
`src/client/graphics/sim_particles.cpp:347` deactivates at
`emitterLifetime`) with particles to +3.5 s; the measured light ends 2.5-3.1 s early
and neither +0x78 nor +0x7C moves it in the isolated harness path (F3). The oracle's
plateau (340-365 k lit80 sustained t_o 2.5..5.5 = ~3 s at steady state,
rate x life = 225 x 3.5 = 787 live) plus its decay (150 k @ 6.16 -> 1 k @ 7.84)
fits a ~5.0 s emission window; willeq's Target-only light peaks 1.4 s after the
oracle's aligned peak (6.33 vs 4.93), i.e. its population ramps slower -- consistent
with a ~3.0-3.5 s window (peak live 675 vs 787, the 0.70-0.85x px ratio).
SEPARATING A/B (full render, not the isolated path, whose timing already diverges
from the full path in baseline): patch 432's +0x78 -5.0 -> -1.0 and re-render `full`
+ `e0`; prediction if C1 owns it: the Target-only light ends ~stage+4.5 (not 9.20)
and the plateau n falls toward ~70 k; if the end stays ~9.20, the window is set
elsewhere and the search moves to the stage-latency/plan timing.

**C2: the placement/footprint (spawn shape 5, rX 5 / rY 4, radial 1..2, gravity 2,
driftX 25) scatters willeq's sprites while the oracle's merge into one core** (F2).
Even at equal population this alone changes the per-px mix (F4). SEPARATING A/B:
patch 432's +0xA8 spawn_shape 5 -> 0 (a point) on the isolated e432 render;
prediction if C2 owns the structure gap: the e432 component census collapses toward
fewer, larger components (mean component area up ~3-5x) at the same window integral;
if the census is unchanged the scatter is not the shape word and the density
difference is population (back to C1).

The A/Bs are the fix leg's first runs (this leg is report-only; the patched-EDD
mechanism is proven: `b1197_edd_ab.py`-style patches, `ab_delta.txt`).

## 7. Cite check

`python3 -I scripts/refactor/cite_check.py refactor/B1197/README.md --tree .
--extra /mnt/projects/fable --extra /home/user/kb-evidence`:

    checked 5 cites, 0 problems, 0 address warnings

## 8. Evidence

- `~/kb-evidence/kb-b1197-rec35-blue/`: `full_vs_oracle.{txt,tsv}`,
  `full_vs_e0.{txt,tsv}`, `emitter_delta.{txt,tsv}`, `ab_delta.{txt,tsv}`,
  `word_dump.out.txt`, `word_dump_280_434.out.txt`, `sprite_census.txt`.
- Frames (kept for the fix leg's A/B baseline):
  `/mnt/scratch/kb-evidence/kb-b1197-rec35-blue/frames/{full,e0,e432,e433,e434,ab-L,ab-T,oracle_e35}/`.
- Oracle capture provenance: B1191 (branch task/kb-b1191-green-channel, MR !343),
  its committed scripts; the b1130 camera row.
