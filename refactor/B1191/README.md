# B1191 (#510): landing pixels' GREEN channel "half the oracle's" — per-channel
# measurement, stage trace, and the finding

**Branch:** cut at `origin/fable` `8c2bfe7c2`. **Atlas state: PRE-B1188.** MR !342
(B1188, the 1:1 atlas storage fix) was **NOT merged** when this started, so per the
card I measured on this tree and state the atlas state here: the shipped
`spell_sprite_atlas.bmp` (sha `fbafab6c…`, `data/textures/spell_sprite_atlas.bmp`)
stores its 64 px single-image sprites (the `spelcf` pillar included) **box-reduced
2:1 into 32 px cells** (`include/client/graphics/environment/spell_sprite_atlas.h:86`
`kCellSize = 32`; the 2:1 resample is
`tools/spell_sprite_atlas_builder.cpp:109` `blitCell`), i.e. B1188's "pre-fix"
diffused state. The pack rendered against is the pin-verified `1cdd31e7…`
(`data/willeq.eqe1.sha256`), rebuilt byte-for-byte from this tree
(`review/B1191/scripts/build_packs.sh`; the shared NFS pack had been reinstalled by
the B1188 worker and was **not** used).

## TL;DR

**The "green halved" residual does not reproduce on the current tree.** On fresh
ThinkPad renders of this tree (pack `1cdd31e7`, 1280×800), the landing's green
channel is **equal to or higher than** the oracle's for the two warm-colour
records, and only mildly (0.83–0.91×) lower on the one blue record. **No per-channel
stage halves G**: the atlas builder is a clean per-channel-independent box average,
the per-particle tint is white, the blend is per-channel-identical additive, and ETC1
is a standard codec whose error is per-channel-correlated and cannot selectively
halve one channel. The B1188 `G 47 vs 93` number was measured on the **B1188 branch
(1:1 native atlas)** in B1188's own render, which was measurably different from a
fresh render of this tree — the residual is **branch-specific, not present here**.
No source fix is warranted on this tree; the follow-up is to re-measure on the B1188
branch once MR !342 merges.

## Ask 1 — per-channel tables, three records, both sides, same method

Method (identical on both sides): full 1280×800 frame, per-frame diff vs the
tail-median baseline (last 30 willeq / 60 oracle frames), the landing's **peak**
frame, per-channel means over the `lit80` (max channel > 80) and `px>150`
(max channel > 150) sets. `review/B1191/scripts/b1191_check_pair.py`.

**Records picked.** The card names record 80 (= spell 93, `fixed_npc` — B1188 §2
"Record 80 is spell 93, the tour row `tour_93`") plus two of the short rows with the
largest `landing_cov` shortfall from the b1130 gate
(`review/B1184/worker/…/tables/b1130.tsv`): **rec 35** (spell 253, shortfall
7.283→4.796 = **2.487**, the largest absolute) and **rec 43** (spell 720, `fixed_self`,
1.176→0.573, ratio **0.487**, the largest relative).

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

**Is the green deficit universal or per-record?** It is **not present at all** on
this tree for the warm records (80, 43): willeq's G is *higher* than the oracle's
(1.13×, 1.17×). Only the blue record (35) shows a mild G deficit (0.83–0.91×), and
that is a blue-dominant spell where the bright set's channel mix is B-led
(oracle B=201 vs G=76), so a small absolute G gap is within the per-record colour
differences, not a 2× halving. **No record shows G halved (≈0.5×).**

Like-for-like core box (B1188's scene-matched boxes, each side's core peak):

| side | box | px>150 | R | G | B | G/R |
|---|---|---|---|---|---|---|
| oracle r51 | `[0:300, 620:880]` | 12,603 | 184.8 | 82.1 | 3.6 | 0.44 |
| willeq r0 | `[60:360, 770:1030]` | 63,934 | 201.2 | 144.9 | 5.4 | 0.72 |

Here willeq's core G (144.9) is **1.77× the oracle's** (82.1) — the opposite of
"halved". (The oracle's flame core runs *redder* than willeq's: G/R 0.44 vs 0.72.)

## Ask 2 — trace the colour of a landing particle, name the halving stage

Every per-channel stage in the path was checked; **none halves G**:

