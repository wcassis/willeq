# B1164 (#483): spell-93 landing density — population = the word count; the lane-5 A/B
finds no capture condition behind B1118's larger landing, B1130's r51 target geometry is wrong,
and willeq is ~0.5× at the gate = a relatively small target (0.60× against r51) × a thinner
landing per target height (0.82× against r51); acceptance B NOT met

Record 80 (spell 93), harness b1118 + b1130, oracle frames from B1130 r51 (`review/B1130/oracle/r51/video.mp4`)
and B1118 `oracle_druid` / `oracle_leg` (lane 5). This card acts on B1160's `popcount_93.txt`
(MR !312) and resolves its two open quantities: the population (1550 vs "~100 from the EDD
words", §1) and the per-particle pixel area (n_px 0.505× the oracle, §2–§3).

Evidence root: `/mnt/projects/fable/review/B1164/worker/` (the first worker's runs and
`oracle_r51/` frame extraction) and `/mnt/projects/fable/review/B1164/worker/dev2/` (this
leg: renders, measurements, the B1118 frame extractions). Scripts are committed under
`refactor/B1164/scripts/`; every number below is printed by the command beside it, run from
`dev2/` (paths in the commands are relative to it).

## Continuation "pin s" (dev5) — read §10 first

§10 pins the attach scale s on lane 5 (a read-only gdb probe of the official client) and from the
client's code: record 80's slots are TRACK-attached (not point-attached as §4's table assumed), and
s is the actor's scale, 0.2 × the spawn's size = **1.2** on the size-6 human target of 93 and 1.0 on
the caster. Fable now passes it (`include/client/spell/spell_fx_attach_scale.h`). An unoccluded
reference shows willeq's TARGET model is the one drawn small (client 1.2, willeq 1.0). With the
target at the client's scale the landing is still ~0.72–0.77× per target height²: acceptance B is
NOT met.

## README fix (dev4, review 7754) — read with the dev3 list below

Evidence: `/mnt/projects/fable/review/B1164/worker/dev4/` (no new captures or renders; every
number is re-measured from the existing stills, frames and videos).

- **r51's target is 286 px (rows 329..614), not 265.** b1164_hostpx.py's difference threshold
  40 stops at row 593 because fewer than 3 px per row exceed 40 below it (the boot pixels' per-row
  maxima fall 43/39/36/32/30/28/28/27 over rows 594..601, 21–25 over 602..614; per MR note 7787); at
  threshold 20 or 15 the rows run to 603 (the selection ring starts at 604), and the boot's own
  colour runs on to row 614 (§5, `review/B1164/worker/dev4/hostpx_sweep.txt`, new script
  `refactor/B1164/scripts/b1164_hostpx_sweep.py`). Every number built on 265 is recomputed:
  r51 = **1.806** target heights² in the window (was 1.978), willeq / r51 **0.82×** (was 0.75×),
  r51's target / caster **1.37** (was 1.27).
- **The co-located captures (B1118, (a), (c), (d)) are clipped at row 600** by the Main Chat
  window's top border (row 601, x 427..820): their target heights are lower bounds, their
  per-target-height² values upper bounds, and their target/caster ratios uncertain (§5).
- §2c's r51 row (325 px) is withdrawn and recomputed at 286 px; §3's "652 (325 px)" corrected.

## Continuation leg (dev3, review 7668) — read first

Evidence: `/mnt/projects/fable/review/B1164/worker/dev3/` (lane-5 captures `oracle/`, plans
`refactor/B1164/plans/`, measurements beside them). Every number below in §5 is printed by the
command named with it, run from `dev3/` unless the path says otherwise.

1. §2a: +0x19C and +0x6C are INT flags = 1 on the landing emitters (the first leg printed
   +0x19C as a float).
2. §4: the decompile — record 80's landing slots are attached to the target's
   SPELLPOINT_DEFAULT, so the client multiplies every landing dimension by s = the point's
   world-matrix scale; willeq passes s = 1.
3. §5: the lane-5 A/B. Neither condition B1118 differs in (target on the caster; target slain),
   nor both together, reproduces B1118's landing: r51 and the three controlled captures read
   1.81–1.98 target heights² in the window (r51 1.806 and (b) 1.984 with unclipped targets;
   (a) ≤ 1.963 and (c) ≤ 1.980, upper bounds) and 3.95–4.24 caster² on the gate's instrument, and
   B1118's own plan re-run today reads ≤ 2.115 / 4.342 (B1118: ≤ 2.377 / 5.847, its target drawn
   at least ~6 % larger). B1118's 93 is not reproducible; it is not a condition of the cast.
4. §5: **B1130's r51 target geometry (325 px, feet (940,652)) is not reproduced.** Measured by
   difference against a target-free still at r51's own camera, r51's target is 286 px
   (rows 329..614; dev4 corrected the dev3 value 265, see the list above). §2b's "0.969× in r51's
   geometry" used the 325 px and is withdrawn; with the measured height, willeq is 0.82× r51 per
   target height² (0.79× at the 275-px lower bound).
5. willeq's gate shortfall (1.993 vs 3.95–4.24 caster²) factors into (i) the target's size
   relative to the caster, 1.057 caster heights in willeq vs 1.37 in r51 ((1.057/1.37)² = 0.60;
   the co-located captures' 1.30–1.35 are ratios of two clipped heights, uncertain), and (ii) 0.82×
   lit area per target height² against r51. Neither is a word willeq mis-applies on
   the EDD path that this card can name from evidence: no code change (§7, FOLLOW-UP §9).

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

Command: `python3 refactor/B1164/scripts/b1164_edd.py pop` (reads
`/mnt/projects/EverQuestP1999/spellsnew.edd`: an 8-byte header, then 416-byte entries counted
from 0; the index is the spellsnew.eff slot word). The four landing emitters sum to **~1247 live**;
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

| emitter | tex | w(DCC) | h(D17C) | mult(D16C) | D19C (int flag) | depthBias(D0D0) | D6C (int flag) | peakOpac(D0A4) | tex lit @thr45 |
|---|---|---|---|---|---|---|---|---|---|
| 280 | spelcc.png | 0.5 | 0.5 | 1.0 | **1** | 0.0 | **1** | 1.0 | **9.7 %** |
| 282 | spelcf.png | 6.0 | 6.0 | 1.0 | **1** | 0.0 | **1** | 0.875 | 17.4 % |
| 9 | spelcb.png | 1.25 | 1.25 | 1.0 | **1** | 0.0 | **1** | 1.0 | 42.2 % |
| 281 | spelcm.png | 16.0 | 16.0 | 1.0 | **1** | 0.0 | 0 | 0.75 | 16.9 % |

Command: `python3 refactor/B1164/scripts/b1164_edd.py size` (size words; +0x6C and +0x19C are
read as INT flags, `docs/spellsnew_format.md:94` and `docs/spellsnew_format.md:132` — the first
leg printed +0x19C as a float, where the int 1 reads 1.4e-45 and printed as 0.0) +
`python3 b1164_texlit.py` (the lit fraction of each original PNG at the instrument's threshold
45, additive max-channel). So **all four landing emitters have +0x19C = 1** (shape and offsets
scale with the attach scale s) and **280/282/9 have +0x6C = 1** (sizes and velocities scale
with s); what s is at runtime is §4.

Every word that sets the per-particle area is applied correctly:

- **Size.** willeq's dominant quad (E280, w=h=0.5) measures **310 px² (17.6 px side)**
  at depth ≈29 (`WILLEQ_FX_AREA`, `meanPx2`/`meanSize`/`meanDepth`), which is exactly
  the decompile formula — half-width `w·0.5/depth·projScale`, `EQGraphicsDX9.dis`
  `0x1006335a` (the size read is `0x10061262` `fld [edi+0x180]` = D184 wMax). D16C=1.0
  (no multiplier), D0D0=0.0 (no depth bias; the bias
  subtracts `fsub [edi+0xd0]` at `0x10063069`/`0x10063198`, a no-op at 0.0). The width
  is read at `sim_particles.cpp:872` and the height at `sim_particles.cpp:874`, both
  `× es.scale` — the `scaleSizes` word at `spell_emitter_presets.cpp:462`. The size is
  read correctly **for s = 1**: D6C = D19C = 1 make the width, height, velocities, shape
  and offsets all × s, and willeq passes s = 1 (§4).
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

## 2b. The 0.505× compares two different units (first leg; the host geometry is WITHDRAWN, §5)

> **Withdrawn (dev3).** This section normalises r51 by the target geometry of
> `review/B1130/geom/r51.json` (325 px, feet (940,652)). §5 measures r51's target at 286 px
> (rows 329..614, by difference against a target-free still at the same camera), so the
> "0.969×/0.995× in r51's geometry" below does not hold; the corrected comparison is §5.

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
| oracle r51 (B1130) | target alive, 1.4 caster heights right, 325 px | f7561 (`or_0057`) | **1.493** | 1.340 | 0.153 | `WIN=-1.5,0.6,0.16,1.8 python3 scripts/b1164_regions.py oracleR51_93 "../oracle_r51/or_{f:04d}.png" 45 70 177 256 0,0,1280,800 940,652 325 209 494,380,292,230 1140,0,140,800 0,0,60,800` → `regions_win2_oracleR51_93_series.txt` (f7561 = the lit80 (crop) peak frame; the in-window peak is **f7584 (Act+3.03): 1.532, column 1.408**, `dev3/regions_win2_oracleR51_93_series_to7600.txt`, the same command over or_45..or_96) |
| willeq `--camera b1130` | the same placement, target 316 px, feet row 782 | f158 | **1.485** | 1.335 | 0.150 | `WIN=… python3 scripts/b1164_regions.py willeq_t93_b1130 "frames/t93_b1130/frame_{f:05d}.png" 140 165 345 390 0,0,1280,800 960,782 316 209 <same excludes>` → `regions_win2_t93_b1130.txt` |
| willeq `--camera b1118` | target on the caster, 235 px | f152 | 1.523 | 1.313 | 0.210 | `regions_t93_b1118.txt` (third block) |

**willeq / oracle in the oracle's own geometry, peak to peak = 1.485 (f158) / 1.532 (f7584) =
0.969×** (at the oracle's lit80 peak frame f7561 it is 1.485 / 1.493 = 0.995×; the column
and outside shares match to 0.005 host² there). The horizontal profile (lit fraction per 0.1-host
dx bin, up 0.16..1.8; `scripts/b1164_profile.py`) at the table's frames,
r51 f7561 vs willeq b1130 f158, dx −0.4: 0.911 / 0.754, −0.2: 0.921 / 0.925,
+0.0: 0.854 / 0.767, +0.3: 0.713 / 0.864, +0.5: 0.331 / 0.229
(`profile_oracleR51_f7561.txt`, `dev3/profile_t93_b1130_f158.txt`: `python3 scripts/b1164_profile.py
willeq_t93_b1130 "frames/t93_b1130/frame_{f:05d}.png" 158 345 390 0,0,1280,800 960,782 316 0.16 1.8
'494,380,292,230;1140,0,140,800;0,0,60,800'`).

The willeq b1130 target's 316 px / feet row 782 was measured off a pre-cast frame of this
run (bbox of the non-background pixels right of x 800 in `frames/t93_b1130/frame_00040.png`:
x 896..1023, y 467..782), matching B1155's FOLLOW-UP (`refactor/B1155/README.md:246`).

## 2c. Per-sprite lit area (the card's step 2), oracle vs willeq (r51 row WITHDRAWN, §5)

> **Withdrawn (dev4).** The r51 row below normalises by B1130's 325 px / feet (940,652). At
> r51's measured 286 px / feet (945,614), with the same window (−2.4,0.6,0.16,2.0), band, frames
> and excludes, r51 reads 121.6 sprites/frame, median **2.93**e-4, p90 diameter **0.039** target
> heights, mean peak rise 153 (`review/B1164/worker/dev4/sparks_oracleR51_93_h286.txt`:
> `EXC='494,380,292,230;1140,0,140,800;0,0,60,800' python3 refactor/B1164/scripts/b1164_sparks.py
> oracleR51_93_h286 "../oracle_r51/or_{f:04d}.png" 51 63 177 256 0,0,1280,800 945,614 286
> -2.4,0.6,0.16,2.0`, run from `dev2/`). willeq's sprites at its own 241-px target are p90
> **0.033** (§5), so willeq's sprites ARE smaller in diameter. This section's "not smaller",
> "p90 diam 0.034 = 0.034" and "matches r51 at 0.969×" are withdrawn; §5 is the comparison.

`scripts/b1164_sparks.py`: blobs of the same mask, 8-connected, ≥ 2 px, fill ≥ 0.25 of the
bbox, aspect ≤ 2.2 (the `fx_sprite_roi.py` sprite filter), off the column (|dx| ≥ 0.6
host), pooled over the 13 frames around each landing peak, areas in 1e-4 host².

| capture | frames | sprites/frame | median area | p90 area | p90 diam (host) | mean peak rise | file |
|---|---|---|---|---|---|---|---|
| oracle r51 | or_0051..0063 (f7555..f7567) | 110.8 | **2.18** | 9.09 | 0.034 | 150 | `sparks_oracleR51_93_peak.txt` |
| willeq b1130 | f146..f158 | 74.9 | 3.81 | 24.77 | 0.056 | 166 | `sparks_t93_b1130.txt` |
| willeq b1118 | f146..f158 | 263.7 | 3.98 | 9.23 | 0.034 | 171 | `sparks_t93_b1118.txt` |

(the r51/b1130 rows use `EXC='494,380,292,230;1140,0,140,800;0,0,60,800'` and window
−2.4,0.6,0.16,2.0; the b1118 row −1.95,1.95,0,2.55.) In these (withdrawn) units willeq's sprites
read **not smaller and not dimmer** than the oracle's: median per-sprite lit area 3.81–3.98 vs 2.18, mean peak rise
166–171 vs 150. The texture check of §2a holds on willeq's own frames: lit-80 area / quad area
= 22.0 px² (3.98e-4 × 235²) / 310 px² (`WILLEQ_FX_AREA` `meanPx2`, §4) = 0.071, against
`spelcc.png`'s 0.069 of texels with colour×alpha > 80
(`python3 scripts/b1164_texlit.py` → `texlit.txt`).

Ruled out by the first leg, each by the number beside it (**sprite size** and **population** are
WITHDRAWN, see the box above): **sprite size** (p90 diam 0.034 = 0.034),
**alpha / blend / texture brightness** (peak rise 171 ≥ 150), **population** (the in-window
lit area matches r51 at 0.969× peak to peak (0.995× at f7561, with the same column/outside split); §1 for the word count),
**the target's death** (willeq `--target-death 0 --target-despawn 1.027`: land_cov 1.985
vs 1.993, `measure/t93_b1118_die/summary.tsv`; the end moves to +4.700 because the target
despawns at Act+1.027).

## 3. What the 0.505× is, and the two remaining owners (first leg; superseded by §5)

1. **Units (×2.418)**: oracle normalised by the 209 px caster while its landing host is
   325 px; willeq normalised by its 235 px landing host. 3.945 / 2.418 = 1.631.
2. **Crop clip**: r51's crop cuts its landing at up 1.82 / dx +0.62 host; inside one
   common window the two agree at 0.969× peak to peak (§2b; withdrawn, §5: 0.82× at r51's
   measured 286 px).

(Withdrawn, §5: at r51's measured 286 px willeq is 0.82× r51 per target height², so there IS a
density gap against r51 too.) The first leg concluded that against the **r51** reference there
is no density gap and that this did not hold against the gate's own B1118 capture (below).

Two things the measurement exposes, each owned elsewhere:

- **The b1130 harness gate cannot see the landing.** Its target stands with its feet at
  row 782 (316 px) instead of the 652 (325 px) of B1130's geometry (r51's measured target is
  286 px with its boots at row 614, §5): the solver fits the rest pose
  (`refactor/B1155/README.md:246`; the solver is `fitFixedNpcTarget`, `tools/model_viewer/README.md:73`); the b1130 crop ends at row 600, so most of the
  willeq landing falls below it and record 0 reads land_cov 0.358
  (`refactor/B1157/b1130_table.tsv:2`) (§2b's in-window match is withdrawn, §5). This is
  the "0/40 landing_cov PASS" of `refactor/B1155/README.md:215`.
- **The two oracle captures disagree with each other.** (WITHDRAWN, §5, per MR note 7787: the
  first leg compared B1118 against r51 at B1130's 325 px / feet (940,652) and read 2.940 host²
  as 1.97× r51's 1.493, B1118's spark sprites as twice r51's diameter, and willeq as drawing
  "the r51 size in both framings". At r51's measured 286 px (§5) the comparison is, in §5's
  units, B1118 vs r51 ≤ 2.377 / 1.806 = **≤ 1.32×** in the window and ≤ 0.049 / 0.039 =
  **≤ 1.26×** in p90 diameter (upper bounds: B1118 is chat-window clipped at row 600), and
  willeq's p90 0.033 is BELOW r51's 0.039, so willeq does not draw "the r51 size". Which
  capture condition (target on the caster vs 1.4 heights away; slain vs alive) produces
  B1118's larger column is not decided by these captures.) What stands from the first leg:
  B1118's `oracle_druid` 93 (lane-5 b1118 framing, target `#spawn`ed ON the caster, killed at
  OP_Action: the chat at `dev2/oracle_b1118_frames/ob_7573.png` reads "You have slain TrollF")
  reads 5.847 (`review/B1118/measure/timing.txt:3`; re-measured 5.845 at f7573 by
  `scripts/b1164_regions.py oracleB1118_93 "oracle_b1118_frames/ob_{f}.png" 7565 7580 7725 7760 150,60,900,540 640,598 210 210`
  → `regions_oracleB1118_93.txt`), and its spell 38 cast (same record, same framing, target
  slain) shows the same wide column (`regions_oracleB1118_38.txt`, f9800); willeq does not
  change with the death (1.985). The only code path in the trace that
  scales sprite size, offsets and velocities together is the attach scale s
  (`refactor/B1151/TRACE.md:51`, stage 1 at 0x10060906, the +0x6C stage 2 ending
  0x1006094a); willeq passes it as 1 (`docs/spellsnew_format.md:32`). That is a candidate,
  not a finding: s's runtime value is not pinned statically (TRACE.md (a) "PARTIAL").

## 4. The attach scale s at runtime (decompile trace)

Record 80's Target stage (spellsnew.eff record 80, stage 2) has four slots, all anchor 3
(target) and attach point 1 (SPELLPOINT_DEFAULT): `(280,0,3,1) (281,0,3,1) (282,39,3,1)
(9,24,3,1)` (edd index, min level, anchor, attach point; layout `docs/spellsnew_format.md:72`
and `docs/spellsnew_format.md:75`; printed by `python3 refactor/B1164/scripts/b1164_eff.py 80`).

**What eqgame passes.** For an attached slot, eqgame `0x495f02` calls the DLL's emitter
create (`0x1005e0a0` → `0x1005a430`) with the actor, the attach point and a size scale of
1.0 (12 is the only point that passes the actor scale; `review/B1141/eff_fields_from_code.tsv:10`).
CreateEmitter hands them to the CEmitter constructor (`0x1005a696` `call 0x1005a0e0`), which
stores the actor at instance +0x68, the point at +0x6C, a track at +0x70 and its last
argument at +0x88 (read only for an emitter with nothing attached, row 1 below; which eqgame
push lands there was not traced here and does not matter for record 80, whose slots are
point-attached) (`0x1005a17a` `mov [esi+0x88], edx`; `EQGraphicsDX9.dll.c:63311`..`EQGraphicsDX9.dll.c:63313`
`param_1[0x1a..0x1c]`, `EQGraphicsDX9.dll.c:63272` `param_1[0x22] = param_14`).

**What Render does with +0x6C / +0x19C** (`0x100605b0` CParticleSystem::Render, edi = the
EDD entry, ebp = the instance). Two scale slots start at 1.0: s6C `[esp+0x30]` and s19C
`[esp+0x48]` (`0x100608bf`, `0x100608c7`). Then:

| instance attach | s6C (when +0x6C = 1) | s19C (when +0x19C = 1) | address |
|---|---|---|---|
| nothing (+0x68 = +0x70 = 0) | the size scale +0x88 | +0x88 | `0x100608ea`, `0x100608fc` |
| actor only (+0x68) | the attached object's vtbl+0x84 × actor+0x10 (B1151 TRACE (a), not pinned) | the same | `0x10060944`, `0x10060979` |
| **attach point (+0x6C) — record 80's case** | **‖row 0 of the point's world matrix‖** | **the same** | `0x100609e9`, `0x100609fd` → `0x10039480` |
| track (+0x70) | ‖matrix row‖ × `0x1005c4f0` | the same | `0x10060ae8`, `0x10060b32` |

The point path runs after the actor path and overwrites it (`0x10060991` tests +0x6C, the
point's matrix is refreshed by `0x100609bf` `call 0x100398d0` and its position read from
point+0xD0 at `0x100609c7`). `0x10039480` is `sqrt(m[0xa0]² + m[0xa4]² + m[0xa8]²)`
(`EQGraphicsDX9.dll.c` FUN_10039480): the length of the first row of the point's 4×4 world
matrix, i.e. the uniform scale the client renders that model point at — the actor's scale
as it reaches the DEFAULT bone. There is **no distance, no co-location and no death term**
in it: the only inputs are the target model's matrices.

**What s multiplies** (each a `fmul` by the slot):

- s6C: the sprite size — `0x1006325e` (`s6C × +0x16C`), the draw half-width at `0x10063364`..`0x1006336c`
  (× the particle's size `[esi+0x6c]`); the per-step velocity integration
  (`0x10062705`, h += v1·s·dt, and the rows listed at `src/client/graphics/sim_particles.cpp:899`).
- s19C: the spawn shape and offsets — `0x100612c8`..`0x100612cf` (`s19C × +0xB8`), the
  respawn copy at `0x100629bb`, and `[esp+0x2c] = 1/s19C` at `0x10060b8d`.

So for record 80 every landing dimension (sprite size, velocities, shape radius/height,
offsets) is × s, where s is the target's rendered scale at its DEFAULT point.

**What willeq does.** The EDD path reads both flags and applies them —
`spell_emitter_presets.cpp:395` (shape: `scaleShapes ? attachScale : 1`),
`spell_emitter_presets.cpp:420` (motion), `spell_emitter_presets.cpp:462` (sprite: `multiplier ×
(scaleSizes ? attachScale : 1)`), consumed at `sim_particles.cpp:842`/`sim_particles.cpp:844` (size)
and `sim_particles.cpp:899` (motion) — but every caller passes the constant
`kSpellFxAttachScale = 1.0` (`include/client/spell/spells_new_edd_reader.h:262`; the Target
stage at `spell_visual_fx.cpp:1013`). So willeq draws record 80 at s = 1 for every target.
Whether that is the gap depends on the target's s in the oracle captures, which §5 measures.

## 5. Lane-5 A/B (dev3): which condition makes B1118's landing larger?

B1118's `oracle_druid` 93 differs from B1130 r51 in two conditions: the target `#spawn`ed ON
the caster (r51: `#goto`-ed 1.4 caster heights away) and the target slain at the landing (r51:
hp 30000, alive). Each leg below is r51's plan (`review/B1130/plans/r51.tsv`) with the named
change(s), run by `scripts/eqemu/oracle/oracle_class_leg.sh 5 6 <plan> <out>` from hermes-ext
under the lane-5 lease (owner `kb-b1164-pixel-area_*`), class 6 for the leg; every leg ended
`cleanup exit=0 ... lock=state=FREE` with char 67's class, gems, skills, disciplines and timers
restored (`BACK` lines in each `driver.log`). Plans: `refactor/B1164/plans/`.

| leg (`dev3/oracle/`) | plan | change vs r51 | target fate (pcap, `review/B1118/scripts/pcap_ops.py`) |
|---|---|---|---|
| r51 (B1130) | `review/B1130/plans/r51.tsv` | — | alive (hp 30000, `#stun`) |
| (a) `ab_a_oncaster_alive_25fps` | `ab_a_oncaster_alive.tsv` | no `#goto`: target on the caster | alive: no OP_Death for 261 |
| (b) `ab_b_away_slain_25fps` | `ab_b_away_slain.tsv` | bare `#spawn TrollF` (B1118's: Health 1) | OP_Death 260 at OP_Action (1793106347.477), OP_DeleteSpawn 260 +1.04 s |
| (c) `ab_c_oncaster_slain_25fps` | `ab_c_oncaster_slain.tsv` | both | OP_Death 260 at OP_Action (1793107679.281), OP_DeleteSpawn +1.04 s |
| (d) `ab_d_b1118_rerun_25fps` | `ab_d_b1118_rerun.tsv` | B1118's own plan, verbatim | OP_Death 261 at OP_Action (1793108543.808), OP_DeleteSpawn +1.04 s |
| B1118 `oracle_druid` | `review/B1118/plans/oracle_druid.tsv` | (both) | OP_Death 260 at OP_Action (1792609933.847), OP_DeleteSpawn +1.04 s |

All targets are the same model: OP_NewSpawn `TrollF000 race=1 gender=0 size=6.000` in B1118,
r51 and every new leg (`dev3/spawns.txt`: `python3 refactor/B1164/scripts/b1164_spawns.py
<capture.pcap> TrollF`, the Spawn_Struct offsets of `include/common/packets/spawn.h:64`).

**Target heights** (`dev3/hostpx.txt`, `python3 refactor/B1164/scripts/b1164_hostpx.py
<with> <without> x0 x1 305 602`: rows of the band where ≥ 3 px differ by > 40 between a
still with the target and a target-free still at the same camera; every row's count is
printed). dev4 swept the threshold (40 / 20 / 15) and added a colour test
(`review/B1164/worker/dev4/hostpx_sweep.txt`: `python3 -P
refactor/B1164/scripts/b1164_hostpx_sweep.py <with> <without> x0 x1 y0 y1 40,20,15 [--probe x,y]
[--darkb 25]`, run from `/mnt/projects/fable/review`):

| capture | stills (with − without) | rows (thr 40) | height | threshold / clip |
|---|---|---|---|---|
| B1118 | `27_spawn93_settle` − `25_mana253` | 317..600 | **≥ 284 px** (caster 210) | 315..600 = 286 at thr 20/15; **clipped at row 600** |
| (a) both runs, (c) | `23_tgt_npc_settle` − `14_cam_settle` | 329..600 / 328..600 | **≥ 272 / ≥ 273 px** (caster 209) | 272–273 at every threshold; **clipped at row 600** |
| (d) (B1118's plan re-run) | `27_spawn93_settle` − `25_mana253` | 333..600 | **≥ 268 px** | 332..600 = 269 at thr 20/15; **clipped at row 600** |
| (b), r51 camera | `29_tgt_npc_settle` − `16_empty_r51cam` | 333..600 | **268 px** | boots end at row 601 (2 px), 0 px in rows 602..606 at every threshold and by colour; not clipped (x 925..980) |
| **r51 itself** | `review/B1130/oracle/r51/26_tgt_npc_settle` − (b)'s `16_empty_r51cam` | 329..593 | **286 px** (rows 329..614) | thr 40: 593 (265 px); thr 20/15: 603 (276 px; the selection ring starts at 604); the boot's colour (B < 25, cols 936..952): 10–14 px every row to 614, 3 px at 615, 0 from 616 |
| willeq b1118 harness | `refactor/B1164/scripts/b1164_silhouette.py dev2/frames/t93_b1118/frame_00010.png` (union of caster + target on the uniform clear colour) | 367..607 | **241 px** (caster 228, rows 378..605, harness log `dev2/runs/t93_b1118.run.log`) | rendered without UI |

**The Main Chat window clips the co-located captures.** Its top border is row 601 over
x 427..820 in every oracle still (pixel (640,601) = BGR (115,130,140) in the stills with and
without the target; `hostpx_sweep.txt`, last block). In B1118, (a), (c) and (d) the target
stands on the caster at x ≈ 600..690, and the row count is still 4–10 px (threshold 40) /
16–28 px (threshold 15) at row 600 and 0 from 601 at every threshold: the figures run into the
border. So their target heights (284 / 272 / 273 /
268) are **lower bounds**, and so is the casters' 209/210 (B1130's caster unit has its feet at
row 598, `review/B1130/README.md:74`). Every per-target-height² value of those captures below is
therefore an **upper bound** (marked ≤), and their target/caster ratios (272/209 = 1.30,
284/210 = 1.35) are ratios of two clipped heights: uncertain in either direction. r51 and (b)
stand at x ≈ 925..980, right of the border, and are not clipped.

**r51's target is 286 px with its right boot (columns ~938..950) down to row 614**, not the
265 px of the dev3 measurement and not the 325 px / feet (940,652) of
`review/B1130/geom/r51.json:9`. Threshold 40 stops at row 593 because the boot (BGR
~(16,33,78)) differs from the ground (33,60,57) by at most 43/39/36/32/30/28/28/27 per channel at
rows 594..601 and 21–25 at rows 602..614 (per MR note 7787), so fewer than 3 px per row exceed 40 below 593; at threshold
20 or 15 every row 594..603 counts 11–14 px, and below 603 the selection ring's green glow takes over the
difference count, so the boot's bottom is read by its own colour (B < 25: 10–14 px in every row
to 614, 3 px at 615). 286 px (329..614) is adopted; 275 px (329..603, the rows the difference
test separates from the ring) is the lower bound. B1130's 325 px includes the "TrollF" name
label: the single-still extent of r51's band, `python3
refactor/B1164/scripts/b1164_figure_rows.py review/B1130/oracle/r51/26_tgt_npc_settle.png 895 1000
290 660`, reads rows 290..614 = 325 px, starting at the label's top row and ending at the same
boot row 614. r51 draws the human target 286/209 = **1.37** caster heights tall (the only
unclipped target; ≤ 1.37 if B1130's 209-px caster is itself clipped); willeq's b1118 harness
draws it 241/228 = 1.057.

**Results.** Gate instrument = `review/B1130/scripts/cast_measure.py <leg> 93 measure <label>
geom/<g>.json` (B1122 `fx_timing.py`, crop 150,60,990,540, caster 209 px; B1118's own row is
its crop 150,60,900,540, caster 210). Window = `WIN=-1.5,0.6,0.16,1.8 python3
refactor/B1164/scripts/b1164_regions.py <label> <frames> <f0> <f1> <tail0> <tail1> <crop>
<target feet> <target px> 209 [excludes]` (lit area in TARGET heights², the reviewer's window
and mask), frames by `refactor/B1164/scripts/b1164_extract.py`. Tail: the leg rows use the last
1.5 s of their measure window; r51's row uses or_177..256 = f7681..f7760 (the last 80 frames of
`oracle_r51/`), and the gate's own tail f7796..f7833 gives the same peak, 1.806 at f7584 for
286 px (1.978 for 265 px) (`review/B1164/worker/dev4/regions_win2_oracleR51_93_h286_gatetail.txt`,
`review/B1164/worker/dev4/regions_win2_oracleR51_93_h265_gatetail.txt`, frames extracted to
`dev4/r51_frames/`). Sprites = `refactor/B1164/scripts/b1164_sparks.py ... -1.95,1.95,0,2.55`
for the co-located captures (the same window and band for all of them); r51's sprite row uses
§2c's window −2.4,0.6,0.16,2.0, because −1.95..1.95 around r51's target reaches the caster.
≤ marks an upper bound (clipped target, above).

| capture | gate crop cov (caster²), peak | window (target²), peak | column \|dx\|<0.5 | outside | sprites p90 diam / median (target, 1e-4) | files |
|---|---|---|---|---|---|---|
| r51 (25 fps), target 286 px | 3.945 f7561 Act+2.108 | **1.806** f7584 (1.890 at 275 px; 1.978 at dev3's 265 px) | 1.542 | 0.264 | 0.039 / 2.93 (window −2.4,0.6,0.16,2.0) | `review/B1130/measure/r51_fixed_npc.txt:5`; `review/B1164/worker/dev4/regions_win2_oracleR51_93_h286.txt`; `review/B1164/worker/dev4/regions_win2_oracleR51_93_h275.txt`; `review/B1164/worker/dev4/sparks_oracleR51_93_h286.txt` |
| (a) on caster, alive | 4.071 f7296 Act+2.088 | ≤ 1.963 f7296 | 1.539 | 0.424 | ≤ 0.038 / 4.19 | `measure/ab_a25_fixed_npc.txt`; `regions_win2_ab_a25.txt`; `sparks_ab_a25.txt` |
| (b) away, slain | 4.210 f7852 Act+2.274 (end Act+4.834) | 1.984 f7852 | 1.583 | 0.401 | — | `measure/ab_b25_fixed_npc.txt`; `regions_win2_ab_b25.txt` |
| (c) on caster, slain | 4.237 f7101 Act+2.147 (end Act+4.827) | ≤ 1.980 f7099 | 1.545 | 0.435 | ≤ 0.039 / 4.05 | `measure/ab_c25_fixed_npc.txt`; `regions_win2_ab_c25.txt`; `sparks_ab_c25.txt` |
| (d) B1118 plan re-run (target ≥ 268 px) | 4.342 f8519 Act+2.028 (end Act+4.908; its crop 150,60,900,540, caster 210) | ≤ 2.115 f8518 | 1.562 | 0.552 | ≤ 0.040 / 4.32 | `measure/ab_d25_druid93.txt`; `regions_win2_ab_d25.txt`; `sparks_ab_d25.txt` |
| **B1118** (on caster, slain) | **5.847** f7573 | ≤ **2.377** f7573 | 1.562 | 0.815 | ≤ 0.049 / 6.57 | `review/B1118/measure/timing.txt:3`; `regions_win2_oracleB1118_93_humanhost.txt`; `sparks_oracleB1118_93_h284.txt` |
| willeq b1118 (gate harness) | 1.993 f152 | 1.484 f154 | 1.284 | 0.200 | 0.033 / 3.79 | `dev2/measure/t93_b1118/summary.tsv`; `regions_win2_t93_b1118_humanhost.txt`; `sparks_t93_b1118_h241.txt` |

10 fps / crf 23 controls (the driver's default rate, before `# fps=25` was added to the plans):
(a) 4.237 caster² f3080, window ≤ 2.008 (`measure/ab_a_fixed_npc.txt`,
`regions_win2_ab_a_10fps.txt`); an r51 replica (the (b) plan before its spawn line was changed)
4.101 caster² f2994 (`measure/r51rep_fixed_npc.txt`).

**Reading.**

- Neither B1118 condition alone, nor both together, makes the oracle's landing B1118's. On the
  gate instrument (caster², no target height in it) r51, (a), (b) and (c) agree within 7 %
  (3.945–4.237); (d), B1118's own plan, reads 4.342. In the window the two unclipped targets
  read r51 **1.806** and (b) **1.984** (same spot and camera; target alive vs slain, drawn 286 vs
  268 px), and (a) / (c) read ≤ 1.963 / ≤ 1.980; (d) ≤ 2.115. The window values therefore spread
  1.81–1.98 (10 % between r51 and (b)), not "within 0.021" as dev3 wrote at 265 px. The death
  shortens the landing (end Act+4.83 when the corpse is deleted 1.04 s after OP_Death; r51/(a)
  Act+6.65) but not its gate peak. So the co-location/death A/B finds **no capture condition
  that the gate's harness would have to match**; the decompile agrees (§4: s has no distance,
  co-location or death input).
- B1118's 2.377 / 5.847 is the outlier: its column is taller and wider (`dev3/grid_oracleB1118_f7573.txt`
  vs `dev3/grid_ab_a25_f7296.txt`: lit ≥ 0.86 at up 1.75 over dx 0..0.25 vs 0.13/0.06;
  dx −0.75 fully lit vs 0.40–0.75). B1118's own plan, re-run today as (d), reads 4.342 / ≤ 2.115,
  not 5.847 / ≤ 2.377. Its target's visible part measures 268 px (rows 333..600, `hostpx.txt`)
  against B1118's 284 px (317..600) in the same plan, pose and stills; both are clipped at row
  600, B1118's top is 16 rows higher on the same clip row, so B1118 drew the target larger and
  its camera sat closer — by at least ~6 % (284/268) if the hidden feet scale with the figure
  (that plan sets no `Camera2` ini keys, r51's plans do; verified). Per target height B1118 reads
  1.12× (d), a ratio of two upper bounds. **B1118's 93 landing is not reproducible today and is
  not a condition of the cast**; it should not be the gate's 93 reference without a re-capture
  (FOLLOW-UP).
- willeq is **0.82×** r51 per target height² (1.484 / 1.806; 0.79× against r51's 275-px lower
  bound, 1.890) and 0.70–0.76× against the co-located captures ((d) ≤ 2.115, (a)–(c) ≤ 1.963–1.980;
  upper bounds, so these ratios are lower bounds). Its sprites are smaller in diameter: p90
  0.033 vs r51's 0.039 (r51's window); against the clipped co-located captures (p90 ≤ 0.038–0.040)
  that is not decidable; its median area is not smaller than r51's (3.79 vs 2.93e-4), and
  against the co-located medians (≤ 4.05–4.32) it is not decidable either. That is the
  per-particle-area gap acceptance B asks about; it is NOT explained by a word §2a reads wrongly
  (sizes, multiplier, depth bias, blend, texture are applied as written), and the one multiplier
  willeq does not apply is s (§4). An oracle s of ≈ 1.10 (√(1/0.82)) to 1.18 (0.039/0.033)
  for this target would cover it, but s is not pinned statically: `0x10039480` reads the
  point's world matrix, which is model data at runtime.
