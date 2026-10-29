# B1191 (#510): landing pixels' GREEN channel "half the oracle's" — per-channel
# measurement, stage trace, and the finding

**Branch:** cut at `origin/fable` `8c2bfe7c2`. **Atlas state: PRE-B1188.** MR !342
(B1188, the 1:1 atlas storage fix) was **NOT merged** when this started, so per the
card I measured on this tree and state the atlas state here: the shipped
`spell_sprite_atlas.bmp` (`data/textures/spell_sprite_atlas.bmp`, sha `fbafab6c…`)
stores its 64 px single-image pillar/flare sprites box-reduced 2:1 into 32 px
cells (`include/client/graphics/environment/spell_sprite_atlas.h:86`
`kCellSize = 32`; the 2:1 resample is
`tools/spell_sprite_atlas_builder.cpp:109` `blitCell`), i.e. B1188's "pre-fix"
diffused state. The pack rendered against is the pin-verified `1cdd31e7…`
(`data/willeq.eqe1.sha256`), rebuilt byte-for-byte from this tree
(`refactor/B1191/scripts/build_packs.sh`; the shared NFS pack had been reinstalled by
the B1188 worker and was **not** used).

## TL;DR

**The "green halved" residual does not reproduce as a 2× halving on the current
tree.** On fresh ThinkPad renders of this tree (pack `1cdd31e7`, 1280×800), the
landing's green channel is **equal to or higher than** the oracle's on the two
warm-colour records (0 and 8), and **0.828–0.912×** the oracle's on the one blue
record (35). **No per-channel stage halves G**: the atlas builder is a clean
per-channel-independent box average, the per-particle tint is white, the blend is
per-channel-identical additive, and ETC1 is a standard codec whose error is
per-channel-correlated and cannot selectively halve one channel. The one open
measurement — the record-35 G shortfall (0.828–0.912×) — **has no named cause**
(every stage in Ask 2 is CLEARED), so by Will's rule it is the **follow-up
defect B1197**, a measured gap to own, not an acceptable or within-noise one.
The B1188 `G 47 vs 93` number was a property of the **B1188 branch state** (its
1:1 atlas's bright-area redistribution), not of this tree; B1188's own fix head
(re-measured, finding 6) shows no record G-halved there either.

## Ask 1 — per-channel tables, three records, both sides, same method

Method (identical on both sides): full 1280×800 frame, per-frame diff vs the
tail-median baseline, the landing's **peak** frame (max lit80 pixel count),
per-channel means over the `lit80` (max channel diff > 80) and `px>150`
(max channel diff > 150) sets (`refactor/B1191/scripts/b1191_check_pair.py`).
**The baseline tail differs per record**: last 30 willeq frames for every record;
oracle tail **60** for rec 0 (its ~500-frame `tour_93` window) and **38** for rec
35/43 (the ~201-frame b1130 window, `b1191_oracle_chan.py`'s 1.5 s tail). The
committed oracle rows reproduce exactly only at those tails — re-running rec 35/43
at 30/60 gives rec 35 oracle 237,979 px (R 9.0 / G 76.5 / B 197.9) and rec 43
oracle 17,986 px (R 154.5 / G 131.1 / B 180.1), not the table below.

**Records picked.** The card names record 80 (= spell 93, `fixed_npc` — B1188 §2
"Record 80 is spell 93, the tour row `tour_93`") plus two short rows from the
b1130 gate (`review/B1184/worker/refactor-B1184/tables/b1130.tsv`, 38 short rows):
**rec 35** (spell 253, `landing_cov` shortfall 7.283→4.796 = **2.487**, rank 7 of
38 by absolute shortfall — the largest is rec 26 at 6.427) and **rec 43** (spell
720, `fixed_self`, 1.176→0.573, ratio **0.487**, rank 16 of 38 by relative
shortfall — the smallest ratio is rec 33 at 0.174).

Per-channel means (R/G/B) over `px>150`, willeq / oracle:

| record | spell | side | px | R | G | B | G/R |
|---|---|---|---|---|---|---|---|
| 80 (rec 0) | 93 | willeq | 213,517 | 199.6 | 149.3 | 11.5 | 0.748 |
| 80 (rec 0) | 93 | oracle | 144,694 | 189.4 | 131.6 | 3.9 | 0.695 |
| **ratio** | | | 1.48× | **1.05** | **1.13** | 2.95 | |
| 35 | 253 | willeq | 162,559 | 22.8 | 69.5 | 174.4 | 3.05 |
| 35 | 253 | oracle | 248,766 | 9.0 | 76.2 | 201.0 | 8.48 |
| **ratio** | | | 0.65× | 2.53 | **0.91** | 0.87 | |
| 43 | 720 | willeq | 10,922 | 187.0 | 157.4 | 163.5 | 0.842 |
| 43 | 720 | oracle | 22,839 | 160.3 | 134.3 | 184.2 | 0.838 |
| **ratio** | | | 0.48× | 1.17 | **1.17** | 0.89 | |

`lit80` gives the same picture (rec 80 G 1.11×, rec 35 G 0.83×, rec 43 G 1.20×).

**Is the green deficit universal or per-record?** It is **not a 2× halving on any
record** on this tree. On the two warm records (0, 8) willeq's G is *higher* than
the oracle's (1.134×, 1.172×). The blue record (35) shows willeq G at **0.828–
0.912×** the oracle's — a B-dominant spell whose bright set is B-led (oracle
B=201 vs G=76). That shortfall is **measured, has no named cause (all four stages
in Ask 2 are CLEARED), and is therefore the follow-up defect B1197**; it is not
treated as small, within-noise, or acceptable. No record reads ≈0.5×.

Like-for-like core box (B1188's scene-matched boxes; each side's own **box-peak**
frame, not a shared frame index — `b1191_scan_box.py`):

| record | side | box | frame | px>150 | R | G | B | G/R |
|---|---|---|---|---|---|---|---|---|
| 0 | oracle r51 | `[0:300, 620:880]` | f186 | 12,603 | 184.8 | 82.1 | 3.6 | 0.44 |
| 0 | willeq r0 | `[60:360, 770:1030]` | f266 | 63,934 | 201.2 | 144.9 | 5.4 | 0.72 |

Here willeq's box-peak core G (144.9 at f266; 139.4 at the matched frame f158) is
**~1.7× the oracle's box-peak** (82.1 at f186) — the opposite of "halved". (The
oracle's flame core runs *redder* than willeq's: G/R 0.44 vs 0.72.)

