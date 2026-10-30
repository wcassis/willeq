# B1197 (#516): record 35 (spell 253) landing G 0.83-0.91x the oracle -- diagnose leg

DIAGNOSE leg, report only (the card: name the owner with an A/B number, or the two best
candidates with the separating A/B; a FIX leg is cut on the merge of this MR).
Branch cut at origin/fable ff782a71f (the B1188 merge). Render on the ThinkPad
(lane data `--client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1`,
whose sha256 equals this tree's pin in `data/willeq.eqe1.sha256`), measured on
hermes-ext. WILL'S RULE: a measured gap is a
defect until a named cause owns it; never a tuned factor; never "within noise".

## 1. Method (committed scripts, exact commands)

Renders (`refactor/B1197/scripts/render_all.sh`, ThinkPad, CONC=2; A/B re-render
`b1197_ab_render.sh`, same client dir with only `spellsnew.edd` swapped via a
symlink tree over the P1999 files):

```
full   -- no WILLEQ_FX_LANDING_EMITTERS (every drawn slot)
e0     -- WILLEQ_FX_LANDING_EMITTERS=9999 (source+impact only: the Target-free scene)
e432/e433/e434 -- WILLEQ_FX_LANDING_EMITTERS=<n> (B1188's per-emitter isolation)
ab-L   -- e432 with spellsnew.edd record i43 (1-based) +0x7C (life) patched 3.5 -> 8.0
ab-T   -- e432 with record i43 +0x78 (run) patched -5.0 -> -8.0
```

All rows: `model_viewer --spell-test --client <dir> --eqe1 <pack> --plan r35.plan
--fps 30 --seconds 16.8 --sim-hz 60 --seed 1 --headless --camera b1130`, 504 frames
each. `r35.plan` (B1191's, `/home/user/kb-frames/kb-b1191-green-channel/plans/r35.plan`
on the ThinkPad): `t_begin_s=1.5, spell 253, target other, dist 0, cast 1750 ms,
level 60`.

Measurement (hermes-ext): `refactor/B1197/scripts/b1197_chan_series.py` (the extended
Ask-1 table), `b1197_word_dump.py` (the EDD words, 1-based record index = the reader's
convention, `entryBytes` at `src/client/spell/spells_new_edd_reader.cpp:135`),
`b1197_emitter_delta.py` (per-emitter and A/B per-frame contribution),
`b1197_tex_census.py` (the spelab.tga census), `b1197_sprite_census.py` (F2),
`b1197_oracle_base.py` (F1). A/B patch + render: `b1197_edd_ab.py`,
`b1197_ab_render.sh`. Exact commands:

```
python3 refactor/B1197/scripts/b1197_chan_series.py \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/full ~/kb-evidence/kb-b1197-rec35-blue/frames/oracle_e35 \
  'frame_*.png' 'f*.png' 30 25 \
  ~/kb-evidence/kb-b1197-rec35-blue/full_vs_oracle.tsv   # table -> full_vs_oracle.txt
python3 refactor/B1197/scripts/b1197_emitter_delta.py \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e0 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e432:e432 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e433:e433 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e434:e434 \
  ~/kb-evidence/kb-b1197-rec35-blue/emitter_delta.tsv    # -> emitter_delta.txt
python3 refactor/B1197/scripts/b1197_emitter_delta.py \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/e432 \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/ab-L:ab-L \
  ~/kb-evidence/kb-b1197-rec35-blue/frames/ab-T:ab-T \
  ~/kb-evidence/kb-b1197-rec35-blue/ab_delta_vs_e432.tsv # -> ab_delta_vs_e432.txt
python3 refactor/B1197/scripts/b1197_word_dump.py \
  > ~/kb-evidence/kb-b1197-rec35-blue/word_dump.out.txt   # record-35 dump (no args)
python3 refactor/B1197/scripts/b1197_word_dump.py \
  /mnt/projects/EverQuestP1999/spellsnew.edd 280 281 282 432 433 434 \
  > ~/kb-evidence/kb-b1197-rec35-blue/word_dump_280_434.out.txt
python3 refactor/B1197/scripts/b1197_tex_census.py > ~/kb-evidence/kb-b1197-rec35-blue/tex_census.txt
python3 refactor/B1197/scripts/b1197_sprite_census.py > ~/kb-evidence/kb-b1197-rec35-blue/sprite_census.txt
python3 refactor/B1197/scripts/b1197_oracle_base.py > ~/kb-evidence/kb-b1197-rec35-blue/oracle_base.txt
python3 refactor/B1197/scripts/b1197_edd_ab.py /mnt/projects/EverQuestP1999/spellsnew.edd \
  432 ~/kb-evidence/kb-b1197-rec35-blue/edd/spellsnew_ab-L.edd 0x7C 8.0 > ~/kb-evidence/kb-b1197-rec35-blue/edd_ab.txt
python3 refactor/B1197/scripts/b1197_edd_ab.py /mnt/projects/EverQuestP1999/spellsnew.edd \
  432 ~/kb-evidence/kb-b1197-rec35-blue/edd/spellsnew_ab-T.edd 0x78 -8.0 >> ~/kb-evidence/kb-b1197-rec35-blue/edd_ab.txt
```

Oracle side: B1191's committed record-35 capture (branch task/kb-b1191-green-channel),
201 frames (8.04 s at the capture's 25 fps), frames `oracle_e35/`.

## 2. Ask 1: the extended table both sides

Full table: `~/kb-evidence/kb-b1197-rec35-blue/full_vs_oracle.txt` (+ `.tsv` per-frame
histograms). Peak-frame and window ratios (willeq / oracle), committed method:

| set | peak px ratio | per-px R | per-px G | per-px B | window px ratio |
|---|---|---|---|---|---|
| lit80 | 255,695/365,799 = **0.70x** | 17.2/7.7 = 2.21 | 49.7/61.1 = **0.81** | 151.9/171.8 = 0.89 | 35.3M/49.2M = 0.72 |
| px150 | 162,965/248,766 = 0.66 | 22.6/9.0 = 2.51 | 68.5/76.2 = 0.90 | 174.3/201.0 = 0.87 | 22.4M/32.5M = 0.69 |

Peak frames are each side's own lit80 peak (willeq f148 t 4.93, oracle f77 t_o 3.08;
time-aligned offset willeq t - oracle t = +1.85 s). Window means: lit80 per-px
R 18.0/8.1 = 2.21, G 51.3/59.0 = 0.87, B 151.5/169.6 = 0.89; px150 G 70.7/74.9 = 0.94,
B 174.5/200.3 = 0.87. The card's green band 0.83-0.91 (B1191's f2 finding, its
steady-core 0.828-0.912x) is reproduced: per-frame green ratio 0.840-0.990 across
f104..f232 (the sustained below-1 sub-window; 0.840-1.240 over f58..f269, >1 only in
the early ramp f58..f103); per-frame blue 0.860-0.880 across f104..f232.
ROI split (each side, its own peak px150 centroid; core 209x209 / halo 418x418
minus-core / ground the rest; window lit80 px): core 2,925,471/4,807,309 (0.61),
halo 7,832,625/12,905,123 (0.61), ground 24,583,221/31,457,101 (0.78); the oracle's
extra pixels are in every ROI: ground +6,873,880 > halo +5,072,498 > core +1,881,838.