- Open question: r51 (286 px) and leg (b) (268 px) are the same model at the same spot and
  camera yet differ by 18 px; this is unexplained, and the 275-px lower bound covers only part
  of it.
- willeq's gate number has a second, separate factor: its target is drawn 1.057 caster heights
  tall against r51's 1.37, so in caster units its landing host is (1.057/1.37)² = **0.60×** the
  oracle's (against the co-located captures' 1.30–1.35 it would be 0.61–0.66×, but those ratios
  are uncertain, above). 0.60 × 0.82 = 0.49 ≈ 1.993 / 3.945 = 0.505 (r51 on the gate).


## 6. Instrument (first leg; env-gated, local patch — not committed as a default)

- `WILLEQ_FX_COUNT=1` — per-emitter live count + config per frame
  (`sim_particles.cpp`, after `particleActiveCount_`).
- `WILLEQ_FX_AREA=1` — per-emitter quad area / meanSize / meanHeight / meanDepth /
  meanAlpha, plus `skipR` (the +0x198 / r<0 draw gate) and `skipProj` (projection /
  near-clip) counters and a per-frame `[fx-frame] f=… cnt=… drawn=… skipR=… skipProj=…`
  summary (`unified_particle_renderer.cpp`). 239/239 frames: `skipR=0 skipProj=0`,
  `drawn == cnt == sim total` — the renderer draws every alive particle, so the density
  gap is not a dropped draw.