## Ask 2 — trace the colour of a landing particle, name the halving stage

Every per-channel stage in the path was checked; **none halves G**:

| stage | where | verdict |
|---|---|---|
| Atlas builder (the 2:1 pack) | `tools/spell_sprite_atlas_builder.cpp:109` `blitCell` | **CLEARED.** Alpha-weighted box average, `sumR/sumA`, `sumG/sumA`, `sumB/sumA` — per-channel-independent, no gamma/tint/matting. Cannot shift one channel. |
| Per-particle tint `vColor` | EDD colour words (the **eleven** landing emitters across all three records: rec 80 → 280/281/282/9, rec 35 → 432/433/434, rec 43 → 395/396/397/275; `refactor/B1191/scripts/edd_colour_dump.py`, committed output `edd_colour_dump.out.txt`), `src/client/graphics/sim_particles.cpp` | **CLEARED.** Every one of the eleven landing emitters has start **and** end RGB (255,255,255) → `vColor = (1,1,1)` (white), on every record. No per-particle per-channel factor. |
| Fragment colour / blend | `src/client/graphics/environment/unified_particle_renderer.cpp` (`color = texColor * vColor * fog`, additive `SRC_ALPHA, ONE`) | **CLEARED.** Multiplies by `vColor` (white) and `fog` (per-channel scalar from distance); additive blend is per-channel. No per-channel transformation. |
| ETC1 encode (the pack) | `tools/eqe1_builder.cpp`, the `_alpha` companion | **Cannot be a per-channel halving.** ETC1's diff-mode modifiers (−8/−4/−2/0/+2/+4/+8/flat) apply to **all three channels of a block** — its error is per-channel-correlated, not channel-selective. It can add a few levels of per-channel noise, not halve G. (The ETC1-vs-RGBA8888 render A/B did not isolate the encode: the driver takes a different alpha path per encoding — the RGBA render's B≈0 and half the pixel count show the alpha-companion handling differs — so that A/B is not a clean encode isolation.) |

