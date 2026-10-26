# B1159 (#478): the surviving-observer missile legs

Will, 2026-10-26, from the visual ledger: the M1_38 clip shows the observer killed
and zoning to bind. The spec is issue 478: re-capture the missile legs on an
observer that SURVIVES — 38 Lightning Bolt, 54 Frost Bolt and 93 Burst of Flame
x2 each — on lane 5; rows M1b_38 / M2b_54 / M3b_93 in
`refactor/B1152/missile_oracle.tsv` with frame indices; the original M1_38 row
stays as the death-on-landing row; the three ledger clips regenerated.

Facts from the record: char 103 Oraclewatch (acct willeqwatch) is a level-1
Human Warrior; the B1152 m1_rig38 pcap carries OP_Death on the OP_Action tick
(121 dmg); no other ledger capture has a target death in its window.

## The leg

ONE leg, `m_b1159_rig`, lane 5, plan `refactor/B1159/plans/m_b1159_rig.tsv`
(cut from the B1152 working template: same pose, F9 x3 camera, 36 s settle,
pcap of both clients, the official client closes Tip of the Day).

- **caster** = char 67 Testcaster (WIZ, level 60, B1152 gem set), at -74,428;
- **observer** = char 103 Oraclewatch at -67.6,434.4, **raised to level 60 for
  the leg** by `plans/pre_b1159_obs60_guardoff.sql` (B1134 provisioning
  pattern: `character_data.level = 60` + `PVPEnableGuardFactionAssist` off so
  the caster's attacks cannot be turned around); `plans/post_b1159_guardon.sql`
  restores the rule; the driver's `eqemu_snapshot`/restore returns char 103 to
  level 1 and everything else byte-identical.
- 38 x2 via `/cast 2` (gem), 54 x2 via `/cast 3` (gem); **93 x2 via
  `#castspell 93 0`** — WIZ cannot gem-cast 93 (`spells_us.txt` field 123 = 255
  for 93), so the GM command casts it server-side with the spell's own cast
  time. `/target Oraclewatch`, `#stun` after `/target`, no-comma `#goto`
  (the B1143-proven command forms).
- B1152 spacing: 1.5 s target-settle + 15.5 s fx settle per cast.

Checker (run BEFORE the leg, `scripts/eqemu/oracle/oracle_plan_check.py`):

```
$ python3 scripts/eqemu/oracle/oracle_plan_check.py check \
    refactor/B1159/plans/m_b1159_rig.tsv --class 12 --lane 5 \
    --memmed 0:93,1:38,2:54,3:41,4:253,5:458,6:656
OK line 57: /cast 2: spell 38 "Lightning Bolt" WIZ (class 12) level 16
OK line 62: /cast 2: spell 38 "Lightning Bolt" WIZ (class 12) level 16
OK line 67: /cast 3: spell 54 "Frost Bolt" WIZ (class 12) level 1
OK line 72: /cast 3: spell 54 "Frost Bolt" WIZ (class 12) level 1
rc=0, 0 problems
```

Lane state (`scripts/eqemu/env.sh`, EQEMU_LANE=5; lease taken for the leg,
released after): `lane_before.txt` and `lane_after.txt` (char 67/103,
accounts, memmed, buffs, skills, timers, rule_values) are **byte-identical
below the timestamp header** (`diff` exit 0). Char 103 back at level 1.

## HARD RULE: the pcap census (decode first)

`scripts/pcap_census.py` over `oracle/m_b1159_rig/capture.pcap` (both client
streams; OP_Action keeps only the p[30]==0 tick, the 0x04 action-state twin
and the capture point's duplicate datagram are filtered out — see the script
header). Full output: `pcap_census.txt`. Observer spawn id 261, caster 260
(measured from the streams).

```
OP_BeginCast (observer stream):  caster 260, 38/54/93 x2 each, cast_ms 2500/1000/1500
OP_Action on the observer stream: 6 total, 6 measured (spell in [38,54,93], target = the observer)
  +484.777 spell 38 | +519.463 spell 38 | +552.455 spell 54
  +586.560 spell 54 | +621.413 spell 93 | +655.214 spell 93
OP_Death (whole leg, both streams): 30 -- every one a zone wanderer; 0 for the observer
window video [+482.177..+489.777]: spell 38 Act +484.777: 0 OP_Death in window, 0 of them the observer
window video [+516.863..+524.463]: spell 38 Act +519.463: 2 OP_Death in window, 0 of them the observer
window video [+549.855..+557.455]: spell 54 Act +552.455: 0 OP_Death in window, 0 of them the observer
window video [+583.960..+591.560]: spell 54 Act +586.560: 0 OP_Death in window, 0 of them the observer
window video [+618.813..+626.413]: spell 93 Act +621.413: 0 OP_Death in window, 0 of them the observer
window video [+652.614..+660.214]: spell 93 Act +655.214: 0 OP_Death in window, 0 of them the observer
HARD RULE: 6/6 measured OP_Action, 0 cast window(s) with an observer OP_Death -> PASS
```

(Damage lines, OP_FormattedMessage 9073: 38 → 119/118, 54 → 9/10, 93 → 3/3 —
the level-60 observer soaks them all.)

## The rows

`refactor/B1152/missile_oracle.tsv` — three rows added, same columns, `note`
column added; the original M1_38 row is KEPT and labelled
`death-on-landing (level-1 target)`. fps = 30 (ffprobe on
`oracle/m_b1159_rig/video.mp4`: 20404 frames); t_video/video.calibration.tsv;
frame = round(t_video x 30). Per-frame census: `census_frames.tsv` (20404
rows; box x660-760 y440-620, >=180 grayscale, B1152 TRACE sec. 8), runs:

```
== 38a BC 482.278 Act 484.777   RUN 14509-14548 (Act -1.144..+0.156, 45..280 px)   RUN 14561-14734 (Act +0.590..+6.356, 231..16100 px)
== 38b BC 516.951 Act 519.463   RUN 15541-15549 (Act -1.430..-1.163, 42..78 px)   RUN 15598-15777 (Act +0.470..+6.437, 48..16100 px)
== 54a BC 551.420 Act 552.455   RUN 16588-16593 (Act +0.478.., 194..262 px) + 20 more runs to Act+6.345 (frost burst)
== 54b BC 585.540 Act 586.560   RUN 17610-17614 (Act +0.440.., 178..252 px) + 23 more runs to Act+6.273 (frost burst)
== 93a BC 619.892 Act 621.413   RUN 18611-18622 (Act -1.046..-0.680, 37..63 px)   RUN 18656-18832 (Act +0.454..+6.320, 108..16099 px)
== 93b BC 653.703 Act 655.214   (no pre-Act run)                                     RUN 19671-19849 (Act +0.486..+6.419, 50..16091 px)
```

Strips: `refactor/B1152/strips/m1b_38_flight_483.4-485.8.png` (cells = frames
14502/14514/14526/14538/14550/14574, per-cell gap census 0/45/61/143/17/9595),
`m2b_54_cast_552.2-554.2.png` (16566/16578/16590/16602/16614/16626,
0/0/262/11/491/0), `m3b_93_flight_620.2-622.2.png` (18606/18618/18630/18642/
18654/18666, 0/56/0/0/0/7181) — `strip_regen.py` (the B1152 script).

## Landing on the surviving target (obs_measure, LANDING at the TARGET)

`scripts/obs_measure_b1159.py` (copy of `refactor/B1143/scripts/obs_measure_b1143.py`,
B1134 rig geometry) over the leg video, `--observer Oraclewatch`; output
`measure_b1159/m_b1159_rig_<spell>_<rep>_player.{txt,frames}`:

| cast | ABS onset | end | peak | coverage |
|---|---|---|---|---|
| 38a | f14553 Act+0.323 | f14746 Act+6.756 | f14605 Act+2.056 | 3.109 |
| 38b | f15588 Act+0.137 | f15786 Act+6.737 | f15644 Act+2.004 | 3.054 |
| 54a | f16576 Act+0.079 | f16770 Act+6.545 | f16654 Act+2.679 | 2.257 |
| 54b | f17597 Act+0.006 | f17794 Act+6.573 | f17653 Act+1.873 | 1.973 |
| 93a | f18652 Act+0.321 | f18844 Act+6.721 | f18707 Act+2.154 | 3.011 |
| 93b | f19666 Act+0.319 | f19859 Act+6.753 | f19718 Act+2.053 | 3.079 |

**Delta vs the death capture (B1152 M1_38, level-1 target):** the landing
END/coverage numbers above are the first measured at a target that stays
standing — in the death capture the observer dies on the OP_Action tick (121
dmg), so its "landing" is the corpse plus the death/zoning sequence, not a
burst on a standing model. Per-frame gap-census arrival onset: M1_38
+0.116 s (f14188) vs M1b_38 +0.590 s (f14561) — first-visible-burst-frame
variance between the two captures (the server tick is authoritative in both:
Act). Missile visibility window: M1_38 frames 14170-14179
(Act-0.484..-0.184) vs M1b_38 frames 14509-14548 (Act-1.144..+0.156). Landing
end is consistent at Act+6.5..+6.8 across all six B1159 casts. The ledger
clips make the difference direct: M1_38 (b1155 baseline) ends on the observer
dead and zoning; M1b_38 shows the observer standing through Act+7.5
(verified in `M1b_38_strip.png`).

Spell 93: the pre-Act flight run is faint in the observer's own video
(93a: max 63 px vs 38a's 280 px; 93b: not visible). B1152 sec. 9 already
established 93 shares record 80 with 38 and renders the identical flight —
the willeq-side m3_rig93 render below shows the flight and the flame pillar.

## Ledger clips (regenerated, the three b-legs only)

`refactor/B1158/scripts/ledger_inventory.py` now reads the b-rows (shared leg
video `m_b1159_rig`, willeq side = the ORIGINAL m1_rig38/m2_rig54 harness
runs — the willeq side does not depend on the observer's survival — plus the
NEW m3 harness for 93). Specs + clips in
`~/kb-evidence/kb-b1159-missile-recapture/VISUAL-FX/`:

| clip | oracle (left) | willeq (right) |
|---|---|---|
| `clips/M1b_38.mp4` (+`_strip.png`) | m_b1159_rig f14468 (BC) / f14543 (Act), t0 1793031035.588 | m1_rig38 harness f45 / f120 |
| `clips/M2b_54.mp4` (+`_strip.png`) | m_b1159_rig f16543 / f16574 | m2_rig54 harness f45 / f75 |
| `clips/M3b_93.mp4` (+`_strip.png`) | m_b1159_rig f18597 / f18642 | m3_rig93 harness f45 / f90 |

`specs/M1b_38.json`, `M2b_54.json`, `M3b_93.json` alongside. The orchestrator
publishes them to the ledger.

**m3_rig93** (the 93 willeq side, first ever): `scripts/plans/m3_rig93.plan`
(record 80, cast_ms 1500 — the pcap-measured OP_BeginCast value, NOT the 2500
plan value), rendered on the ThinkPad with
`model_viewer --spell-test --camera b1130 --frames-dir ... --fps 30
--seconds 17.5 --sim-hz 60 --seed 1 --headless` from the worktree root (the
EQE1 index path is relative — a foreign CWD dies at
`data/eqe1_texture_index.json`). 525 frames; `timing.tsv`: BC frame 45
(t=1.500), Act frame 90 (t=3.000). Run copied to
`~/kb-evidence/kb-b1159-missile-recapture/measure/m3_rig93/`. Flame pillar
verified at the target in the impact cells (`M3b_93_strip.png`).

## Evidence

`~/kb-evidence/kb-b1159-missile-recapture/`: `oracle/m_b1159_rig/`
(video.mp4, capture.pcap, video.calibration.tsv, all-opcodes.tsv, settle
stills), `pcap_census.txt`, `lane_before.txt` / `lane_after.txt`,
`census_frames.tsv`, `measure_b1159/`, `measure/m3_rig93/`,
`VISUAL-FX/{clips,specs}/`, `spells_us.txt` (fetched from the lane for the
class-check of `#castspell`).

## FOLLOW-UP

- The b1155-baseline ledger's `M1_38` clip (death-on-landing) and the three
  b-legs should both stay in the ledger: they are the two faces of record 80.
  When the orchestrator publishes, the baseline index keeps M1_38 as-is.
- 93's flight is only faintly visible in the observer's own video (max 63 px
  of the 18k gap box; absent in rep 2). If a future leg wants the 93 flight
  measured crisply, it needs a side camera on the gap (the B1130 fixed camera
  does that in the gate runs) — the harness m3_rig93 render carries it.
- `refactor/B1152/strips/` now holds five strips (two original + three b-legs);
  the `.gitignore` negation for the directory already exists.
