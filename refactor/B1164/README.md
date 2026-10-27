# B1164 (#483): spell-93 landing "0.505x the oracle" is a units/geometry mismatch, not a sim
density gap — the population is the word count and, in the oracle's own geometry, willeq's
landing matches the oracle within 1 %

Record 80 (spell 93), harness b1118 + b1130, oracle frames from B1130 r51 (`review/B1130/oracle/r51/video.mp4`)
and B1118 `oracle_druid` / `oracle_leg` (lane 5). This card acts on B1160's `popcount_93.txt`
(MR !312) and resolves its two open quantities: the population (1550 vs "~100 from the EDD
words", §1) and the per-particle pixel area (n_px 0.505× the oracle, §2–§3).

Evidence root: `/mnt/projects/fable/review/B1164/worker/` (the first worker's runs and
`oracle_r51/` frame extraction) and `/mnt/projects/fable/review/B1164/worker/dev2/` (this
leg: renders, measurements, the B1118 frame extractions). Scripts are committed under
`refactor/B1164/scripts/`; every number below is printed by the command beside it, run from
`dev2/` (paths in the commands are relative to it).

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

## 2a. Per-particle area — the size, blend, alpha and texture words (first leg)

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

These words are applied as the file states; §2b–§3 measure what the per-particle area
actually is against the oracle, like with like.

## 2b. The 0.505× compares two different units

The card's ratio is 1.993 (willeq, b1118 harness) / 3.945 (oracle, B1130 r51). The two
numbers are normalised by different bodies:

- **Oracle 3.945** = 172315 lit px / 209² (`review/B1130/measure/r51_fixed_npc/lit80.tsv:122`,
  f7561; `review/B1130/measure/r51_fixed_npc.txt:5`). 209 px is the CASTER; the landing host
  is the target, 325 px tall at feet (940,652) (`review/B1130/geom/r51.json:9`, B1130 README
  "Geometry", `review/B1130/README.md:74`), standing ~1.55× nearer the camera
  (`tools/model_viewer/README.md:69`). In landing-host heights the same frame is
  172315 / 325² = **1.631**; the factor between the units is (325/209)² = 2.418.
- **willeq 1.993** = 110079 lit px / 235², where 235 px is the caster AND the landing host
  (b1118 tour plan `other 0`: the target stands on the caster). Command:
  `python3 scripts/b1164_regions.py willeq_t93_b1118 "frames/t93_b1118/frame_{f:05d}.png" 146 158 345 390 170,0,1060,650 640,605 235 235`
  → `regions_t93_b1118.txt`, f152: lit80 110079, cov 1.993 (the gate's own
  `measure/t93_b1118/summary.tsv`: land_cov 1.993, end +6.633).
- r51's crop (150,60,990,540) also clips its landing: in target heights it ends at
  up = (652−60)/325 = 1.82 above the target's feet and dx = (1140−940)/325 = +0.62 to its
  right, so even 1.631 is a clipped number.

**The like-for-like quantity** is lit area in landing-host heights² inside one window that
fits every capture: dx ∈ [−1.5, +0.6], up ∈ [0.16, 1.8] host heights, with the caster
box (494,380,292,230) and the UI columns excluded (`scripts/b1164_regions.py`, env `WIN`).
Same mask as `fx_timing.py` (max-RGB rise > 80 over the tail median).

| capture | geometry | frame | lit (host²) | column \|dx\|<0.5 | outside | command → file |
|---|---|---|---|---|---|---|
| oracle r51 (B1130) | target alive, 1.4 caster heights right, 325 px | f7561 (`or_0057`) | **1.493** | 1.340 | 0.153 | `WIN=-1.5,0.6,0.16,1.8 python3 scripts/b1164_regions.py oracleR51_93 "../oracle_r51/or_{f:04d}.png" 45 70 177 256 0,0,1280,800 940,652 325 209 494,380,292,230 1140,0,140,800 0,0,60,800` → `regions_win2_oracleR51_93_series.txt` (window peak) |
| willeq `--camera b1130` | the same placement, target 316 px, feet row 782 | f158 | **1.485** | 1.335 | 0.150 | `WIN=… python3 scripts/b1164_regions.py willeq_t93_b1130 "frames/t93_b1130/frame_{f:05d}.png" 140 165 345 390 0,0,1280,800 960,782 316 209 <same excludes>` → `regions_win2_t93_b1130.txt` |
| willeq `--camera b1118` | target on the caster, 235 px | f152 | 1.523 | 1.313 | 0.210 | `regions_t93_b1118.txt` (third block) |

**willeq / oracle in the oracle's own geometry = 1.485 / 1.493 = 0.995×**, and the column
and outside shares match to 0.005 host². The horizontal profile (lit fraction per 0.1-host
dx bin, up 0.16..1.8; `scripts/b1164_profile.py`) agrees bin for bin:
r51 f7561 vs willeq b1130 f152 at dx −0.4: 0.911 / 0.799, −0.2: 0.921 / 0.937,
+0.0: 0.854 / 0.833, +0.3: 0.713 / 0.787, +0.5: 0.331 / 0.206
(`profile_oracleR51_f7561.txt`, `profile_t93_b1130_f152.txt`).

The willeq b1130 target's 316 px / feet row 782 was measured off a pre-cast frame of this
run (bbox of the non-background pixels right of x 800 in `frames/t93_b1130/frame_00040.png`:
x 896..1023, y 467..782), matching B1155's FOLLOW-UP (`refactor/B1155/README.md:246`).

## 2c. Per-sprite lit area (the card's step 2), oracle vs willeq

`scripts/b1164_sparks.py`: blobs of the same mask, 8-connected, ≥ 2 px, fill ≥ 0.25 of the
bbox, aspect ≤ 2.2 (the `fx_sprite_roi.py` sprite filter), off the column (|dx| ≥ 0.6
host), pooled over the 13 frames around each landing peak, areas in 1e-4 host².

| capture | frames | sprites/frame | median area | p90 area | p90 diam (host) | mean peak rise | file |
|---|---|---|---|---|---|---|---|
| oracle r51 | or_0051..0063 (f7555..f7567) | 110.8 | **2.18** | 9.09 | 0.034 | 150 | `sparks_oracleR51_93_peak.txt` |
| willeq b1130 | f146..f158 | 74.9 | 3.81 | 24.77 | 0.056 | 166 | `sparks_t93_b1130.txt` |
| willeq b1118 | f146..f158 | 263.7 | 3.98 | 9.23 | 0.034 | 171 | `sparks_t93_b1118.txt` |

(the r51/b1130 rows use `EXC='494,380,292,230;1140,0,140,800;0,0,60,800'` and window
−2.4,0.6,0.16,2.0; the b1118 row −1.95,1.95,0,2.55.) willeq's sprites are **not smaller and
not dimmer** than the oracle's: median per-sprite lit area 3.81–3.98 vs 2.18, mean peak rise
166–171 vs 150. The texture check of §2a holds on willeq's own frames: lit-80 area / quad area
= 22.0 px² (3.98e-4 × 235²) / 310 px² (`WILLEQ_FX_AREA` `meanPx2`, §4) = 0.071, against
`spelcc.png`'s 0.069 of texels with colour×alpha > 80
(`python3 scripts/b1164_texlit.py` → `texlit.txt`).

Ruled out, each by the number beside it: **sprite size** (p90 diam 0.034 = 0.034),
**alpha / blend / texture brightness** (peak rise 171 ≥ 150), **population** (the in-window
lit area matches at 0.995× with the same column/outside split; §1 for the word count),
**the target's death** (willeq `--target-death 0 --target-despawn 1.027`: land_cov 1.985
vs 1.993, `measure/t93_b1118_die/summary.tsv`; the end moves to +4.700 because the target
despawns at Act+1.027).