**No stage names a per-channel halving, because the pipeline is a clean
per-channel copy.** The one open measurement is the record-35 G shortfall
(0.828–0.912×); because every stage above is CLEARED it has no named cause and is
owned as the **follow-up defect B1197** — not waved off as within the per-record
colour mix.

## Reconciliation with B1188's `G 47 vs 93`

B1188's residual (its README §7) was measured on the **B1188 branch** (the 1:1
native atlas, pack `785690e6`) in B1188's own renders — a different tree state
from this pre-B1188 tree. It does not reproduce as a per-channel coefficient here:

- **Like-for-like (the same method on both sides), the tip is at least as blue as
  the B1188 head — not less.** The earlier "B1188's render carried ~8× the blue
  this tree produces, i.e. it was a different render state" line was a
  **measurement-method conflation**: it divided B1130's **absolute** gate census
  (B1188 **B=80**, the rise-mask's absolute pixels) by B1191's **diff-against-tail**
  (tip **B≈9**), which removes the scene background — and that background is
  itself blue (this tip render's full-frame tail-30 median: **R 50.5 / G 49.1 /
  B 79.1**). Run the same method on both sides and the ~8× disappears:
  - B1130 absolute census: B1188 **B=80** vs tip **B=87** (the tip is bluer).
  - B1191 diff-against-tail, `px>150` whole-frame peak: B1188 **B=5.1**
    (`f6_check_pair.out.txt`, rec 0 willeq) vs tip **B=11.5** (rec 0 willeq,
    `refactor/B1191/scripts/B1191_perchannel_summary.txt`). The tip is bluer.
- **B1188's own fix head states this and closes the follow-up.** Branch head
  `a5c2dba8b` (MR !342, finding 6) says the "flame B 80 vs 9" branch difference is
  "a measurement-method mix, not blue", and it performs the re-measure this section
  once promised (old Ask 3, "re-measure on the B1188 branch once MR !342 merges"):
  **no record shows G halved on the B1188 head either** (0.92×/0.90×/1.22×, vs the
  tip's 1.13×/0.83–0.91×/1.17×). Its scene-matched-box core G **47.4** (f158) is a
  **bright-area artifact**, not a per-channel coefficient: in the same box at the
  same frame, the tip reads **61,360 px, R 202.0 / G 139.4 / B 5.7** and the B1188
  head reads **27,497 px, R 193.2 / G 47.4 / B 2.4** — R and B track, but the 1:1
  cell lights ~2.2× fewer bright pixels and the tip's box-diffused 32-px cell's
  added mid-bright halo lifts the tip's bright-set G. Evidence:
  `f6_check_pair.out.txt` / `f6_matched_box.out.txt` in
  `~/kb-evidence/kb-b1188-landing-pixels/`.

This is a like-for-like box/frame comparison **across the two trees** (tip
`8c2bfe7c2` / pack `1cdd31e7` vs B1188 head `a5c2dba8b` / pack `785690e6`); it is
deliberately **not** a "same tree, same pack" claim, because B1188's old §7
"≈56 pre-fix" was measured on the B1188 branch **base** (`ac8c26bc6`,
2026-10-29 03:36 — before the B1185/B1189 merges) with B1188's pack. On **this**
tree/pack the fresh pre-B1188 core G is 139.4 (f158) / 144.9 (box peak f266) —
far above both B1188's ≈56 and the oracle's 82.1. So the real B1188-vs-tip
difference is **bright-area redistribution, not a blue or green deficit** — and
the tip is the bluer of the two. The `G 47 vs 93` is a property of the B1188
branch state; on this tree the per-channel colour is a clean copy (Ask 1/2), and
the one open number is the record-35 G shortfall, owned as **B1197**.

## Ask 3 — fix, test, re-gate

**There is no reproducible 2× halving on this tree, so there is no per-channel
rule to pin here.** Forcing a change to lower G would violate Will's rule (a tuned
factor) and would *move the warm records away* from the oracle (their G already
matches or exceeds it). The disposition:

1. **No code change, no new unit test, no shipped-pack change** on this branch
   (a shipped pack change is a ruling request, not a push — and there is nothing to
   change).
2. **The record-35 G shortfall (0.828–0.912×) has no named cause on a pipeline
   whose four stages are all CLEARED, so it is the follow-up defect B1197.**
   B1197 owns tracing/owning that measured gap; this report does not call it small,
   within-noise, or acceptable.
3. **The B1188-branch re-measure this section once promised is done by B1188's own
   fix head** (`a5c2dba8b`, finding 6): no record shows G halved there either
   (0.92×/0.90×/1.22×), and its box core G 47.4 is a bright-area artifact. So the
   `G 47 vs 93` is the B1188 branch's own tracked residual, not a per-channel
   coefficient to fix here.
4. The B1184/B1188 `landing_cov` re-gate is unchanged by this card (no
   render-affecting change landed), so the existing B1188 gate tables stand as the
   record.

## Acceptance (D) — docs/tests

- This README, the measurement scripts (`refactor/B1191/scripts/`), the per-channel
  summary table (`refactor/B1191/scripts/B1191_perchannel_summary.txt`), and the
  EDD dump + its output (`refactor/B1191/scripts/edd_colour_dump.py` /
  `edd_colour_dump.out.txt`) are the deliverable.
- No C++ change → no new ctest surface; `model_viewer_fx_harness.cpp` is 1,480
  lines (< 1,500). Hygiene (doc/size/citation) is run below; the tree is otherwise
  at the merged tip.
- This MR closes #510 with the measurement + stage trace; the residual the premise
  pointed at (green halved on the current tree) is refuted, and the one open
  measured gap (record-35 G) is handed to **B1197**.

## Hygiene (doc/size/citation)

```
$ python3 -I scripts/refactor/cite_check.py refactor/B1191/README.md --tree . \
    --extra /mnt/projects/fable \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis
checked 3 cites, 0 problems, 0 address warnings

$ ~/.local/bin/rbuild kb-b1191-green-channel hygiene   # ThinkPad
100% tests passed, 0 tests failed out of 49
rbuild: hygiene exit 0 in 252 s on thinkpad
```

## Reproduction

Scripts live under `refactor/B1191/scripts/`. The oracle tail differs per record
(rec 0: 60 frames over its ~500-frame `tour_93` window; rec 35/43: 38 frames over
the ~201-frame b1130 window); the willeq tail is 30 for all. Running 30/60 on
rec 35/43 does **not** reproduce the committed oracle rows (it gives rec 35
237,979 px / R 9.0 G 76.5 B 197.9 and rec 43 17,986 px / R 154.5 G 131.1 B 180.1
instead) — use the per-record tails below.

```bash
# packs (measurement-only; the RGBA8888 one is never shipped) -- ThinkPad
bash refactor/B1191/scripts/build_packs.sh
# renders (same plan/seed/camera; ETC1 pack baseline + RGBA8888 A/B for rec 0) -- ThinkPad, CONC<=2
bash refactor/B1191/scripts/render_all.sh
# per-channel tables (Ask 1), per-record oracle tail (60 for rec 0, 38 for rec 35/43)
python3 -I refactor/B1191/scripts/b1191_check_pair.py <wil_r0>  <orc_r51> 'frame_*.png' 'f*.png' 30 60
python3 -I refactor/B1191/scripts/b1191_check_pair.py <wil_r35> <orc_r35> 'frame_*.png' 'f*.png' 30 38
python3 -I refactor/B1191/scripts/b1191_check_pair.py <wil_r43> <orc_r43> 'frame_*.png' 'f*.png' 30 38
# like-for-like core box (rec 0), each side's own box-peak frame
python3 -I refactor/B1191/scripts/b1191_scan_box.py <wil_r0>  'frame_*.png' 60:360:770:1030 30 30
python3 -I refactor/B1191/scripts/b1191_scan_box.py <orc_r51> 'f*.png'       0:300:620:880  30 30
# EDD colour dump (Ask 2 stage 2, all eleven landing emitters)
python3 -I refactor/B1191/scripts/edd_colour_dump.py      # -> edd_colour_dump.out.txt (committed)
```