| stage | where | verdict |
|---|---|---|
| Atlas builder (the 2:1 pack) | `tools/spell_sprite_atlas_builder.cpp:109` `blitCell` | **CLEARED.** Alpha-weighted box average, `sumR/sumA`, `sumG/sumA`, `sumB/sumA` — per-channel-independent, no gamma/tint/matting. Cannot shift one channel. |
| Per-particle tint `vColor` | EDD colour words (`review/B1141/edd_fields_from_code.tsv`; the landing emitters 280/281/282/9), `src/client/graphics/sim_particles.cpp` | **CLEARED.** Every landing emitter's start **and** end RGB is (255,255,255) → `vColor = (1,1,1)` (white). No per-particle per-channel factor. |
| Fragment colour / blend | `src/client/graphics/environment/unified_particle_renderer.cpp` (`color = texColor * vColor * fog`, additive `SRC_ALPHA, ONE`) | **CLEARED.** Multiplies by `vColor` (white) and `fog` (per-channel scalar from distance); additive blend is per-channel-identical. No per-channel transform. |
| ETC1 encode (the pack) | `tools/eqe1_builder.cpp`, the `_alpha` companion | **Cannot be a per-channel halving.** ETC1's diff-mode modifiers (−8/−4/−2/0/+2/+4/+8/flat) apply to **all three channels of a block** — its error is per-channel-correlated, not channel-selective. It can add a few levels of per-channel noise, not halve G. (The ETC1-vs-RGBA8888 render A/B did not isolate the encode: the driver takes a different alpha path per encoding — the RGBA render's B≈0 and half the pixel count show the alpha-companion handling differs — so that A/B is not a clean encode isolation.) |

**No stage names itself the halving stage, because the halving is not there.** The
measured per-channel ratios (Ask 1) are consistent with a clean copy of the client's
colour pipeline: R and G track the oracle within the per-record colour mix, and G is
never 0.5×.

## Reconciliation with B1188's `G 47 vs 93`

B1188's residual (refactor/B1188/README.md §7) was measured on the **B1188 branch
(1:1 native atlas, pack `785690e6`)** in B1188's own gate render. It does not
reproduce on a fresh render of this pre-B1188 tree:

- Same box (`[60:360,770:1030]`), same frame (158), same tree, same pack
  (`1cdd31e7`): this render's core G = **144.9** (px>150), not B1188's ≈56.
- B1188's own per-frame census (`review/B1188/worker/gate2/measure/b1130_r0/fable_93/
  frames.tsv`, landing peak) shows the flame **blue-shifted** (R=210, G=120, **B=80**,
  hue 0.62) — whereas this tree's fresh render is orange (R≈200, G≈149, **B≈9**, hue
  ≈0.10) and the oracle is orange (B≈4–6). B1188's render carried ~8× the blue this
  tree produces, i.e. it was a different render state (the B1188-branch 1:1 atlas
  and/or the pack installed at B1188's run time), not this tree.

So the `G 47 vs 93` is a property of the **B1188 branch state**, not of the current
tree. On the current tree the per-channel colour is a clean copy (Ask 1/2), which is
exactly the state Will's rule ("the client's colour pipeline copied exactly, per
channel") asks for.

## Ask 3 — fix, test, re-gate

**There is no reproducible halving on this tree, so there is no source to fix and no
per-channel rule to pin here.** Forcing a change to make G lower would violate Will's
rule (a tuned factor) and would *move the warm records away from* the oracle (their G
already matches or exceeds it). The honest disposition:

1. **No code change, no new unit test, no shipped-pack change** on this branch
   (a shipped pack change is a ruling request, not a push — and there is nothing to
   change).
2. **Re-measure on the B1188 branch once MR !342 merges.** If the core-specific G
   loss (B1188's `G≈47` on the 1:1 native atlas) reproduces *there*, the cause is in
   B1188's 1:1 storage/encode path — i.e. the follow-up belongs in B1188, not here.
   The mechanism to check there: the 1:1 native pixels' ETC1 4×4 blocks on the bright
   saturated core (the only sub-region where a codec could read differently), and the
   driver's `_alpha` companion decode for the 1:1 entry.
3. The B1184/B1188 `landing_cov` re-gate is unchanged by this card (no render-affecting
   change landed), so the existing B1188 gate tables stand as the record.

## Acceptance (D) — docs/tests

- This README, the measurement scripts (`review/B1191/scripts/`), and the summary
  table (`review/B1191/scripts/B1191_perchannel_summary.txt`) are the deliverable.
- No C++ change → no new ctest surface; `model_viewer_fx_harness.cpp` is 1,480 lines
  (< 1,500). Hygiene (doc/size/citation) is run below; the tree is otherwise at the
  merged tip.
- No MR is opened: there is no fix to close #510 with on this tree. The card's premise
  (green halved on the current tree) is refuted by the measurement; the residual is
  B1188-branch-specific and is the follow-up's target once !342 merges.

## Reproduction

```
# packs (measurement-only; the RGBA8888 one is never shipped)
bash review/B1191/scripts/build_packs.sh        # on the ThinkPad
# renders (same plan/seed/camera; ETC1 pack baseline + RGBA8888 A/B for rec 0)
bash review/B1191/scripts/render_all.sh         # on the ThinkPad, CONC<=2
# per-channel tables (Ask 1)
python3 -I review/B1191/scripts/b1191_check_pair.py frames/r0 <oracle_r51> 'frame_*.png' 'f*.png' 30 60
python3 -I review/B1191/scripts/b1191_scan_box.py <dir> 'f*.png' 0:300:620:880 40 25   # core box
```