## 3. What the 0.505× is, and the two remaining owners

1. **Units (×2.418)**: oracle normalised by the 209 px caster while its landing host is
   325 px; willeq normalised by its 235 px landing host. 3.945 / 2.418 = 1.631.
2. **Crop clip**: r51's crop cuts its landing at up 1.82 / dx +0.62 host; inside one
   common window the two agree at 0.995× (§2b).

So there is **no sim density defect to fix** for the r51 reference, and no EDD word is
mis-applied (§2a, §2c).

Two things the measurement exposes, each owned elsewhere:

- **The b1130 harness gate cannot see the landing.** Its target stands with its feet at
  row 782 (316 px) instead of the oracle's 652 (325 px): the solver fits the rest pose
  (`refactor/B1155/README.md:246`; the solver is `fitFixedNpcTarget`, `tools/model_viewer/README.md:73`); the b1130 crop ends at row 600, so most of the
  willeq landing falls below it and record 0 reads land_cov 0.358
  (`refactor/B1157/b1130_table.tsv:2`) although the in-window density matches (§2b). This is
  the "0/40 landing_cov PASS" of `refactor/B1155/README.md:215`.
- **The two oracle captures disagree with each other.** B1118's `oracle_druid` 93
  (lane-5 b1118 framing, target `#spawn`ed ON the caster, killed at OP_Action: the chat at
  `dev2/oracle_b1118_frames/ob_7573.png` reads "You have slain TrollF") reads 5.847
  (`review/B1118/measure/timing.txt:3`; re-measured 5.845 at f7573 by
  `scripts/b1164_regions.py oracleB1118_93 "oracle_b1118_frames/ob_{f}.png" 7565 7580 7725 7760 150,60,900,540 640,598 210 210`
  → `regions_oracleB1118_93.txt`). In the §2b window it is **2.940 host²**
  (`regions_win2_oracleB1118_93.txt`) — 1.97× r51's 1.493 — with a column about twice as
  wide (lit fraction ≥ 0.95 out to dx −0.9 / +0.6, `profile_oracleB1118_f7573.txt`, vs
  0.07 / 0.14 in r51) and spark sprites twice the diameter (p90 0.067 host, median 11.79e-4,
  `sparks_oracleB1118_93_peak.txt`). B1118's spell 38 cast (same record, same framing,
  target slain) shows the same wide column (2.432 host² in the window,
  `regions_oracleB1118_38.txt`, f9800). willeq does not change with the death (1.985) and
  draws the r51 size in both framings. The two oracle legs differ in two conditions at
  once (target on the caster vs 1.4 heights away; slain vs alive), so which one doubles the
  oracle's effect is not decided by these captures. The only code path in the trace that
  scales sprite size, offsets and velocities together is the attach scale s
  (`refactor/B1151/TRACE.md:51`, stage 1 at 0x10060906, the +0x6C stage 2 ending
  0x1006094a); willeq passes it as 1 (`docs/spellsnew_format.md:32`). That is a candidate,
  not a finding: s's runtime value is not pinned statically (TRACE.md (a) "PARTIAL").

