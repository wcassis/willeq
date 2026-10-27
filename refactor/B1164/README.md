# B1164 (#483): spell-93 landing is half the oracle's pixel density — the population
"excess" is a misread, and the per-particle area is per-pixel density, not a mis-applied word

Record 80 (spell 93), harness b1118 + b1130, oracle per-frame from B1130 r47 / B1142.
This card acts on B1160's `popcount_93.txt` (MR !312, `origin/task/kb-b1160-density-drift`)
and resolves its two open quantities: the population (1550 vs "~100 from the EDD words")
and the per-particle pixel area (n_px 0.505× the oracle).

## Corrected problem statement (operator 23:35Z, MR !312 note 7623)

Two numbers in the card's Why were lifted from B1151 and are wrong for the current tip;
the corrected values are used throughout this README (the conclusion is unchanged:
right population / length / footprint, half the pixel density):

- n_px **1.993** = **0.505×** the 3.945 oracle and **0.654×** the 3.049 b1118 lane
  reference (not 0.54×/0.69×).
- There is **no "extent 9.32"** for spell 93. The LANDING bbox is **771×627 px =
  3.28×2.67 model** at the current tip (B1151: 791×612 px = 3.37×2.60).
- The **1550** particle count, the end **+6.633**, and the **~100-from-the-words**
  prediction stand.

My independent re-measurement of the same run reproduces the corrected numbers
(`FX_SPELLS=auto FX_KEEP=1 python3 -I /mnt/projects/fable/review/B1139/scripts/
harness_measure.py frames_cur93 measure_cur93`): LANDING coverage **1.992 model²**,
peak **+2.07 s**, end **+6.633 s**, n=**138703 px**, bbox **771×626 px = 3.28×2.66
model** (the 1-px / 0.001 differences are run-to-run timing, not a systematic gap).

## 1. Population — the "~100 from the EDD words" is a misread; the sim's 1550 is the correct word count

The card (and B1160) predicted ~100 landing particles from the words
("280: 30/s × 1.5 s = 45, 282: 28/s × 1.5 s = 42, 9: 10/s × 1.25 s = 12, 281: 1").
That arithmetic used the **tick rate** (D88, ticks/second) as the emission rate and
dropped the **per-tick count** (D84). The EDD emission is `perTick` particles **per
tick**, at `tps` ticks/second, so the sustained rate is `perTick × tps`:

| emitter | name | tex | burst(D80) | perTick(D84) | tps(D88) | rate (perTick×tps) | life | window | live (rate×window) |
|---|---|---|---|---|---|---|---|---|---|
| 280 | SFofSeukor S3-1 | spelcc.png | 50 | **50** | 30.0 | **1500/s** | 1.5 | 0.75 | **~1125** |
| 282 | SFofSeukor S3-3 | spelcf.png | 2 | 2 | 28.0 | 56/s | 1.5 | 1.5 | ~84 |
| 9 | IgniteTargetill Imp01 | spelcb.png | 3 | 3 | 10.0 | 30/s | 1.25 | 1.25 | ~38 |
| 281 | SFofSeukor S3-2 | spelcm.png | 1 | 0 | 1.0 | 0 | 1.0 | — | 0 (burst 1) |

Command: `python3 b1164_edd9.py` (reads `/mnt/projects/EverQuestP1999/spellsnew.edd`,
416-byte entries, C++ 1-based index). The four landing emitters sum to **~1247 live**;
the sim's **1550** impact peak (`popcount_93.txt`, frame 154) adds the SOURCE emitters
(the caster glow), so the sim holds the correct word count. **There is no 15× over-spawn.**

The decompile confirms per-tick is the sustained rate, not a one-shot: `CreateEmitter`
is at `0x1005a430` and its emission loop (`0x10060e8b`) multiplies the per-tick count
(`0x10060fe2` `imul ecx, dword ptr [ebp + 0x1c]` = × the D84 word). The sim implements
the same rule — it sets `ticksPerSecond` at `spell_emitter_presets.cpp:452` and
`tickParticles` at `spell_emitter_presets.cpp:453` — so the spawn rate is
`perTick × tps`. **No population bug; nothing to fix in the spawn path.** The
"~100" figure stands as the card's premise; the resolution is that it mis-read
`perTick × tps` as `tps`.

## 2. Per-particle pixel area — every word verified correct; the 0.505× is per-pixel lit density