## 3. Ask 2: the three Target emitters, word by word

Record 35's Target stage (spellsnew.eff record 104, spell 253) names EDD 432/433/434
(`GlowGlyphs3-1/-2/-3`, the 1-based reader index), all `anchor=target attach=CHEST`,
minLevel 24/0/39 (all drawn at level 60: `createLanding ... (of 3)`).
`word_dump.out.txt` (432-434) and `word_dump_280_434.out.txt` (the record-80 contrast,
280-282). **All three sample the SAME texture, `SpellEffects/spelab.tga`** (the blue
glyph), additive (+0x68 = 1), white start/end colour (+0xD4..+0xE8 = 255,255,255),
peak alpha 1.25, size 1.5x1.5 fixed, spawn shape 5 (rX 5, rY 4, h 0, no offsets/tilts),
radial 1..2, gravity 2, driftX 25, 16 frames @ 12 fps, `+0x19C scale_size` = 1.4e-45
(denormal; every emitter of both records carries it, so it is not a size scale).
The words that differ:

| EDD | name | run +0x78 | life +0x7C | burst +0x80 | per-tick +0x84 | ticks/s +0x88 | delay +0x8C | fade-in/out +0x90/+0x94 | file population (burst + perTick x tps x min(T,L)) |
|---|---|---|---|---|---|---|---|---|---|
| 432 | GlowGlyphs3-1 | -5.0 | 3.5 | 2 | 3 | 75 (225/s) | 0 | 0.1 / 3.25 | 789.5 live |
| 433 | GlowGlyphs3-2 | -5.0 | 3.0 | 2 | 3 | 25 (75/s) | 0 | 0.1 / 4.0 | 227 |
| 434 | GlowGlyphs3-3 | -5.0 | 3.0 | 2 | 2 | 25 (50/s) | 0 | 0.1 / 4.0 | 152 |