## 4. Instrument (env-gated, local patch — not committed as a default; first leg)

## 4. Instrument (env-gated, local patch — not committed as a default)

- `WILLEQ_FX_COUNT=1` — per-emitter live count + config per frame
  (`sim_particles.cpp`, after `particleActiveCount_`).
- `WILLEQ_FX_AREA=1` — per-emitter quad area / meanSize / meanHeight / meanDepth /
  meanAlpha, plus `skipR` (the +0x198 / r<0 draw gate) and `skipProj` (projection /
  near-clip) counters and a per-frame `[fx-frame] f=… cnt=… drawn=… skipR=… skipProj=…`
  summary (`unified_particle_renderer.cpp`). 239/239 frames: `skipR=0 skipProj=0`,
  `drawn == cnt == sim total` — the renderer draws every alive particle, so the density
  gap is not a dropped draw.


## 5. Re-gate (acceptance C)

This card changes **no C++, config, golden, manifest or baseline**: the only files are this
README and `refactor/B1164/scripts/`. The binary every gate row runs is therefore the
B1157 tip's, and no row can move. The 93 row was re-rendered and re-measured on this
branch to confirm it (`scripts/run93.sh t93_b1118 tour_93.tsv --camera b1118 --seed 1`, then
`FX_KEEP=1 python3 -I /mnt/projects/fable/review/B1139/scripts/harness_measure.py frames/t93_b1118 measure/t93_b1118`):