For record 80's four landing emitters I computed the on-screen sprite from the words
and measured willeq's rendered quad (`WILLEQ_FX_AREA=1` gated print, §4):

| emitter | tex | w(DCC) | h(D17C) | mult(D16C) | scale(D19C) | depthBias(D0D0) | scaleSizes(D6C) | peakOpac(D0A4) | tex lit @thr45 |
|---|---|---|---|---|---|---|---|---|---|
| 280 | spelcc.png | 0.5 | 0.5 | 1.0 | 0.0 | 0.0 | 1 | 1.0 | **9.7 %** |
| 282 | spelcf.png | 6.0 | 6.0 | 1.0 | 0.0 | 0.0 | 1 | 0.875 | 17.4 % |
| 9 | spelcb.png | 1.25 | 1.25 | 1.0 | 0.0 | 0.0 | 1 | 1.0 | 42.2 % |
| 281 | spelcm.png | 16.0 | 16.0 | 1.0 | 0.0 | 0.0 | 0 | 0.75 | 16.9 % |

Command: `python3 b1164_edd11.py` (size words) + `python3 b1164_texlit.py` (the lit
fraction of each original PNG at the instrument's threshold 45, additive max-channel).

Every word that sets the per-particle area is applied correctly:

- **Size.** willeq's dominant quad (E280, w=h=0.5) measures **310 px² (17.6 px side)**
  at depth ≈29 (`WILLEQ_FX_AREA`, `meanPx2`/`meanSize`/`meanDepth`), which is exactly
  the decompile formula — half-width `w·0.5/depth·projScale`, `EQGraphicsDX9.dis`
  `0x1006335a` (the size read is `0x10061262` `fld [edi+0x180]` = D184 wMax). D16C=1.0
  (no multiplier), D19C=0.0 (no B1150 scale), D0D0=0.0 (no depth bias; the bias
  subtracts `fsub [edi+0xd0]` at `0x10063069`/`0x10063198`, a no-op at 0.0). The width
  is read at `sim_particles.cpp:872` and the height at `sim_particles.cpp:874`, both
  `× es.scale` — the `scaleSizes` word at `spell_emitter_presets.cpp:462`. The size is
  read correctly.
- **Blend.** willeq's additive path is `SRC_ALPHA, ONE` (`unified_particle_renderer.cpp:571`
  `srcAlphaAdditive`, comment at :567 "additive is SRC_ALPHA, ONE"), which is the
  oracle's `+0x068` blend (B1141 TSV, PROVEN: `SRCBLEND=SRCALPHA(5), DESTBLEND=ONE(2)`).
  The blend is read correctly.
- **Alpha.** per-emitter `meanAlpha` (0.810/1.000/…) tracks the D0A4 peak-opac word
  (1.0/0.75/0.875/1.0) through the fade. The alpha is read correctly.
- **Texture.** the oracle draws the EDD-named file (B1127); willeq resolves
  `spelcc/cm/cf/cb` to the same atlas cells (`spell_sprite_atlas.h:122` `kFiles` —
  `spelcc` at sprite idx 20). `spelcc.png` is a 64×64, **9.7 %-lit-at-threshold-45**
  texture — the sparsest of the four, and it is the dominant emitter (~1125 of the
  ~1247 landing live particles).

So **no single EDD word is mis-applied**. With the population correct (§1), the size
correct, the blend correct, the alpha correct and the texture the oracle's own, the
0.505× per-particle lit area is a **per-pixel density** effect: the dominant emitter's
texture is 9.7 % lit at one additive copy, and the net lit over the 3.28×2.67 footprint
is 0.505× the oracle's (138703/199206 raw; 1.992/3.945 model-normalised). This is the
card's case (b) (B1160 §3): the owner rules are read correctly and the oracle is still
denser.

## 3. Conclusion and the next quantity

- **Population: resolved.** The "15× excess" was a misread — the emission is
  `perTick × tps` (1500/s on the dominant emitter), not `tps` (30/s). The sim's 1550
  is the correct word count. No spawn-path change. (Acceptance A.)
- **Per-particle area: no mis-applied word.** Size, scale, depth-bias, blend, alpha
  and texture are all verified against the words and the decompile. The 0.505× is
  per-pixel lit density driven by the sparse dominant texture (`spelcc`, 9.7 % lit).
  It does not reduce to a single word, so there is no source fix to make without
  guessing (Will: match the oracle, never infer). (Acceptance B — named, no fix.)