432 is 3x denser (225/s vs 75/50; 789 vs 227/152 peak live). The three together are
the Target stage's 1168 peak live (the `config/spell_effects.json` landingPopulation
rule's own census).

The texture itself (measured from the P1999 file, 256x256x32-bit TGA, 18-byte header,
bottom-up BGRA -- `b1197_tex_census.py`): alpha = 255 on 100% of texels; 14.02% of
texels are lit80 (9,188 of 65,536, extent rows 13..242, cols 12..241 -- a full-bleed
glyph, not a dot); their mean RGB = **3.9 / 50.1 / 189.6** (B/G = 3.8); **R max = 33,
96.9% of all texels have R < 1, no texel has R > 48**. The per-pixel added light of
the ORACLE at its aligned peak, core ROI (10.1, 68.7, 187.0), is this texel census
times about 1.15 (additive overlap) -- the oracle draws the file's own texels. The
willeq per-pixel (21.3, 57.7, 153.0) is 2.2x the R of the file's lit80 texels at a
similar B -- see F4.

Per-emitter isolated renders (B1188's filter, baseline = the e0 scene;
`b1197_emitter_delta.py`):

| set | window | peak | window lit80 px | per-px R/G/B |
|---|---|---|---|---|
| e432 | f108..f267 (t 3.60..8.90) | f147 (t 4.90) n=95,249 | 9,630,118 | 16.6 / 56.1 / 115.1 |
| e433 | f108..f258 (t 3.60..8.60) | f154 (t 5.13) n=80,838 | 7,599,473 | 16.0 / 55.4 / 112.9 |
| e434 | f108..f262 (t 3.60..8.73) | f154 (t 5.13) n=79,819 | 7,418,849 | 16.1 / 55.9 / 111.7 |