| row | B1157 after (`refactor/B1157/b1118_table.tsv`) | this branch | owner of the remaining delta |
|---|---|---|---|
| tour 93 (b1118) | land 1.993, end +6.633 | land 1.993, end +6.633 (`measure/t93_b1118/summary.tsv`) | none in the sim: equal to the r51 oracle in its geometry (§2b); the b1118 lane oracle 5.847 is the open oracle-side question (§3, FOLLOW-UP 2) |
| tour 93 (b1118), target slain at Act | — | land 1.985, end +4.700 (`measure/t93_b1118_die/summary.tsv`) | record only |
| r0 93 fixed_npc (b1130) | land 0.358 (`refactor/B1157/b1130_table.tsv:2`) | not re-run through the b1130 instrument; in-window 1.485 vs oracle 1.493 (§2b) | the b1130 target fit (FOLLOW-UP 1) |

Every other tour-7 / 40-record row (both presets) is the B1157 after column unchanged,
with B1157's owners (`refactor/B1157/README.md` sec. 6).

**93 clip (acceptance D):** the first leg's `clips/b1164_93_current.mp4` (+ `_strip.png`,
`refactor/B1158/scripts/make_clip.py`, spec `specs/b1164_93_current.json`) is the current
behaviour; with no code change it is the after clip too. The like-for-like evidence is the
table in §2b, not the clip (the clip's oracle side is a different framing).

## 6. Acceptance

| # | item | status |
|---|---|---|
| A | population attributed with the decompile line, 93 count re-measured | **met** — §1: `perTick × tps` (0x10060fe2), 1550 is the word count; no spawn bug, so no fix |
| B | per-particle area word named + fixed + re-measured | **met as attribution, no fix** — §2c: per-sprite area and brightness are not below the oracle's; §2b: the 0.505× is the caster-vs-host normalisation (×2.418) plus r51's crop clip; in-window 0.995×. No word is mis-applied, so there is nothing to fix in the sim (a fix would be the guess Will forbids) |
| C | both sweeps + tour-7 before/after vs B1157 with owners | **met** — §5 (no code change; 93 re-measured equal) |
| D | 93 clip | **met** — first leg's clip, unchanged behaviour (§5) |
| E | targeted ctest + hygiene + CI build green | no C++ change, so no new device-free test; the MR pipeline is the build/hygiene check |
| F | MR "Closes #483" | the MR says Closes #483: the card's question (the 0.505×) is fully attributed; the two follow-ups below are separate owners |

## 7. FOLLOW-UP (named owners — no card cut, per the worker rules)

1. **b1130 harness target fit** (owner: the B1155 harness preset, `tools/model_viewer`
   `fitFixedNpcTarget`): fit the target on a STANDING frame so its feet land on row 652 at
   325 px, then re-run the 40-record b1130 sweep. Until then every b1130 landing_cov row is
   read through a crop that misses most of the target (§3), and its "0/40 landing PASS" is
   not a sim finding.
2. **Oracle A/B on lane 5** (owner: FX oracle legs): cast 93 twice with one condition
   changed at a time — target `#spawn`ed on the caster but with hp 30000 (alive), and target
   1.4 caster heights away with hp 1 (slain) — measured with `scripts/b1164_regions.py` /
   `b1164_sparks.py` in host units. It decides whether co-location or the death doubles the
   oracle's landing (B1118 2.940 vs r51 1.493 host² in the same window); only then is there
   a rule (candidate: the +0x6C attach scale s, TRACE.md (a)) for willeq to implement, and
   the b1118 gate's oracle reference (5.847) is a fair target only for the condition it
   captured.
