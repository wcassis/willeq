# B1180 (#499): the sprite half-width factors in CParticleSystem::Render

**The task**: trace the half-width factors to their sources and pin them live on lane 5
for spell 93's Target emitters, name the mis-applied factor with a number, implement the
client's rule with a unit test on the pinned values, and re-gate.

**Result**: every factor of both half-width branches is traced and pinned (ask 1, sec. 1).
The mis-applied factor was the attach scale `s` (Fable constant 1.0 vs the client's 1.2 —
0.833x linear on every scaling sprite); the client's rule is in the tree via B1164 (#483)
and is pinned by B1180's unit test on the pinned values (ask 2, sec. 2). The re-gate
(ask 3, sec. 3): the 93 tour-7 landing moves 1.993 -> 2.905 (oracle 5.847; like-for-like
0.34x -> 0.50x), and the b1130 landing_cov PASS count stays **0/40** (38 short, 2 over,
no row regressed) — the residual is the landing-window measurement domain, owned by the
next window-method unit (B1171's attribution).

**Correction to the run-1 record**: run 1's claim that spell 93's Target sprites are
non-scaling (EDD +0x06C = 0) is REFUTED by the full pin — all 383 pinned rows carry
+0x06C = 1 and +0x019C = 1, and the attach scale s = 1.2 is multiplied into their
half-width (sec. 1).

## 1. The factors (ask 1) — both branches, every factor pinned

EQGraphicsDX9 `CParticleSystem::Render` (FUN_100605b0) branches on the EDD +0x070
sprite-mode word (mode 0 billboard, 1 streak — review/B1141/edd_fields_from_code.tsv:8,
read at 0x100630a1):

```
100632f0  cmp edx, 1          ; edx = [edi+0x70], the mode word (0x100630a1)
100632f3  jne 0x100633b5      ; mode 0 (billboard) falls through to 0x100633b5
```

### Mode 1 (streak) — the card's region, 0x1006335a..0x1006336f

```
1006335a  fld  dword ptr [esp + 0xc8]   A: streak-length accumulator (width side)
10063361  fmul dword ptr [eax + 0x18]   view matrix element +0x18 ([esp+0x514])
10063364  fmul dword ptr [esp + 0x1c]   B
10063368  fmul dword ptr [esp + 0x30]   s: the attach scale
1006336c  fmul dword ptr [esi + 0x6c]   w: the sprite width
1006336f  fmul dword ptr [0x101c594c]   0.5 (DLL .rdata, read from the lane-5 image)
```

The rule the factors implement is the TSV's own: +0x0CC "on screen half-width =
w*s*D_16C*0.5/depth" (review/B1141/edd_fields_from_code.tsv:31) and +0x17C "billboard
half-height h*s*D16C*0.5/depth; streak length (mode 1)"
(review/B1141/edd_fields_from_code.tsv:69). Spell 93's Target emitters do NOT take this
branch: all 383 lane-5 rows pinned for them are mode 0, where A = [esp+0xc8] and
B = [esp+0x1c] are 0 and the product computes to 0 (the slots belong to the mode-1 pass;
their sources in the mode-1 setup are outside this unit's pinned spell).

### Mode 0 (billboard) — the branch spell 93 Target actually takes, 0x100633b5..0x100633ff

```
width side  (0x100633e4..0x100633ff):
100633e4  fld  [esp + 0xd8]    D8 (1.0 initial, 0x100631e8; fade-overwritten 0x100632e9)
100633eb  fmul [ecx + 0x18]    view matrix element +0x18
100633ee  fmul [esp + 0x18]    s * D_16C * fade (set at 0x1006325a..0x10063264)
100633f2  fmul [esp + 0x2c]    invD = 1/depth (set at 0x100631d0..0x100631f3)
100633f6  fmul [esi + 0x6c]    w: the sprite width
100633f9  fmul [0x101c594c]    0.5

height side (0x100633b5..0x100633da):
100633b5  fld  [esi + 0x70]    h: the sprite height
100633c0  fmul [ecx + 0x18]    view matrix element +0x18
100633cb  fmul [esp + 0xc4]    C4 (1.0 initial, 0x100631d6; fade-overwritten 0x100632c8)
100633d2  fmul [esp + 0x18]    s * D_16C * fade
100633d6  fmul [esp + 0x2c]    invD = 1/depth
100633da  fmul [0x101c594c]    0.5
```

= (w or h) · M[+0x18] · (D8 or C4) · (s · D_16C · fade) · (1/depth) · 0.5 — exactly the
TSV's "billboard half-height h*s*D16C*0.5/depth" (review/B1141/edd_fields_from_code.tsv:69).

### Factor sources, pinned for spell 93 Target (size-6 human anchor, lane 5)

| factor | source | pinned value | cite |
|---|---|---|---|
| `s` = [esp+0x30] | the attach scale = the anchor's rendered model scale (the client's `getRaceScale(race) * size/6` for the size-6 human) | **1.2** | review/B1141/edd_fields_from_code.tsv:7 (+0x06C, read at 0x100609e1); refactor/B1164/README.md sec. 4 (0x100609e9/0x100609fd); pin7 |
| `D_16C` = [edi+0x16C] | EDD sprite size multiplier; [esp+0x18] = [esp+0x30] · [edi+0x16C] at 0x1006325a..0x10063264 | 1.0 (the TSV census: "1.0 on all"; the pin's s18 = 1.2 = 1.2 · 1.0) | review/B1141/edd_fields_from_code.tsv:65 (R 0x1006325e); pin7 |
| fade | EDD +0x098 grow-in / +0x09C shrink-out windows multiply [esp+0x18] (0x10063268..0x100632aa) and set C4/D8 (0x100632c8/0x100632e9) | 1.0 (no fade at the pin instants: C4 = D8 = 1.0) | review/B1141/edd_fields_from_code.tsv:18,19; disasm 0x10063268..0x100632aa |
| `invD` = [esp+0x2c] | [0x101c5948] / [esp+0xf4] = 1/depth; 0x101c5948 read from the lane-5 DLL image = **1.0**; the depth carries the EDD +0x0D0 bias (subtracted at 0x10063069/0x10063198) | 0.136–0.422 (target distance) | disasm 0x100631d0..0x100631f3; review/B1141/edd_fields_from_code.tsv:32; DLL .rdata read |
| `w` = [esi+0x6C], `h` = [esi+0x70] | the particle's width/height from the EDD +0x0CC/+0x17C min..max ranges | 1.25 / 1.25 (IgniteTargetill) | review/B1141/edd_fields_from_code.tsv:31,69; pin7 |
| M[+0x18] | view matrix element +0x18, matrix at [esp+0x514] | 1038.654 | pin7 |
| 0.5 | data constant at 0x101c594c (DLL .rdata, read from the lane-5 image) | 0.5 | DLL .rdata read |

**Live pin** (the card's ask 1): read-only gdb at the byte-verified 0x100632f0
(`cmp edx,1` = 83 fa 01; the 0x1006335a bytes verified too), lane 5, spell 93's Target
emitters on the size-6 human target: **383 rows (SFofSeukor 359, IgniteTargetill 24),
all mode 0, all +0x06C = 1, all +0x019C = 1, s = 1.2, D_16C = 1.0, w = h = 1.25,
C4 = D8 = 1.0, invD 0.136–0.422**. The client therefore draws these sprites at
`1.25 · 1.2 · 1.0` = 1.5x the unit size. Evidence:
`/home/user/kb-evidence/kb-b1180-sprite-width/pin7/` (b1180_hw_pin.tsv); the tool is
`refactor/B1180/scripts/b1180_hw_gdb.py` (formula at
`refactor/B1180/scripts/b1180_hw_gdb.py:6`) + `refactor/B1180/scripts/b1180_hw_watch.sh`.

## 2. Fable compared, the fix, the test (ask 2)

Fable's path (`SpellsNewEddSprite::width/height`,
`include/client/spell/spells_new_edd_reader.h:176` and `:179`) implements the client's
rule itself — `width * multiplier * (scaleSizes ? s : 1.0f)`, gated on the +0x06C word
(`include/client/spell/spells_new_edd_reader.h:165`) — and B1164 (#483, merged into
`origin/fable` and into this branch) supplies `s` = the anchor's rendered scale through
`setEntityAttachScaleCallback` (`include/client/spell/spell_fx_attach_scale.h`, wired at
the entity build and in the harness), replacing the old constant 1.0.

Factor by factor, the only one Fable was mis-applying was **`s`: the constant 1.0 against
the client's 1.2 — 0.833x linear (0.694x area) on every scaling sprite**. With B1164's
callback in the tree, Fable applies the client's words the client's way (WILL'S RULE),
no tuned number.

**The test** — `tests/test_b1180_attach_scale.cpp` (registered in
`tests/cmake/spells.cmake`), on the pinned lane-5 values:
- the client width rule: `SpellsNewEddSprite::width(1.25, 0.0, 1.2)` = 1.5
  (w = 1.25, s = 1.2, `scaleSizes` set — the IgniteTargetill row of the pin);
- the application: `Presets::applySlotEmitter` with attachScale 1.2 scales
  `EmitterSize.scale` to 1.5; with 1.0 it stays 1.25 (the pre-fix defect).

Both pass in this run (targeted ctest: 69/69, sec. 4).

**The data trio** (kept per the operator): `config/race_models.json` race 1 (Human)
scale 1.0 -> 1.2, `data/willeq.db` rebuilt, `data/willeq.db.sha256` re-pinned in the same
commit. Since B1172 (#491, in this tree) that column gates invisibility only — the
rendered scale comes from the server-size/model-height route (B1172) and the attach
scale (B1164) — so the trio is inert for the render; it is kept as the record of the
pinned 1.2, not as the mechanism.

## 3. The re-gate (ask 3)

B1157's gate — tour-7 (7 rows), the b1118 40-record sweep, the b1130 40-record sweep —
rendered once, one "after" label, on this branch's tree: **post-B1169, level 60 (the
b1130 capture level is 60 for all 40 records, per `scripts/fx/capture_level.py`; each
row's harness level is checked against the capture's and a mismatch is refused), the
B1166 anchor, b1164-style surfaceless renderer for tour-7/b1118 and the i915 DRM path
for b1130 — the same renderer each table's own before column used.**

**State of the tree at render time: B1164's attach scale `s` is in the tree, and
B1172's target scale is in the tree** (both merged from `origin/fable`; this branch
merges them at its tip 80202c81f). B1177 (#496, the b1130 target-solve harness fix,
landed on `fable` at 09:02Z, after this branch's merge) is NOT in the render tree —
the card's re-merge triggers were B1172/B1164 only, and re-rendering all 87 rows for
it is out of this unit's scope. The runner is `refactor/B1180/scripts/run_gate_b1180.sh`
(frames render on the ThinkPad and stream back per row for the local measure — this
host has no NFS and no numpy/cv2 there); the tables build from the streamed measure
outputs with `refactor/B1180/scripts/build_tables.py` (b1130 through
`refactor/B1180/scripts/band_view.py`, which lays the gate's `b1130_r<rec>` measure
dirs under `band_table.py`'s `<kind>_r<rec>` names). All 87 rows (tour-7 7, b1118 40,
b1130 40) are measured; every b1118 row also carries its SOURCE/LANDING anchor fits
(`refactor/B1180/scripts/reanchor_b1118.sh`). Two incidents, both recovered without
substitution: the hermes-ext disk-full at ~11:00Z left three b1130 rows without plans
(measured rows unaffected); a transient unmount of the `/mnt/projects` NFS at ~14:05Z
dropped the last three b1130 rows (r28 measured from its already-streamed frames, r34
and r26 re-rendered; r34's first measure lost frames to the r34 stream and was
re-measured from a re-stream, 495/495 frames). Evidence:
`/home/user/kb-evidence/kb-b1180-sprite-width/gate/after/` (+ `gate/tables/`); the
per-row text outputs (anchor fits, `summary.tsv`, timings, logs) are there, while the
raw frame dirs (the temporal source) stay on the ThinkPad at
`/home/user/kb-frames/kb-b1180-sprite-width/gate/after/frames/` per the operator's
keep-it-small rule.

### tour-7 (b1118 camera, level 60, B1166 anchor)

```
spell  oracle_land_cov  b1171_before  b1164_fix  mine_land_cov  mine_end   mine_vs_oracle
93     5.847            1.993        2.719      2.905          +6.633     2.01x (mine 0.50x)
38     3.889            1.993        2.719      2.905          +6.633     1.34x
54     2.461            1.204        1.412      1.534          +6.333     1.60x
656    2.493            1.204        1.412      1.534          +6.333     1.63x
41     4.154            2.478        2.962      3.108          +5.700     1.34x
253    7.281            4.148        4.148      4.148          -          1.76x
458    1.242            0.500        0.678      0.715          +5.117     1.74x
```

(`mine_vs_oracle` = oracle_land_cov / mine_land_cov; full table with src_cov and
end columns: `gate/tables/tour7.tsv`.)

### b1118 40-record sweep (level 60 both sides, vs B1157's level-60 table)

`gate/tables/b1118.tsv`: mine vs B1157's land_cov/end/src_cov with d_land_cov /
d_end. **24 of 40 rows improved (d_land_cov > +0.10), 15 within +-0.10, 1 regressed:
r35 (spell 253, a self spell) -1.067 (5.297 -> 4.230)** — an unexplained move this unit
does not attribute (owner: the next unit to touch the self-landing window; B1164's `s`
enlarges self sprites, so the regression is not `s`). Every row's SOURCE/LANDING
anchor fit is in its measure dir (`anchor_SOURCE.tsv` / `anchor_LANDING.tsv`).

### b1130 40-record sweep (B1166 anchor, i915 DRM, capture levels — all 60)

`gate/tables/b1130.tsv`, banded per B1155's `band_table.py` (landing_cov +- 3 %,
end +- 0.10 s, p90 +- 15 %). **landing_cov verdicts: 38 short, 2 over (r31 3.835 vs
3.668; r40 0.876 vs 0.157), 0 PASS.** No row regressed: every short row is above
B1171's before column (e.g. r0 0.537 -> 3.051 vs oracle 3.945; r3 6.093 -> 8.178 vs
11.688; r12 0.801 -> 5.041 vs 5.788).

### Owners (the two headline numbers)

| number | before (B1171 tree: level 60, no `s`) | after (this tree) | oracle | owner |
|---|---|---|---|---|
| 93 like-for-like, tour-7 b1118 | 1.993 (0.34x) | **2.905 (0.50x)** | 5.847 | B1164's `s` (1.993 -> 2.719, their fix gate) + B1172's target scale (2.719 -> 2.905); residual 0.50x is the landing-window measurement domain (per-target-height² window, the oracle target's height), which this unit does not claim — the next window-method unit, per B1171's own attribution |
| b1130 landing_cov PASS | 0/40 | **0/40** (38 short / 2 over) | 40/40 | all 40 rows improved vs before; none within +- 3 % yet: the same window/sprite-size residual as 93 owns the 38 short rows, r31/r40 are over (owner: the window-method unit, which must band the harness window to the capture's own) |

The residual on 93 after `s` + the target scale is the landing window itself: B1171's
window method measures the landing in a target-height-squared window, and the oracle's
window is the oracle target's height — a measurement-domain difference this unit does
not claim (owner: the next window-method unit, per B1171's own attribution).

## 4. Acceptance status

- **(A)** factors traced + pinned — done (sec. 1; pin7, 383 rows, byte-verified
  breakpoints, read-only).
- **(B)** fix + test — done (sec. 2; the rule is B1164's in-tree callback, the test is
  B1180's, both cases pass).
- **(C)** re-gate tables with owners + the two headline numbers — done (sec. 3).
- **(D)** this README: cite-checker clean (run
  `python3 -I scripts/refactor/cite_check.py refactor/B1180/README.md --tree .
  --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis
  --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis` -> "checked 56
  cites, 0 problems, 6 address warnings" — the warnings are the two `.rdata` data words
  0x101c5948/0x101c594c, cited as data, not code), targeted ctest + hygiene green on
  this tip (rbuild), `tools/model_viewer/model_viewer_fx_harness.cpp` at 1,318 lines
  (under 1,500; B1172's split, no B1180 addition to it).
- **(E)** MR "Closes #499".