**No single emitter owns the gap.** All three start at the same frame (f108), end
within 0.3 s of each other (8.60-8.90), and their per-px hue is within 13%
(e432's G is the highest of the three, 56.1 vs 55.4/55.9). The count split follows
the file populations (39% / 31% / 30% of the 24.65M summed isolated px; the
full-minus-e0 light is 10.84M px -- the three overlap on the same pixels, union not
sum). The G deficit and the count deficit are the Target stage's, shared by all
three emitters drawing the same texture.

## 4. Findings

**F1. The oracle capture never shows the pre-light scene.** `oracle_base.txt`
(`b1197_oracle_base.py`): lit80 >= 50 in 178 of 201 frames, first f4 (t 0.16), last
f196 (t 7.84); the rise above frame 1 is 102,809 px by f10 (t 0.40), peaks at 252,978
(t 3.00), and is still 17,258 in the last frame (t 8.00). Frame-1 mean R/G/B
[58.4, 81.2, 94.7]; last-38 mean [58.5, 73.0, 66.2] (delta R +0.1, G -8.2, B -28.4).
The 8.04 s capture sits entirely inside the light, and the committed tail-38
baseline (t 6.52..8.04) contains decaying light: every oracle "rise" number is
light(t) minus a light-contaminated baseline, so the oracle's window px is
undercounted and the 0.72x window ratio (section 2) is a LOWER BOUND on the deficit.
The willeq tail-30 is clean (its light ends at 9.20 s, the tail starts at 15.80 s).
The fix leg needs a clean pre-light oracle frame (a re-capture started earlier)
before any per-px ratio is final.

**F2. Structure: the oracle light is one continuous core; willeq's is scattered
glyphs.** `sprite_census.txt` (`b1197_sprite_census.py`, re-runnable from the frames):
at its peak (f77, t_o 3.04) the oracle lit80 mask is 471 components of which ONE is
333,770 px (92% of the 362,843 total) with a fine halo (median component 3 px); the
willeq e432 run at its peak (f147, t 4.90) is 928 components, largest 7,518 px,
median 8. The oracle's core is 299k..334k px sustained t_o 2.2..5.2, collapses
306,963 -> 120,198 between t_o 5.16 and 6.16, and is gone by t_o 7.60 (2,285 px at
f191). Willeq's e432 plateaus 95k -> 76k over t 3.60..7.00, then decays 76k -> 0 over
t 7.00..8.97. Same texture, same blend, white tint: the difference is overlap
density (population x placement x size), not encoding.

**F3. The willeq Target light is ~3.1 s shorter than the model, the EDD timing
words reach the effect as the F, and neither changes the timing.** The model
(`SpellFxSlotTimingFor`, `include/client/spell/spell_fx_timing.h:127`: +0x78 < 0 =>
fixed run = |+0x78| = 5.0, start = +0x8C = 0, window() = 5.0, lifetime = +0x7C = 3.5,
end() = 8.5 from the stage start; `applySlotEmitter`
(`src/client/graphics/environment/spell_emitter_presets.cpp:331`) sets
`emitterLifetime` to `max(t.window(), 0.05)` (its line 430) and
`lifetimeMin`/`lifetimeMax` to L (its lines 449-450), its trigger delay (its lines
423-425) is the 0.33 s stage latency; the slot loop's `createSpellEffect` enqueue
(`src/client/graphics/spell_visual_fx.cpp:1060`) passes duration 0.0 = "until
emitters expire", `include/client/graphics/environment/spell_particle_types.h:48`) predicts the
isolated e432 light 3.58..12.08 s (effect created at cast-complete 3.25 = 1.5 + 1.75;
trigger +0.33; emission 5.0; particles to +3.5; first lit frame measured 3.60).
Measured: isolated e432 3.60..8.93 (window ~5.3 s; the committed table's window
f108..f267 at its own threshold); the full run's Target-only light
(full minus e0, which strips the caster glow) 3.60..9.20, peak f190 (t 6.33)
n=104,728 (`full_vs_e0.txt`) -- 3.1 s short of the model, the full path 0.3 s longer
than the isolated path (the harness filter changes the end). **The def holds the
EDD timing -- proven in each variant's own log** (`frames/*/run.log`, the
`createLanding` line, `0.33 + window + life`): e432 `+0.33..8.83`,
ab-L `+0.33..13.33`, ab-T `+0.33..11.83` -- exactly the patched words. A/B
(`b1197_edd_ab.py`; only `spellsnew.edd` swapped in a symlink tree over the P1999
files, `edd_ab.txt` names the patch bytes: ab-L first differing byte 179,846 =
record i43's +0x7C word, ab-T 179,842 = +0x78; the reader is 1-based per
`entryBytes`, `src/client/spell/spells_new_edd_reader.cpp:135-137`):
- ab-T (+0x78: -5.0 -> -8.0, the window 5.0 -> 8.0): **all 504 frames bit-identical
  to the baseline** (md5 per frame) -- the window word moves no pixel.
- ab-L (+0x7C: 3.5 -> 8.0, the particle life): the deviation from baseline spans
  f141..f267 with signed window integral +364,393 px (+3.8% of 9,630,120); the
  isolated light's last lit frame is 8.97 vs the baseline's 8.93 (+1 frame) --
  no end extension, against a +4.5 s documented effect.
Excluded, each by measurement: a stale binary (the ThinkPad build is from this tip:
md5 of the four critical TUs matches, all object files newer than the source copy,
full rebuild); the particle pool (`spellParticlePool 8192 (desktop)` in every
run.log vs the record's 4914 peak live); the EDD read (the logs above show the
patched words loaded); the config (`config/spell_effects.json`: `applyPool` scales
NO-RECORD spells only, B1123/B1149; `targetStageLatency()` = 0.33 matches the log);
the effect envelope (the landing's `createSpellEffect` duration is 0.0,
`src/client/graphics/spell_visual_fx.cpp:1060`, so `maxDuration` = 0 = "until
emitters expire", `src/client/graphics/sim_particles.cpp:191`; and `releaseCastGlow`
no-ops for record spells on the `fromFile` check
(`src/client/graphics/spell_visual_fx_nodes.cpp:93`));
the art (F2/F4, the census matches the oracle's per-px).

**F4. The per-px hue gap is a population/sampling mix, not a tint.** Both sides
draw the same file's texels white-tinted additive (section 3). Willeq's per-px R is
2.2x the oracle's across the whole window (R histogram, window px: willeq bin 0 =
20,861,231 (59.0%), bins 0-1 = 84.6%; oracle bin 0 = 40,720,593 (82.8%), bins 0-1 =
94.0% -- the oracle's added light is mostly zero-R). Willeq's lit80 set is the
~900 scattered, minified single-slate sprites (F2): minification averages each
~20 px sprite over ~10 texels, raising the per-px R above the file's 3.9 while the
B falls under the file's 189.6 (the 80-threshold keeps the dim, red-tinged averaged
texels the oracle's dense core does not show). The G deficit (0.81-0.94) and the B
deficit (0.87-0.89) are the same population gap seen per pixel.

## 5. Owner

**The sim-side landing-particle lifecycle does not consume the def's EDD timing
(named by exclusion, F3).** The EDD words reach the def (the per-variant logs), the
def is enqueued by value copies
(`src/client/graphics/environment/particle_manager.cpp:971-980`,
`src/client/graphics/sim_particles.cpp:191-192`, the worker's
`ae.config = emDef.config` at `:657`), the binary is proven to be this tip, and the
worker's source at the tip applies those fields (emitter age/stop
`sim_particles.cpp:345-348`, tick spawn `:430-435`, particle death `:470`, the only
deactivation gates being `emitterLifetime` and the inactive `maxDuration`
`:602-607`) -- yet the measured emission (~5.0-5.3 s of effect age) and the
particles' lit80 visibility (~0.4-1.4 s) are fixed regardless of the def's 5.0/8.0
window (ab-T bit-identical) and 3.5/8.0 life (ab-L +3.8%). The measured behaviour
contradicts the source as written; the owner line is in the worker's
emitter/particle clock.

Two candidates, each with its separating A/B (the fix leg's first runs):

**C1 (primary): a worker-side clock gate this leg did not find** -- something ends
the emitter at ~5.0-5.3 s of effect age and caps particle visibility, independent
of the def values. SEPARATING A/B (one instrumented e432 render): log
`ae.config.emitterLifetime / lifetimeMin / lifetimeMax` at trigger
(`sim_particles.cpp:656-677`) and `emitter.emitterAge` at deactivation
(`:345-348`). If the logged def is 5.0/3.5 while the FX log says 8.x, the consumed
def differs from the logged one (loss in the command/instance path); if the logged
def is 8.x and deactivation still happens at ~5.3, the second gate is named; if the
logged def is 8.x and the emitter runs to 8.x, the rise metric hides the extension
(already disfavoured: ab-T's pixels are bit-identical).

**C2: the placement/footprint scatters willeq's sprites while the oracle's merge
into one core** (F2: shape 5, rX 5 / rY 4, radial 1..2, gravity 2, driftX 25). C2
owns the structure gap and (with F4) the per-px mix, NOT the timing (F3).
SEPARATING A/B: patch 432's +0xA8 spawn_shape 5 -> 0 (a point) on the isolated e432
render; if C2 owns the structure gap the component census collapses toward fewer,
larger components (mean component area up ~3-5x) at the same window integral; if
the census is unchanged the scatter is population, back to C1.

The A/B mechanism is proven (this leg ran both words): `b1197_edd_ab.py` +
`b1197_ab_render.sh`, `ab_delta_vs_e432.{txt,tsv}`, `edd_ab.txt`.

## 7. Cite check

`python3 -I scripts/refactor/cite_check.py refactor/B1197/README.md --tree .
--extra /mnt/projects/fable --extra /home/user/kb-evidence`:

    checked 13 cites, 0 problems, 0 address warnings

## 8. Evidence

- `~/kb-evidence/kb-b1197-rec35-blue/`: `full_vs_oracle.{txt,tsv}`,
  `full_vs_e0.{txt,tsv}`, `emitter_delta.{txt,tsv}`, `ab_delta_vs_e432.{txt,tsv}`,
  `word_dump.out.txt`, `word_dump_280_434.out.txt`, `sprite_census.txt`,
  `tex_census.txt`, `oracle_base.txt`, `edd_ab.txt`, `edd/spellsnew_ab-{L,T}.edd`.
- Frames (kept for the fix leg's A/B baseline; the A/B re-render of this leg is
  bit-identical to the kept sets, 504/504 each):
  `~/kb-evidence/kb-b1197-rec35-blue/frames/{full,e0,e432,e433,e434,ab-L,ab-T,oracle_e35}/`.
- Oracle capture provenance: B1191 (branch task/kb-b1191-green-channel, MR !343),
  its committed scripts; the b1130 camera row.