- **Next quantity (owner: FX-sim sprite lane):** the oracle's per-particle lit area in
  *model units*, from the oracle 93 frames (B1130 r47 / B1118 lane 5 — not on this
  host), to disambiguate whether the 0.505× is (a) the oracle's dominant landing sprite
  being larger in model units than the DCC/D17C words carry (a size-formula input the
  words do not hold), or (b) the oracle's dominant landing sprite being denser per-texel
  than `spelcc` (a texture-content / routing question). Either needs the oracle frames;
  neither is a mis-applied EDD word.

## 4. Instrument (env-gated, local patch — not committed as a default)

- `WILLEQ_FX_COUNT=1` — per-emitter live count + config per frame
  (`sim_particles.cpp`, after `particleActiveCount_`).
- `WILLEQ_FX_AREA=1` — per-emitter quad area / meanSize / meanHeight / meanDepth /
  meanAlpha, plus `skipR` (the +0x198 / r<0 draw gate) and `skipProj` (projection /
  near-clip) counters and a per-frame `[fx-frame] f=… cnt=… drawn=… skipR=… skipProj=…`
  summary (`unified_particle_renderer.cpp`). 239/239 frames: `skipR=0 skipProj=0`,
  `drawn == cnt == sim total` — the renderer draws every alive particle, so the density
  gap is not a dropped draw.

## Re-gate (tour-7 + sweeps vs B1157 — acceptance C)

This card makes **no C++ change**, so before (B1160 current = post-B1157) and after
(this card) are identical — no row can regress. The tour-7 (b1118) and 40-record sweep
(both presets) values are unchanged from B1157's current column and B1160's current-tip
measurements; this card's fresh 93 render (1.993, `measure_cur93c`) confirms the current
tip still produces the established values. B1160's re-gate (MR !312) already established
no row regresses post-B1152; this card adds no delta.

tour-7 (b1118), land_cov current vs the B1151 baseline
(`refactor/B1157/b1118_table.tsv`, "n" column):

| spell | land_cov (current) | vs B1151 | verdict |
|---|---|---|---|
| 93  | 1.993 | −0.120 | over — this card's attribution |
| 38  | 1.993 | −0.120 | over — renders identically to 93 |
| 41  | 2.478 | +0.000 | PASS |
| 54  | 1.204 | +0.000 | PASS |
| 253 | 5.306 | +0.000 | PASS |
| 458 | 0.500 | +0.001 | short — unattributed (no B1152 baseline) |
| 656 | 1.204 | +0.000 | PASS |

No row regressed vs the B1157 current column (identical — no code change). The b1130
40-record sweep is likewise unchanged (`refactor/B1157/b1130_table.tsv`). Owners for the
open deltas: 93/38 = this card's per-pixel density (§6 FOLLOW-UP); 458 = unattributed
(no B1152 baseline); the `over` cast-anim rows (202/40/200) = the B1157 cast pose.

## 5. Acceptance

| # | item | status |
|---|---|---|
| A | population excess attributed to a rule with the decompile line, fixed, 93 count re-measured | **met** — attributed to `perTick × tps` (`10060fe2`); the "excess" was a misread (no spawn bug); 1550 re-measured |
| B | per-particle area word named + fixed + re-measured | **named, no fix** — all words (D16C/D19C/D0D0/D068/D0A4/texture) verified correct; the 0.505× is per-pixel density (§2); a source fix would be a guess |
| C | both sweeps + tour-7 before/after vs B1157 with owners | see the re-gate (no code change → no row regresses, B1160 §4 precedent) |
| D | 93 clip | attached (current behaviour, `make_clip.py`) |
| E | targeted ctest + hygiene + one whole-tree CI build green | the attribution commit (no C++ change) — hygiene + CI green |
| F | MR "Closes #483" | **opens a FOLLOW-UP, does not close #483** (B1160 precedent: a no-misread outcome defers the per-pixel density to the oracle-frame quantity in §3) |

## 6. FOLLOW-UP (named owner — no card, per the "never create cards" rule)

Owner: FX-sim sprite lane (B1149). Quantity: the oracle's per-particle lit area in model
units for record 80's dominant emitter (E280, `spelcc`), from the B1130 r47 / B1118 lane-5
oracle 93 frames, to decide whether the 0.505× is a size-formula input the words do not
carry (a) or a per-texel density / texture-routing difference (b). Until that is measured
there is no non-guessing source fix, so #483 stays open.