## 7. Re-gate (acceptance C)

This card changes **no C++, config, golden, manifest or baseline**: the only files are this
README and `refactor/B1164/scripts/`. The binary every gate row runs is therefore the
B1157 tip's, and no row can move. The 93 row was re-rendered and re-measured on this
branch to confirm it (`scripts/run93.sh t93_b1118 tour_93.tsv --camera b1118 --seed 1`, then
`FX_KEEP=1 python3 -I /mnt/projects/fable/review/B1139/scripts/harness_measure.py frames/t93_b1118 measure/t93_b1118`):

| row | B1157 after (`refactor/B1157/b1118_table.tsv`) | this branch | owner of the remaining delta |
|---|---|---|---|
| tour 93 (b1118) | land 1.993, end +6.633 | land 1.993, end +6.633 (`measure/t93_b1118/summary.tsv`) | §5: 0.60× from the target's relative size (FOLLOW-UP 2) × 0.82× per target height² (FOLLOW-UP 3), both against r51; the 5.847 reference is not reproducible (FOLLOW-UP 4) |
| tour 93 (b1118), target slain at Act | — | land 1.985, end +4.700 (`measure/t93_b1118_die/summary.tsv`) | record only |
| r0 93 fixed_npc (b1130) | land 0.358 (`refactor/B1157/b1130_table.tsv:2`) | not re-run (no code change); the b1130 preset's target is fitted to a wrong reference (§5) | the b1130 target fit (FOLLOW-UP 1) |

Every other tour-7 / 40-record row (both presets) is the B1157 after column unchanged,
with B1157's owners (`refactor/B1157/README.md` sec. 6).

**93 clip (acceptance D):** the first leg's `clips/b1164_93_current.mp4` (+ `_strip.png`,
`refactor/B1158/scripts/make_clip.py`, spec `specs/b1164_93_current.json`) is the current
behaviour; with no code change it is the after clip too. The like-for-like evidence is the
table in §5, not the clip (the clip's oracle side is B1118's capture, §5 (d)).

## 8. Acceptance

| # | item | status |
|---|---|---|
| A | population attributed with the decompile line, 93 count re-measured | **met** — §1: `perTick × tps` (0x10060fe2), 1550 is the word count; no spawn bug, so no fix |
| B | per-particle area word named + fixed + re-measured | **NOT met** — measured like for like (§5, target heights², the same window/band): willeq 1.484 vs the oracle 1.806 (r51, unclipped target) to ≤ 2.115 (co-located captures, clipped targets: upper bounds) = 0.70–0.82×, sprite p90 diameter 0.033 vs 0.038–0.040. No condition of the cast explains it (A/B (a)–(d)); the only multiplier willeq does not apply is the attach scale s (§4), whose runtime value is not pinned, so no word can be named and fixed from evidence (FOLLOW-UP 3) |
| C | both sweeps + tour-7 before/after vs B1157 with owners | **met** — §7 (no code change, so no row moves; 93 re-measured equal) |
| D | 93 clip | **met** — first leg's clip, unchanged behaviour (§7) |
| E | targeted ctest + hygiene + CI build green | no C++ change, so no new device-free test; the MR pipeline is the build/hygiene check |
| F | MR "Closes #483" | the MR says **Refs #483** (orchestrator ruling: #483 stays open until B is met) |

## 9. FOLLOW-UP (named owners — no card cut, per the worker rules)

1. **b1130 target reference** (owner: the B1155 harness preset, `fitFixedNpcTarget`,
   `tools/model_viewer/README.md:73`, and B1130's geometry): r51's target is 286 px (rows
   329..614) with its right boot at columns ~938..950 down to row 614 (§5,
   `review/B1164/worker/dev4/hostpx_sweep.txt`), not the 325 px / feet
   (940,652) of `review/B1130/geom/r51.json:9` that the preset fits (its 316 px / row 782,
   `refactor/B1155/README.md:246`). Re-fit and re-run the 40-record b1130 sweep; every B1130
   geom json carrying `tmpx 325` / `tfeet 940,652` reads its landing rows in the wrong units.
2. **Target size relative to the caster** (owner: entity scale — `config/race_models.json`,
   `entityScaleForServerSize`, the harness's caster/target appearance): the official client
   draws the race-1 male `#spawn` (size 6) 286/209 = 1.37 caster heights tall at r51's spot
   (the only unclipped target; ≤ 1.37 if the 209-px caster is clipped), willeq's b1118 harness
   241/228 = 1.057. At the caster's spot (B1118, (a), (c), (d)) both figures are clipped at
   row 600 by the Main Chat window (top border row 601, x 427..820), so 272/209 = 1.30 and
   284/210 = 1.35 are ratios of two lower bounds and uncertain (§5). Which model is off (the
   size-0 DAF caster or the size-6 HUM target) needs an **unoccluded oracle reference**: both
   figures fully above the chat window (chat window moved or closed, or a camera that puts the
   feet above row 600), measured against a fixed reference; this factor alone is 0.60× (against
   r51) of the gate's 93 number in caster units.
3. **The attach scale s at runtime** (owner: the FX-sim sprite lane, B1149/B1151 successor):
   record 80's landing is point-attached, so the client multiplies sizes, velocities, shape and
   offsets by `0x10039480`(SPELLPOINT_DEFAULT) (§4); willeq passes 1. The remaining 0.82× per
   target height² (against r51) and the 1.18× sprite p90 diameter (0.039/0.033) are the size an
   s ≈ 1.10–1.18 would give, but s is
   model data at runtime: pin it (a lane-5 debugger read of `0x10039480`'s return for this
   target, or the model's point matrix), then implement it with a unit test.
4. **B1118's 93 reference** (owner: FX oracle legs / the gate baseline, BLESS-ONLY): B1118's
   own plan re-run today (§5 (d)) reads 4.342 / ≤ 2.115, not 5.847 / ≤ 2.377, with a target drawn at
   least ~6 % smaller (both clipped at row 600 by the Main Chat window; B1118's plan sets no
   `Camera2` ini keys). Re-capture the b1118 oracle references
   with the camera pinned before 5.847 (or B1157's 3.049 lane band) is used as a target.

## 10. Continuation "pin s" (dev5): s pinned at runtime and implemented; the unoccluded 93 reference

Evidence: `/mnt/projects/fable/review/B1164/worker/dev5/` (lane-5 legs `oracle/`, the gdb probe
logs `review/B1164/worker/dev5/gdb/`, the gate re-run `gate/before` / `gate/after`, the measurements beside them). Three
lane-5 legs under the lease (`oracle_class_leg.sh 5 6 <plan> <out>` from hermes-ext, owner
`kb-b1164-pin-s_*`), each ended `cleanup exit=0 lane=5 lock=state=FREE` and `char 67 restored:
class 12, gems 93,38,54,41,253,458,656` (`review/B1164/worker/dev5/leg1.console.log`, `review/B1164/worker/dev5/leg2.console.log`,
`review/B1164/worker/dev5/leg3.console.log`); char 103 (Oraclewatch) was not logged in (its corpse lies beside the caster
in every still, as found). Plans: `refactor/B1164/plans/pin_s_unoccluded.tsv` (leg 1),
`pin_s_gdb.tsv` (leg 2), `pin_s_gdb2.tsv` (leg 3).

### 10.1 s pinned: method (b), the lane-5 read, cross-checked by (a), the client's code

**The probe.** `refactor/B1164/scripts/b1164_s_watch.sh` runs on the ThinkPad, waits for the
leg's `END <n>_gdb_arm` row in `timeline.tsv`, and attaches gdb (`sudo gdb -p <eqgame.exe>
--batch -x b1164_s_gdb.py`) for a fixed window, then detaches. The probe only reads registers and
memory; its breakpoints are gdb's own int3s, removed at detach, and it refuses to insert any
until the bytes at every address match the disassembly. That check saved leg 1: Wine loads
EQGraphicsDX9.dll at 0x01d70000, not its preferred 0x10000000 (`review/B1164/worker/dev5/gdb/run1/maps.txt:1`), and leg 1's
probe read zeros there and refused (`review/B1164/worker/dev5/gdb/run1/s_probe.log:1`). A manual re-attach with the
base (`review/B1164/worker/dev5/gdb/run2/`) came after leg 1's second cast had ended and logged only
zone light emitters (SKTGLW_PCD, point-attached, s 1.199892..1.199971 -- object scale 1.2). Legs 2 and 3 pass the loaded base
(`review/B1164/worker/dev5/gdb/run4/watch.log:3`); every expected byte matched (`review/B1164/worker/dev5/gdb/run4/s_probe.log:1`..`:3`).
Breakpoints (DLL VAs; + 0xf1d70000 at runtime): 0x100609ee (the point path's `fstp` of
`0x10039480`'s return), 0x10060b82 (every attach path joined: the final s6C `[esp+0x30]` and
s19C `[esp+0x48]`), 0x10060b27 (the track path, after s6C = `0x1005c4f0(track)` × √(row0·row0)).

**What it read** (spell 93 cast on TrollF from r51's set-up; `review/B1164/worker/dev5/gdb/run3/s_probe.log`,
`review/B1164/worker/dev5/gdb/run4/s_probe.log`):

- **Record 80's slots are TRACK-attached (+0x70), not point-attached.** Every Source and Target
  instance has `point=0x00000000` and a non-zero `track` (`review/B1164/worker/dev5/gdb/run3/s_probe.log:589`, S3-1 on the
  target). So §4's "attach point (+0x6C) — record 80's case" row is wrong: the path that runs is
  the track row of §4's table, 0x10060ae8..0x10060b23, and `0x10039480` is not reached for these
  slots.
- **Target slots (on TrollF, race 1, OP_NewSpawn size 6.000): s = 1.200000**, for all four
  emitters with +0x6C = 1 (280, 282, 9; 281 has +0x6C = 0 and reads s6C = 1.000000, s19C =
  1.200000). The factors: row 0 of the track's world matrix = (−0.459220, 1.108655, 0.000000),
  length **1.200000**, `0x1005c4f0` = **1.000000** (`review/B1164/worker/dev5/gdb/run4/s_probe.log:704`, IgniteTargetill
  Imp01; `review/B1164/worker/dev5/gdb/run4/s_probe.log:708`, SFofSeukor S3-1). The value is the same in every sample over
  the landing (the probe logs the 1st..3rd hit and every 50th per emitter).
- **Source slots (on the caster Testcaster, race 6, OP_ZoneEntry size 0.000): s = 1.000000** on
  the DEFAULT slots 277/278 (row 0 = (1, 0, 0); `review/B1164/worker/dev5/gdb/run4/s_probe.log:626`) and 0.999755 on the
  hand slot 276 (`review/B1164/worker/dev5/gdb/run4/s_probe.log:622`; a hand bone's row, length 0.999755).
- **Target slots on DarkF (race 6 female, OP_NewSpawn size 5.000, id 262, after TrollF's
  OP_DeleteSpawn in `31_depop`): s = 1.000000.** Leg 3's second 93 cast was read: the probe
  ran until the client quit (`review/B1164/worker/dev5/gdb/run4/s_probe.log:5888`, `DONE ... t=113.785`), and its
  clock starts at the attach (`review/B1164/worker/dev5/gdb/run4/watch.log:2`, epoch 1793115854.17), so
  `43_cast_daf` (BEGIN 1793115944.675, `review/B1164/worker/dev5/oracle/pin_s_gdb2/timeline.tsv:85`) is t ≈ 90.5
  and `44_fx_daf` ends at t ≈ 108.8 (`review/B1164/worker/dev5/oracle/pin_s_gdb2/timeline.tsv:88`). The landing reads
  from t = 92.797 to 99.155 (`review/B1164/worker/dev5/gdb/run4/s_probe.log:4852` .. `review/B1164/worker/dev5/gdb/run4/s_probe.log:5271`), on the reused
  actor 0x248083a8 / track 0x1520f860 (TrollF's at `review/B1164/worker/dev5/gdb/run4/s_probe.log:705`): S3-3
  (`review/B1164/worker/dev5/gdb/run4/s_probe.log:4853`), S3-1 (`review/B1164/worker/dev5/gdb/run4/s_probe.log:4857`) and IgniteTargetill Imp01 (`review/B1164/worker/dev5/gdb/run4/s_probe.log:4859`)
  have +0x6C = 1 and s6C = s19C = 1.000000 (row 0 = (1, 0, 0), norm 1.000000); S3-2 has +0x6C =
  0 and s19C = 1.000000 (`review/B1164/worker/dev5/gdb/run4/s_probe.log:4861`). A size-5 dark elf reads 0.2 × 5 = 1.0: the
  second live point of the rule, on a second race and size (spawn sizes: `python3 -P
  refactor/B1164/scripts/b1164_spawns.py oracle/pin_s_gdb2/capture.pcap DarkF TrollF`). Its
  `#stun` effect's track read 0.999714 before the cast (`review/B1164/worker/dev5/gdb/run4/s_probe.log:4082`).

**(a) The client's code behind the numbers.**

- Which track: `0x10046830` (`EQGraphicsDX9.dll.c:50613`) builds the actor's SPELLPOINT_* table;
  SPELLPOINT_DEFAULT's track name is the model tag with no suffix (`EQGraphicsDX9.dll.c:50779`,
  the tag copied from the actor's +0x8c just above), i.e. the model's root track. For the old-world human that is `HUM_TRACKDEF` in
  `global_chr.s3d`: one frame, rotation (16384, 0, 0, 0) / 16384 = identity, |q| = 1.000000
  (DAF_TRACKDEF: (−16384, 0, 0, 0), |q| = 1.000000): `python3 -I
  refactor/B1164/scripts/b1164_root_track.py /mnt/projects/EverQuestP1999/global_chr.s3d HUM`
  (and `globaldaf_chr.s3d DAF`) → `review/B1164/worker/dev5/root_track.txt`, the WLD 0x12 fragment's int16 frame. A root
  track with a unit quaternion adds no scale, so the row's length is the actor's world scale.
- The actor's scale: eqgame `0x47fa9e` (`eqgame.exe.c:102153`) reads the spawn's size (+0x23c;
  when it is not > 0 it takes the race default from `0x47a147` and stores it back), calls
  `0x47a0e9` at `0x47fb82` and stores the result in the actor at `0x47fb87` (+0xC4).
  `0x47a0e9` (`eqgame.exe.c:97640`) returns size × 0.2 (`_DAT_00640fec` = 0.2f,
  `eqgame.exe.c:97652`) for every race `0x47937c` (`eqgame.exe.c:96795`) lets through (all but
  0x193 and ≥ 0x195), × 0.769 more only while the global DAT_00923c30 is set — it was not on lane 5:
  s is exactly 0.2 × 6. `0x47a147` (`eqgame.exe.c:97667`): human 6.0 (the default case), barbarian
  7.0, wood / dark elf 5.0, half elf 5.5, dwarf 4.0, troll 8.0, ogre 9.0, halfling 3.5, gnome 3.0
  (the constants read from eqgame.exe's .rdata). `0x47a239` (`eqgame.exe.c:97743`) is the
  per-race override, read in full in 10.2; it does not touch the scale of races 1 and 6.
- So for this target: s = 1.0 (root track) × 0.2 × 6 = **1.2**, for DarkF 0.2 × 5 = **1.0**, and
  for the caster 0.2 × 5 (race default, size 0) = **1.0**. (a) and (b) agree to the printed digit
  at all three points.

**s is not 1.0 for this point.** It is the target's actor scale, 1.2.

### 10.2 Implemented: the client's whole actor-scale rule, no cut-off

**The rule, every branch read** (fix leg, review note 7941 finding 2; dev5 had kept 1.0 for every
race ≥ 0x1a, which is not a client number). The constants are read from eqgame.exe's sections by
`python3 -I refactor/B1164/scripts/b1164_rdata.py /mnt/projects/EverQuestP1999/eqgame.exe 640fec
647cd0 647cd4 642d50 64264c 642658 640e58 64263c 642650 641fa0 6410cc 642d68 640e24 647cd8 6410b4
642ae8 640e38 63fda4 63f4cc 923c30 923798` (0.2, 0.7692308, 1/6, 1.3, 6, 7, 5, 5.5, 4, 8, 9, 3.5,
3, 4.7, 2.0, 0.05, 0.1, −1.0, 0.0; the two globals are .bss, 0 at load).

1. **Size.** `0x47fa9e` (`eqgame.exe.c:102153`) tests the spawn's size +0x23c against 0.0 at
   `0x47fb07` (`eqgame.dis:164242`; `test ah,0x41 / jp` = "> 0"); when it is not > 0 it calls
   `0x47a147(race +0x260, gender +0x24e)` and stores the result back at `0x47fb30`
   (`eqgame.dis:164253`).
2. **The default, `0x47a147`** (`eqgame.exe.c:97667`, `eqgame.dis:156857`): races 1..12 through
   the jump table at 0x47a209 (its 12 entries read from the file: 1 → 6.0, 2 → 7.0, 3 → 6.0, 4 →
   5.0, 5 → 6.0, 6 → 5.0, 7 → 5.5, 8 → 4.0, 9 → 8.0, 10 → 9.0, 11 → 3.5, 12 → 3.0), then compares:
   0x2a and 0x78 → 3.0 for gender 2 else 6.0; 0x2b → 4.7; 0x4d, 0x70, 0x14a → 5.0; 0x51 → 3.5;
   0x58 → 3.0; 0x5a, 0x82 → 7.0; 0x5c → 8.0; 0x5d → 9.0; 0x5e → 4.0; every other race (0, 0x18,
   0x4e, 0x80, …) → 6.0.
3. **Size → scale, `0x47a0e9`** (`eqgame.exe.c:97640`; called at `0x47fb82`, stored at actor +0xC4
   at `0x47fb87`, `eqgame.dis:164276`..`eqgame.dis:164277`): size × 0.2, × 0.7692308 more while
   0x923c30 is set; for the races `0x47937c` (`eqgame.exe.c:96795`) selects, 0x193 and ≥ 0x195,
   size × 1/6 × 1.3 while 0x923c30 and the byte 0x923798 are both 0, else size × 1/6. 0x923c30 is
   NewZone_Struct offset 592 (`0x45c006` copies the 700-byte OP_NewZone to 0x9239e0); both read 0
   on lane 5 (s = 0.2 × 6 exactly).
4. **The override, `0x47a239`** (`eqgame.exe.c:97743`..`eqgame.exe.c:97846`; the linear
   `eqgame.dis` decodes it from `0x47a23c`, `eqgame.dis:156939`..`eqgame.dis:157035`): called at `0x47fbff`
   (`eqgame.exe.c:102217`) with the scale slot preset to −1.0 at `0x47fbbe` (`eqgame.exe.c:102212`),
   and the slot is written over +0xC4 at `0x47fc41` only when it is not −1.0
   (`eqgame.exe.c:102223`). It writes that slot for twelve races only: 0x31, 0x49, 0x72, 0x8d,
   0x8e, 0x8f, 0x9e, 0xc4, 0xfc, 0x194 → 1.0 (`fld1` at `0x47a371`, `eqgame.dis:157027`); 0x48 →
   2.0 (`0x47a29b`, `eqgame.dis:156964`); 0x6c → 0.05 (`0x47a2d7`, `eqgame.dis:156980`). Races
   0x1a, 0x1b, 0x24, 0x32, 0x5b, 0x78, 0xab, 0x14a, 0x164 write only its other outputs (+0x234,
   +0x238, +0x22c); every other race returns at once. So every race not in the twelve keeps step
   3's scale.

**Where it lives — one shared header.** `include/client/graphics/actor_scale.h` holds the rule as
`EQT::Graphics::clientActorScale(race, gender, size, zoneScaleFlag, flag923798)`
(`actor_scale.h:105`; the default table `actor_scale.h:30`, the size/6 races `actor_scale.h:59`,
`0x47a0e9` `actor_scale.h:69`, `0x47a239` `actor_scale.h:81`, the size resolution of step 1
`actor_scale.h:100`). It is B1172's file (#491, branch `task/kb-b1172-model-height`, the entity
model's drawn scale): the fix leg first added it byte-identically (blob ff57980f), every branch and
constant re-read above and agreeing, then merged B1172's fix leg a7798e47, which added
`clientResolvedActorSize`; nothing of B1164's own is in it. `EQ::SpellFxAttachScale(race,
gender, serverSize)` (`spell_fx_attach_scale.h:52`) returns `clientActorScale()`; the flags default
to the lane-5 0. The root track's |row 0| factor (1.000000 for HUM and DAF, 10.1 (a)) is taken as 1
for every model.

**The EDD path.** `SpellVisualFX::entityAttachScale()` (`spell_visual_fx.cpp:138`) replaces the
constant: Source slots pass the caster's s (`spell_visual_fx.cpp:731`), Target slots the landing
entity's (`spell_visual_fx.cpp:1064`); the +0x6C / +0x19C sites downstream are unchanged
(`spell_emitter_presets.cpp:395`, `:420`, `:463`) and now receive s. The Middle stage's missile
actor keeps `kSpellFxAttachScale` (`spell_visual_fx.cpp:1279`, `spells_new_edd_reader.h:263`, not
traced). The renderer resolves s from the entity's race, gender and Spawn_Struct size
(`renderer_entities.cpp:319`); the FX harness from its caster / target options
(`model_viewer_fx_harness.cpp:689`). The harness caster's server size is 0
(`model_viewer_fx_harness.cpp:515`): the oracle's OP_ZoneEntry carries size=0.000 for Testcaster
(`python3 -P refactor/B1164/scripts/b1164_spawns.py oracle/pin_s_25fps/capture.pcap Testcaster`)
and the probe read her s as 1.000000; in the gate's binaries (before the B1172 merge) the drawn
model did not change (`entityScaleForServerSize` gave the race scale at size 0, as at 6/6).

**The local player's own size** (review finding 7, verified, then fixed). Before this leg the
player's entity kept fable's `zone_entry.cpp` placeholder size 6.0: willeq read no spawn packet
for itself (`OnZoneEntry` only logged; T332 measured that no ZoneSpawns / NewSpawn self branch runs
on a graphical zone-in). Replaying a dark-elf OP_PlayerProfile on the pre-fix tree read entity size
6.0 → s = 1.2, where the client's is 1.0 (RED 2/4,
`review/B1164/worker/dev5/fix/local_player_red.txt`; GREEN 4/4 after the fix,
`review/B1164/worker/dev5/fix/local_player_green.txt`). The server's OP_ZoneEntry, which follows
OP_PlayerProfile in the oracle capture (S→C at 1793114146.115, 385 bytes; Testcaster size 0.000), is
our own Spawn_Struct. After the merge with B1172 there is ONE read of it (`zone_dispatch.cpp:138`,
B1172's form of the same read): its size (+75) resolved by `clientResolvedActorSize` (race default
for 0, eqgame `0x47fb30`) goes to both our entity record (the actor scale, so the Source slots' s)
and PlayerState (the wire-z offset); the profile holds the same resolved default until it arrives
(`zone_entry.cpp:480`). `tests/test_local_player_spawn_size.cpp` pins it: the profile alone gives a
dark elf 5.0 / s 1.0 (`test_local_player_spawn_size.cpp:73`), a size word 0 resolves to 5.0 / s 1.0
(`test_local_player_spawn_size.cpp:83`), a sent size is taken (`test_local_player_spawn_size.cpp:94`),
another spawn's OP_ZoneEntry is ignored (`test_local_player_spawn_size.cpp:104`).

**Tests.** `tests/test_spell_fx_attach_scale.cpp`, table-driven: the three live points
(`test_spell_fx_attach_scale.cpp:41`), 32 size-0 defaults incl. both gender-2 races
(`:49`), the fifth for every unforced race incl. the nine that reach `0x47a239` without touching
the scale (`:88`), the size/6 races 0x193 / 0x195 / 500 (`:106`), all twelve forced races at
several sizes (`:115`), and the two flags (`:156`); 6/6 pass. `tests/test_local_player_spawn_size.cpp`
4/4.

### 10.3 The unoccluded oracle reference (follow-up 2)

Leg 1 (`review/B1164/worker/dev5/oracle/pin_s_25fps/`) set `[Defaults] HideChatWin=TRUE` (restored byte-identically:
`review/B1164/worker/dev5/oracle/pin_s_25fps/eqclient-iniset-restored`), and the client still drew the Main Chat window (top border row 601,
`(640,601)` = BGR (115,130,140) in every still, `review/B1164/worker/dev5/hostpx_sweep_dev5.txt`): that key does not hide
this UI's chat. It is not needed at r51's geometry: the target stands at x 860..1010, right of the
window, and the caster's rows end at 595 with 0 px in rows 596..600, above the border. Both
figures are measured unclipped by difference (`refactor/B1164/scripts/b1164_hostpx_sweep.py`, run
from `review/B1164/worker/dev5/oracle/pin_s_25fps/`, output `review/B1164/worker/dev5/hostpx_sweep_dev5.txt`):

| figure | with − without (same camera) | rows (thr 40 / 20 / 15) | height |
|---|---|---|---|
| target TrollF (race 1, size 6) | `33_tgt_npc_settle` − `16_empty_r51cam` | 333..600 / 333..606 / 333..606 (rows 601..606 at thr 20/15 are the selection ring's glow; the boots' colour test B<25 ends at 600) | **268 px** |
| caster Testcaster (race 6, size 0) | `16_empty_r51cam` − `18_bg_r51cam` (the caster drawn as race 127 by `#set race 127`, restored by `#set race 0`) | 389..595 at every threshold | **207 px** |
| DarkF (race 6 female, size 5), on the target's spot | `51_daf_settle` − `16_empty_r51cam` | 389..593 at every threshold | **205 px** |

- **Target / caster = 268 / 207 = 1.295** (r51's 286 / 209 = 1.37 of §5 was a different target
  height on the same plan; this leg's 268 equals dev3's (b), 268, at the same spot).
- The two spots stand at one depth: a DarkF drawn at the target's spot is 205 px, the caster at
  hers 207. So 268 / 205 = **1.307** is the human / dark-elf model ratio as the client draws them
  at sizes 6 and 5.
- willeq draws the same pair 241 / 228 = **1.057** (§5). Times the client's 0.2 × 6 / (0.2 × 5) =
  1.2 for these sizes that is 1.268, within 3 % of 1.307.
- **Which model is off: the TARGET.** willeq (the gate's binaries, before B1172) drew the size-6
  human at its race scale × 6/6 = 1.0 (`entityScaleForServerSize`, which B1172 replaced with
  `clientActorScale`); the client draws it at 0.2 × 6 = 1.2 (`0x47a0e9`). The dark-elf caster is 1.0 in both. The 0.60× "relative target size" factor of §5
  carries this 1.2 (1/1.2² = 0.69); the rest of it is r51's 286-px target reading (this leg: 268).
  Fixed by B1172 (merged into this branch, 10.6 item 1), not by this card's code.
- The 93 landing of leg 1: gate instrument 4.086 caster² (`review/B1164/worker/dev5/measure/pin_s25_fixed_npc.txt`,
  `cast_measure.py oracle/pin_s_25fps 93 measure pin_s25 geom_r51_fixed_only.json`), within dev3's
  3.95–4.24; window method (§5's) at this leg's 268 px / feet (955,600): **1.949** target
  heights² at f10626 (`review/B1164/worker/dev5/regions_win2_pin_s25_h268.txt`), sprite p90 diameter 0.043 target heights
  (`review/B1164/worker/dev5/sparks_oracle_pin_s25_h268.txt`, window −2.4,0.6,0.16,2.0).

### 10.4 The 93 like-for-like ratio, before / after (b1118 tour 93, `gate/<label>/frames/b1118_tour_93`)

(Fix leg: the re-gate of 10.5 on the merged tree rendered tour 93 again for both binaries; all 435
frames of each are byte-identical to dev5's (`cmp`), so every number in this section stands.)

Window method of §5 (`WIN=-1.5,0.6,0.16,1.8 b1164_regions.py … 140 170 345 390 170,0,1060,650
640,607 <host> 235`), willeq's own drawn target 241 px; files `review/B1164/worker/dev5/regions_win2_t93_before_h241.txt`,
`review/B1164/worker/dev5/regions_win2_t93_after_h241.txt`; sprites `review/B1164/worker/dev5/sparks_t93_{before,after}_h241.txt`:

| | gate land_cov (caster²) | window (target²) | vs r51 1.806 (286 px) | vs leg 1 1.949 (268 px) | sprite p90 diam (target) |
|---|---|---|---|---|---|
| before | 1.993 | 1.480 f154 | 0.82× | 0.76× | 0.033 |
| after (s = 1.2 on the target) | **2.719** | **1.906** f157 | **1.06×** | **0.98×** | 0.040 (r51 0.039, leg 1 0.043) |

The ratio moves from 0.82× to 1.06× against r51 (0.76× → 0.98× against the unoccluded leg). The
gate number moves from 0.505× to 0.689× of r51's 3.945 (0.488× → 0.665× of leg 1's 4.086).

**Read it with 10.3.** The like-for-like unit is the target's height, and willeq still draws the
target at 1.0 where the client draws it at 1.2. Normalised by the height the client would draw
(241 × 1.2 = 289.2 px, passed as `289.2`), willeq's window is 1.056 before and 1.396 after
(`review/B1164/worker/dev5/regions_win2_t93_before_h289.txt`,
`review/B1164/worker/dev5/regions_win2_t93_after_h289.txt`, regenerated in the fix leg with the committed
`b1164_regions.py`, whose header now prints the host argument as given, `host 289.2px`; dev5's
files had been made with 289.2 under a header that printed `289`, so a re-run with 289 read
1.057 / 1.398) = 0.77× r51 / 0.72× leg 1 after. At the
gate the remaining 0.665× (leg 1) is that same target scale: 0.98 × (1.057 / 1.295)² = 0.65. So s
is fixed and it is the client's number, but once the target model is drawn at the client's scale
(B1172, merged into this branch in the fix leg, 10.5) the landing per target height² is ~0.72–0.77× the oracle's again: **acceptance B
stays NOT met**. Next candidate: the sprite's projected half-width at `0x1006335a`..`0x1006336f`
— `[esp+0xc8]` × `[eax+0x18]` × `[esp+0x1c]` × s6C × the particle's
size × 0.5 (0x101c594c) — whose three non-s factors are not pinned.

### 10.5 Re-gate on the merged tree (fix leg), and every drift from B1157 attributed

**Builds and renderer.** The fix leg merged gitlab/fable f61fafb4 (B1168, B1166, B1169 harness
determinism, B1173, B1165) into the branch (`tests/cmake/database.cmake`: both appended blocks
kept; `spell_visual_fx.cpp` auto-merged, B1165's Middle-stage call keeps `kSpellFxAttachScale`,
`spell_visual_fx.cpp:1279`) and re-rendered the whole gate on the ThinkPad for two binaries:
**before = f61fafb4** and **after = this branch at 9938bbfa** (both contain B1169;
`refactor/B1164/scripts/run_gate_fix.sh <before|after> <bin> <b1118|b1130>`, B1157's plans, fps 30,
seed 1, sim 60 Hz on b1130, NVIDIA surfaceless EGL depth 24 as dev5's gate; 87/87 rows rendered
and measured in each, `review/B1164/worker/dev5/fix/gate/before/gate.log`, `review/B1164/worker/dev5/fix/gate/after/gate.log`). **Neither binary contains B1172**
(merged into this branch afterwards, at a7798e47, for the shared header and the local player's
size): B1172 draws the target at the client's 1.2, which moves the like-for-like unit but not s.
Tables: `refactor/B1164/fix_b1118_table.tsv`, `refactor/B1164/fix_b1130_table.tsv` (`python3 -I
refactor/B1164/scripts/compare_fix.py review/B1164/worker/dev5/fix/gate review/B1164/worker/dev5/gate
review/B1164/worker/dev5/fix/attrib . <out_b1118> <out_b1130>`): per row B1157's number, **base** (dev5's
before, 4275bc51's code), **base_i915** (b1130 only, below), before, after, and which pair moves
(> 3 % land / src, > 0.10 s end). dev5's before/after (10.5 of dev5) predate B1169; for the b1118
rows the two before binaries (4275bc51 and f61fafb4) read identically on every row (mv_tip empty
on all 47), and tour 93's frames are byte-identical across all four renders (10.4).

**After vs before (this card).**
- b1118: 32 of 47 rows move, all in landing coverage, all upward, all on the `other` target (the
  race-1 size-6 `#spawn`, s = 1.2); the 13 `self` rows, r35 (253, +1.6 %) and r27 do not move.
- b1130: 23 of 40 rows move, all `fixed_npc`, all upward in landing coverage; no `fixed_self` row
  and no source coverage moves. Three rows gain a landing reading after (r2, r15, r22: `nan` →
  0.316 / 0.173 / 0.172).
- **End-time moves, mechanism measured** (finding 5; `python3 -I refactor/B1164/scripts/b1164_end_mechanism.py
  <before fable_<sid>> <after fable_<sid>>` → `review/B1164/worker/dev5/fix/end_mechanism/<tag>.txt`; it re-derives
  fx_timing.py's rule from the lit80.tsv it saves). In every case both runs have the **same**
  thresholds (thr2 from the tail, the onset threshold 1.5 × the source tail), so the moves are the
  lit series, not the instrument:
  - b1118 r7 (spell 40): thr2 1460 px in both; after's tail is lower frame by frame (f265 2159 →
    1584, f274 1471 → 681), so it drops below 1460 at f266 instead of f275: end f274 → f265,
    5.633 → 5.333 s. The LANDING onset moves too, f147 → f132 (Act+1.400 → +0.900 s): the onset
    threshold is 45213 px in both, after's rise is larger (f132 35031 → 46099 px) and crosses it 15
    frames earlier (`review/B1164/worker/dev5/fix/end_mechanism/b1118_r7_40.txt`; dev5's frames read the same,
    `review/B1164/worker/dev5/gate/before/measure/b1118_r7_40/measure_harness.txt:5`). Fewer particles remain at the end
    after (connected blobs ≥ 2 px at f270: 49 → 43, f274: 45 → 39, on the fix gate's frames).
  - b1118 r23 (372) 6.433 → 6.300, r32 (374) 7.217 → 7.083: same thresholds (1412 / 1587 px),
    after's tail falls below them 4 frames earlier (f283 → f279, f302 → f298); onsets 2 frames
    earlier. r24 (348): before has no onset (its lit never exceeds the 1.5 × source-tail threshold
    106160 px), after crosses it at f118 and ends at f277, 5.983 s.
  - b1130 r7 (40) 6.100 → 4.567: the 0.3-s gap rule. Both runs have a first landing run and a
    second lit episode from ~f256 to f290 (≈ 650–950 px). Before, the first run's last frame above
    thr2 312 px is f249 and the second episode is back above it at f258, 0.300 s later, which the
    rule (a gap > 0.3 s ends the run) bridges, so the end is the second episode's (f288). After, the
    first run's last frame above 312 is f242 and the second episode returns at f264, 0.733 s later,
    so the end is f242
    (`review/B1164/worker/dev5/fix/end_mechanism/b1130_fixed_npc_r7.txt`). b1130 r13 4.567 → 4.300 and r32 7.183 →
    7.050: RISE end f242 → f234 and f301 → f297 under the same threshold
    (`review/B1164/worker/dev5/fix/gate/before/measure/b1130_fixed_npc_r13/measure_b1130.txt:7`,
    `review/B1164/worker/dev5/fix/gate/after/measure/b1130_fixed_npc_r13/measure_b1130.txt:7`).
  - Owner of every one: s (the only input the two binaries differ in on the EDD path), through the
    sizes / velocities (+0x6C) and shape / offsets (+0x19C) it multiplies.

**B1157 → base (the reviewer's list, finding 3), attributed by rendering.**
- **The renderer, not code.** B1157's b1130 sweep ran on the ThinkPad's i915 DRM path, depth 16
  (all 40 of its run.logs: `DRM: Opened /dev/dri/card1 ... driver=i915`, `EGL: Config depth=16`,
  e.g. `review/B1157/worker/b1130/frames/b1157_fixed_npc_r0/run.log:38`), its b1118 sweep on NVIDIA surfaceless
  (`review/B1157/worker/b1118/frames/b1157_r0_/run.log`); dev5's gate forced NVIDIA surfaceless, depth 24, for both.
  Re-measuring B1157's own frames with 4275bc51's measure script reproduces B1157's rows exactly
  (`review/B1164/worker/dev5/fix/attrib/remeasure_b1157/`), so the instrument and dev-u24's cv2 are not the cause. Then
  (`review/B1164/worker/dev5/fix/attrib/E*`, the data trees `src_<sha>` = `git archive <sha> config data`):
  E1 = B1157's binary on i915 reproduces B1157 on all 7 rows tried; E2 = the same binary on NVIDIA
  reproduces dev5's base (r0 0.370 / 4.667, r1 3.400, r11 0.064 / 1.600, r29 nan, r33 5.017);
  E4 = 4275bc51 on NVIDIA reproduces dev5's base (the control); **E3 = 4275bc51 on i915, all 40
  rows, equals B1157 on 39** (`base_i915` column; mv_code empty) — the 40th is r35 (spell 253,
  1.791 → 1.206 land, 0.953 → 0.892 src), B1161's retirement of 253's override.
- So every b1130 drift the review listed is the renderer: r29's landing reading (0.095 / 7.267 on
  i915, nan on NVIDIA), r11 2.000 → 1.600 and 0.069 → 0.064, r1 3.533 → 3.400, r33 5.117 → 5.017,
  r0 0.358 → 0.370 and the −0.033 s ends: `mv_gpu` flags exactly r0 (land), r1 (end), r4 / r5 / r8 / r14
  (src), r11 and r29; no code between B1157 and 4275bc51 moves a b1130 row but r35. b1118: base equals B1157 on
  every row but the two 253 rows (B1161), both NVIDIA.
- **Owner:** the gate's renderer choice (B1157's b1130 sweep on i915 depth 16) — not a defect of
  any card; the tables now compare like with like (NVIDIA for base / before / after).

**Base → before (the merged cards; mv_tip).** b1118: none. b1130: 21 `fixed_npc` rows (e.g. r3
6.601 → 5.106, r7 end 4.433 → 6.100, r30 2.121 → 1.329). Rendered at B1166's merge 10ffe690 (E5)
and at B1169's 070a9215 (E6), measured with 4275bc51's script: **E5 equals f61fafb4 on all 21 rows**
(and E6 on the 8 rendered there), and f61fafb4's frames read the same under both measure-script
versions. Owner: **B1166** (#485, the harness's b1130 target anchor, `tools/model_viewer/fx_target_anchor.h`);
B1169, B1165, B1168 and B1173 move no gate row.

### 10.6 FOLLOW-UP

1. **Target model scale** — owned and implemented by B1172 (#491, `task/kb-b1172-model-height`),
   merged into this branch at a7798e47 (merge b00438a5): `actor_scale.h` is B1172's file (with
   `clientResolvedActorSize`), `zone_dispatch.cpp`'s `OnZoneEntry` keeps ONE Spawn_Struct size
   read (B1172's, resolved to the race default for 0, into the entity record and PlayerState), and
   `test_local_player_spawn_size` expects the resolved size (dark elf 5.0, s 1.0). The one
   remaining textual conflict with B1172's branch was the `unified_particle.h:333` comment, resolved
   here. The re-gate of 10.5 predates that merge.
2. **The local player's size** — fixed in this leg (10.2) and unified with B1172's resolution.
3. **The remaining per-target-height gap** (owner: FX-sim sprite lane): 10.4's next candidate.
4. **HideChatWin does not hide the chat window** (owner: oracle rig): leg 1's `# ini-set=
   Defaults.HideChatWin=TRUE` left Main Chat drawn; a leg that needs the area under it must move
   the window (the UI_<char>_<server>.ini, which the driver's `char-ini-set` does not reach).
5. **0x923c30 / 0x923798** (owner: zone entry): `SpellFxAttachScale` passes the lane-5 value 0 for
   both flags; fable does not read NewZone_Struct offset 592.
6. **The gate's renderer** (owner: FX gate / B1157's sweep script): B1157's b1130 table was
   rendered on i915 depth 16, every later gate on NVIDIA depth 24 (10.5); a re-baseline of B1157's
   b1130 columns on NVIDIA (E2 above for 7 rows) would make the committed table like-for-like.

### 10.7 Acceptance of this leg

| item | status |
|---|---|
| s pinned at runtime | **met** — lane-5 gdb read: 1.200000 on the size-6 human target, 1.000000 on the size-5 DarkF target, 1.000000 on the size-0 caster (10.1); the client's code gives the same |
| s implemented on the EDD path, every race, unit test | **met** — 10.2: the whole rule (0x47a147 / 0x47a0e9 / 0x47a239, no cut-off) in the shared `actor_scale.h`; `test_spell_fx_attach_scale` 6/6 table-driven, `test_local_player_spawn_size` 4/4 |
| local player's own s from the wire size | **met** — 10.2, merged with B1172's resolution |
| merge with gitlab/fable | **met** — f61fafb4 merged (e8b21d56), B1172 a7798e47 merged (b00438a5) |
| tour-7 + both sweeps before/after on the merged tree, owner per moved row, B1157 drifts attributed | **met** — 10.5: 32 + 23 moved rows owned by s; B1157 → base = the renderer (39/40 b1130 rows) + B1161 (r35, both 253 b1118 rows); base → f61fafb4 = B1166 (21 b1130 rows); the gate binaries predate the B1172 merge |
| 93 like-for-like ratio | moves 0.82× → 1.06× (r51), 0.76× → 0.98× (leg 1) per willeq's drawn target (10.4) |
| unoccluded oracle reference, which model is off | **met** — the TARGET (client 0.2 × 6 = 1.2); B1172 owns it (10.3) |
| card acceptance B | **NOT met** — at the client's target scale the landing is still ~0.72–0.77× per target height² (10.4); the MR says Refs #483 |
