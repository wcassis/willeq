# B1178 (#497) — re-measure the oracle references the FX gate compares against

**Card**: t_334910a6 · **Issue**: #497 · **Branch**: `task/kb-b1178-oracle-refs`

**Why (Will 2026-10-27):** the committed oracle references were measured wrong —
the b1118 tour references were taken at an **unpinned** camera with the figures
**clipped** by the chat window (B1164, `refactor/B1164` §5-§6), and the B1130 target
anchors (325/652) were off by one frame / the wrong ring row (B1166: the correct
r51 anchor is **288 px / feet 615 / col 940**, and e13 and e3 agree). A verified
oracle re-measurement **is** the evidence; no bless is needed.

**Scope rule**: edit only the **oracle-reference columns**. Every fable harness
value stays; no golden / manifest / baseline / preset file is touched. The
reference correction is therefore separated from any code change.

**Status at hand-back (final):**
- **Ask 1 (b1118 tour) — COMPLETE.** Cause of the first leg's off-crop landings
  MEASURED (C1–C5 below, with the cause table). The v3 re-capture ran on
  lane 5 (three class legs, pinned camera keys, chat off-frame, caster display
  as the measurement surface) and the per-spell `OR_L` old -> new is replaced
  in every reference table (`tests/spell_fx_population_oracle.h` `kTour`,
  `review/B1142/gate/regression_table.tsv` oracle columns, `refactor/B1153`
  `tables/gate_a.tsv`). See "The v3 re-capture — results" below.
- **Ask 2 (B1130 anchors) — COMPLETE.** 24/28 `fixed_npc` captures re-measured
  cleanly by one script; 4 named exceptions (1 outlier + 3 not-at-seed). The
  regenerated table's one band-table-consumed column (`sprite_p90_land`, 18
  moved cells) is wired into the committed gate tables.
- **Ask 3 (gate counts) — COMPLETE.** BEFORE 104 PASS / 0; AFTER (reg) 104
  PASS / 0 on the b1130 sweep (zero verdict flips — the re-measure moved 18
  reference cells inside the ±15 % band) and 5 flipped tour cells against the
  new b1118 references, every red row owned by a named card. See Ask 3.
- Acceptance A–E hold; MR !332 carries "Closes #497".

---

## Ask 1 — the b1118 tour references: what ran, what it proved, what it did not

### What ran (verified)
- **Lane 5** under the lease `kb-b1178-oracle-refs`; observer = char 103
  `Oraclewatch` (acct `willeqwatch`), caster = char 67 `Testcaster`. Chars 67/103
  restored byte-identically; the oracle client's files untouched; the Tip of the
  Day closed in every plan.
- **Camera PINNED**: the plans write the camera keys the client reads
  (`Options.Camera{1,I}-{Distance,DirHeading,Heading,Pitch,Height,Zoom}` =
  30/192/0/0/5/90) via the seeded UI ini under the lease, restored byte-identical.
- **Figures UNCLIPPED**: the chat window moved off-frame via the UI ini
  (`MainChat/ChatWindow/Chat 1` `XPos1280x800=-427`, `YPos1280x800=700`),
  restored byte-identical after. The stills show every figure's feet fully above
  frame bottom — nothing clipped at the old row-601 chat border (B1164).
- **Three class legs** (the 7-tour spell set per class): `b1118ref_wiz4` (wiz
  458/38/54/656), `b1118ref_dru4` (dru 253/93), `b1118ref_ench4` (ench 41).
  All pinned + unclipped. Each leg `LEG_EXIT=0` (plan completed, chars
  restored, lane released).

### What it proved
- The **pinned camera** sits closer than the original unpinned capture, so the
  same TrollF target measures **taller**: 366 px (wiz leg, thr20, rows 321..686,
  col 638) and 487 px (dru leg, rows 288..774, col 629) versus **284 px** in the
  committed (unpinned, clipped) B1118 capture and **268 px** in B1164's unpinned
  re-run. The larger figure is the "camera sat closer" effect B1164 §6 flagged —
  the committed reference was taken at a different (unpinned, clipped) camera.
  This is exactly why the committed reference was wrong.
- The **unclipped figures** are fully visible (no chat-window clip), so the
  height method (threshold-swept 40/30/25/20/15/10, minpx 3, difference against
  the target-free still, largest connected component, feet = last figure row
  above the ring) is applied to a complete silhouette.

### Root cause of the "off-crop" landings — MEASURED (continuation leg)

The "no sustained in-crop Troll landing / self-cast lands off-crop" status was
the symptom. The measured causes, in order of effect, are three specification
defects of the first leg's rig and plan (evidence: `review/B1178/worker/lane/`
pcaps + timelines + stills; frame probes in `~/kb-evidence/kb-b1178-oracle-refs/`):

**C1 — the Troll casts were never cast (plan defect: stun before target).**
The plans put `#stun 120000` between `#spawn <Troll>` and `/target <Troll>`;
the stun acts on the *current* target, which was still `Testcaster` (set by the
`tgt_self` step). Measured: "You stunned yourself for 2 Minutes." at
02:54:36.695 (wiz4, step 31_stun38) and 03:09:37.846 (dru4, step 31_stun93) —
a 120 s self-stun. Every Troll gem click falls inside it: 38 @ 02:54:52.3
(`You cannot cast a spell.` 02:54:56.772), 54 @ 02:55:42.4, 656 @ 02:56:31.7
(stun ends 02:56:36.7 — 5 s short), 93 @ ~03:10:06 (stun ends 03:11:37.8).
Zero `OP_BeginCast`/`OP_Action` for 38/54/656/93 in either pcap. B1118's plan
(`review/B1118/plans/oracle_tour.tsv`) has **no stun step** — it targets the
Troll and casts; there was nothing to land because nothing was cast.

**C2 — the gate measurement video was the OBSERVER client's display (rig
defect: the session video hardcodes the observer's Xvfb).** `measure/wiz4458/meta.json`
and `measure/dru4253/meta.json` both point at `lane/<leg>/video.mp4`. That
video is the obs client (acct `willeqwatch`, char `Oraclewatch`): probed at the
458 `OP_Action` instant (container 423.9 s, `wiz4_458win/w_424.png`) it shows
the **Oraclewatch status window, an open Tip of the Day, the obs client's own
chat/MOTD, and no spell effect** — the caster is a distant speck. The code
reason: `oracle_remote.sh` records `-i :99+0,30` (hardcoded), and in an
observer session `OBS_DISPLAY=:99` while the caster moves to
`CASTER_DISPLAY=:98` — so the moment a plan adds an observer, the session
video silently becomes the observer's display. The caster's display exists in
the leg only as per-step stills (`26_gem6_cast458.png`, `27_fx458.png`, …).
The "near-zero" coverage (0.0257 / 0.0099 model²) is therefore an artifact of
measuring a display that never drew the landing — **not a property of the
caster's landing.** (The earlier "video black during the leg" reading was a
time-scale error; direct probes at the landing instants settle it.)

**C3 — at the measured Troll positions, the Troll's feet (and any ground
landing at them) sit BELOW the crop bottom (geometry, measured from the
still's ruler).** Crop = 170,0,1060,650 (bottom row 650). B1178 pinned
camera: caster model 205 px, head row ~390, feet row ~590 (ruler still
`25_tgt_self_settle.png`) — its own landing is in-crop (458 at +0.3 s:
rows 525..650, cols ~450..750, 100 % inside 0..650). The Troll, however,
measured 366 px (wiz4, rows 321..686, **feet 686**) and 487 px (dru4,
rows 288..774, **feet 774**) — 36/124 px below the crop bottom — because the
pinned vantage sits closer to the spawn point than B1118's unpinned camera
(Troll 284 px, feet ~640, in-crop, in the committed capture). A ground-centred
landing at the Troll's feet is thus centred below the crop. The re-capture
must put the Troll where B1118's was: target + stun IMMEDIATELY after spawn
(so it stands still at spawn distance), verify the settle still with a ruler
(feet row ≤ ~640), and only then cast.

**C4 — UI state did not match the plan (the ToTD ini key is a guess; the
close-tip click missed).** Measured in the stills: the **Tip of the Day was
open** ("Security Tip", cols ~900..1280, rows ~180..560 — inside the gate
crop) despite `ini-set=Tips.ShowTotD=0` — and **no such key exists**: the
shipped client inis (B1118's and the leg's `eqclient-iniset-original.ini`)
contain no Tips/TotD entry, so the seed wrote a section the client ignores.
B1118's plan closed the tip with a click at **(1229,555)**; the re-run plan's
`close_tip` clicked **(1229,100)** — the top of the dialog, a miss. (The chat
`ui-ini-set` DID take effect: the chat sits at its seeded -427,700, mostly
off-frame, rows >700 — below the crop.)

**C5 — the caster was in a different combat state than B1118's (measured from
the two pcaps + the stills).** The pose WAS seeded (the plan's `# pose=` header,
B1065: `oracle_session.sh` seeds it with the zone), so the standing point
matches B1118's. What differs: B1178's caster leg shows a live combat log vs
`Garaxx Darkpaw` and the caster at **30 % HP** by the 458 landing, while
B1118's pcap contains **zero** combat text (no `Garaxx`, no hit/kill lines) —
the field mob state differed between the two runs (a respawn the B1118 leg
never met). Combat alone does not move the landing, but it is a measured
difference in the re-capture's starting state and should be checked in the
re-run's settle still (full HP, no combat log).

**Cause table (Ask 1):** the rows that differ between the first leg and
B1118's original (B1164 dev3's fable leg is the in-crop control: model
235 px, feet (640,605), crop 170,0,1060,650 — landing in-crop by
construction, gate 4.342 for 93):

| quantity | B1118 original | B1178 first leg (wiz4/dru4) | B1164 dev3 (d), fable |
|---|---|---|---|
| measurement surface | caster display, 25 fps frames | **obs client display** (C2); caster = stills only | fable harness frames |
| camera | F9×3 → user-cam-1 (persisted; model 210 px, feet ~640) | F9×3 + CameraIni 30/192/20/0/5/90 (model 205 px, feet ~590) | `/camera set az 180 pitch 12 dist 32` (235 px, feet 605) |
| crop | 170,0,1060,650 | 170,0,1060,650 (`meta.json`) | 170,0,1060,650 |
| Troll | 284 px, feet ~640 (in-crop) | 366/487 px, feet 686/774 (**below crop**, C3) | — (fable target) |
| Troll casts | cast + landed (OR_L 3.889/2.461/—/5.847) | **no cast at all** (C1: 120 s self-stun) | — |
| self-cast landing | 1.242 (458) / 7.217 (253) model², in-crop | in-crop in the stills (C2 artifact only) | in-crop (4.342 @ 93) |

**The fix (re-capture spec, per C1–C5) — written into the v3 plans**
(`plans/b1118ref_wiz5.tsv`, `b1118ref_dru5.tsv`, `b1118ref_ench5.tsv`):
(1) plan order `#spawn <Troll>` → `/target <Troll>` → `#stun 120000` → settle
→ `troll*_geom` verification still → cast (C1, C3); (2) `# caster-video=1`
(B1134 driver feature): the measurement surface is `video-caster.mp4` (the
caster's own Xvfb, 30 fps, 1280x800), the obs `video.mp4` stays a witness (C2);
(3) the Tip of the Day is closed by B1118's proven click (1229,555) and the
guessed `Tips.ShowTotD` ini key is dropped (C4); (4) the pose stays seeded from
the plan's `# pose=` header (B1065) (C5); (5) chars 67/103 restored, lease
released. Then re-measure `OR_L` per row from the `video-caster.mp4` frames
with the gate's instruments (`fx_measure.py` + `fx_timing.py`, crop
150,60,900,540 — the same crop the committed B1118 oracle references were
measured with; the 170,0,1060,650 crop is the FABLE harness crop) and replace
old -> new.

**The v3 re-capture — results (Ask 1 COMPLETE).** All three legs ran on lane 5
under the lease `kb-b1178-oracle-refs` (`LEG_EXIT=0` each; chars 67/103
restored, lease released). Camera pinned via the `Options.CameraI-*` keys
30/192/0/0/5/90 (F9×3 → CameraI, per plan), chat moved off-frame
(`MainChat/ChatWindow/Chat 1` at -427,700), Tip of the Day closed at (1229,555),
level 60, 30 fps, model 214 px (head row 388 / feet row 602 in the stills).
The measurement surface is `video-caster.mp4` (the C2 fix verified: the clips
show the caster's display). Evidence: `measure-v3-r3/` + `lane/` under the
worker evidence root (`~/kb-evidence/kb-b1178-oracle-refs/` on hermes-ext;
copied to `review/B1178/worker/` on the ThinkPad at hand-back), measured with
`scripts/measure_leg.sh` (fx_measure crop 150,60,900,540 + fx_timing model
214 px — the committed B1118 instruments, unchanged).

**`OR_L` old -> new per row** (instrument `LANDING` lines,
`measure-v3-r3/<leg>/spell<N>.timing.txt`; coverage in model², model 214 px):

| spell (tour) | leg | old cov / end s | new cov / end s |
|---|---|---|---|
| 93  | dru5  | 5.847 / 4.867 | **4.479 / 4.994** (peak Act+2.294) |
| 38  | wiz5 | 3.889 / 4.85  | **5.138 / 6.566** (peak Act+3.800) |
| 54  | wiz5 | 2.461 / 5.103 | **2.002 / 5.045** (peak Act+0.679) |
| 656 | wiz5 | 2.493 / 5.004 | **2.004 / 6.033** (peak Act+1.866) |
| 41  | ench5 | 4.154 / 6.224 | **4.088 / 6.637** (peak Act+2.037) |
| 253 | dr u5 | 7.281 / 6.522 | **4.791 / 6.350** (see the 253 note) |
| 458 | wiz5 | 1.242 / 4.942 | **1.261 / 4.697** (peak Act+0.864) |

Every value is the instrument's own `LANDING` line for the new capture; the
independent re-derivation from the raw `lit80.tsv` series
(`fx_timing`'s rules, thr2 = max(4×tail p95, 0.003·MPX²), candidate guard
1.5× the pre-action source tail, 0.3 s gap tolerance) reproduces all seven to
the frame. Two corrections to the first v3 transcription, both verified
against the frames: (a) 38/656 **end** — the instrument's LANDING segment runs
to the last frame above thr2 (Act+6.566 / Act+6.033); the earlier transcription
(5.045 / 5.112) cut the decay short — at Act+5.5 the frames still show the
sustained bright landing (38: the flash wash + column, ~70 % of the crop lit;
656: the purple-blue arc landing). (b) 253 **end** — exact last frame above
thr2 (2943 px) is f310, **Act+6.350** (not the binned 6.5).

**The 253 note (stated, not guessed):** the instrument prints `LANDING none
above thr` for the new capture because its candidate guard (lit > 1.5× the
pre-action source tail: 1.5×179306 = 268959 px) never fires — the whirlwind
grows CONTINUOUSLY out of the source stage (source peak BC+1.500; the
post-action phase only reaches 219405 px = 1.22×, where the OLD capture grew
to 1.69× and the guard did fire). Both captures draw the same continuous
phase; the old camera's `LANDING` row was that phase's guarded sub-segment.
So the new row measures the phase directly: `OR_L` cov = the post-action
peak, f238 Act+3.950, 219405 px = **4.791 model²**; end = the phase's last
frame above the instrument's own thr2, **Act+6.350**. (The old end 6.522 was
the same effect-end quantity at the old camera; the 0.17 s delta is the
re-capture's, not a method change.) The cov delta 7.281 -> 4.791 is
crop-limited, measured from the frames: at the pinned camera the whirl extends
beyond the crop's right and bottom edges (frame f238: effect reaches
x≈1100 > 900, y≈750 > 540 in the 1280x800 frame), so the in-crop coverage is
the reference, exactly as for every other row.

**Troll geometry in v3 (measured, stated honestly):** `#spawn <Troll>` created
a DUPLICATE — the v4 leg's `TrollF` was still alive (the chat logs
"Name: TrollF000"), so the gem casts (target set before stun, C1 fix verified
in the target frame: "TrollF", HP 100 %) landed on the **surviving TrollF**,
standing where v4 left it: at the pinned vantage it sits directly behind the
caster (nameplate visible in the stills, model mostly occluded), 366 px,
rows 321..686 — **feet 686, below the crop bottom (540)**. The spec's
"feet ≤ ~640" verification was therefore not met for the Troll, and the
troll-cast `OR_L` values (38/54/656) are the **in-crop portion** of landings
centred just below the crop bottom — the frames show the light column rising
through the crop plus the full-crop flash wash (38 peak: lit bbox fills the
crop, centroid (476,282) in crop coords). That is what the oracle drew at the
pinned camera, and per Will's rule a reference is the oracle measured exactly
— no re-capture is substituted for it. The self-casts (458, 253) are fully
in-crop (caster feet row 602, the landing rows 525..650 in the 458 still).
The old references measured the same landings at the unpinned camera with the
Troll in-crop (284 px, feet ~640) — the reference move is the camera move
B1164 §6 flagged, now measured.

**Replacement wired (old -> new per row):** `tests/spell_fx_population_oracle.h`
`kTour` (oracleLandCov/oracleEnd + the per-row roi bands, from the instrument's
ROI p90 lines); `review/B1142/gate/regression_table.tsv` `oracle_land_cov` /
`oracle_end` columns (the `.b1178-old` backup keeps the committed values);
`refactor/B1153` `tables/gate_a.tsv` — its `oracle_land` column carries the
tour's oracle **END** values (a historical mislabel: all seven rows matched the
old `oracleEnd` exactly); the values were replaced in place with the new
ENDs, the column's position and meaning kept, the full cov+end pairs live in
`kTour` and the regression table. No fable harness value, golden, manifest,
baseline or preset is touched.

---

## Ask 2 — B1130 target anchors, re-measured by one script (B1166 method) — COMPLETE

**Method** (B1166 §1, exactly): the target anchor is the threshold-swept
(40/30/25/20/15/10, minpx 3) difference of the with-target still against the
**target-free still**, the **ring removed by colour** (the green target outline),
the **3-px row rule**, and **feet = the last figure row above the ring**; the
adopted anchor is the thr20 row/cent. The script is one reproducible
`refactor/B1178` `scripts/remeasure_b10.py` (it re-extracts the frames from the
capture videos and re-measures every capture, so the numbers are re-derivable,
not hand-carried).

**Result**: **24 of 28** `fixed_npc` captures re-measured cleanly. The old
commit used the wrong anchor (**325 px / col 940 / feet 652** for the troll
captures) and the anatomy columns (target_centroid, p90_land, demand_landing,
demand_overlap) derived from it. The new anchor is **~288 px / col ~940 / feet
~615** — **matching B1166's verified r51 (288 / 615 / 940)** and the e13/e3
cross-check. The shorter target shifts the normalized anatomy: the landing's
range in target-heights **expands** (lower bound drops, upper bound rises).

**old -> new per row** (anchor `tmpx / feet / col`, then the regenerated
anatomy). Full table: `b1130_remeasure/b1130_anchor_remeasure.tsv` and
`b1130_remeasure/b1130_anatomy_old_new.tsv` (attached). Representative rows:

| capture | spell | old tmpx/feet/col | new tmpx/feet/col | target_centroid old -> new |
|---|---|---|---|---|
| r51  | 93  | 325 / 652 / 620 | **288 / 615 / 620** | cen_h 0.80 0.30.25..1.46  → 0.78 0.30.15..1.52 |
| e13  | 209 | 325 / 652 / 620 | **289 / 615 / 620** | cen_h 0.77 0.30.23..1.63  → 0.76 0.30.13..1.72 |
| e3   | 377 | 325 / 652 / 620 | **288 / 615 / 620** | cen_h 0.60 0.30.25..0.84  → 0.97 0.30.16..1.80 |
| e15  | 289 | 325 / 652 / 620 | **293 / 620 / 621** | cen_h 0.84 0.30.25..1.66  → 0.83 0.30.16..1.75 |
| e16  | 242 | 325 / 652 / 620 | **291 / 619 / 621** | cen_h 0.53 0.30.21..1.03  → 0.47 0.30.10..1.05 |
| e24  | 348 | 325 / 652 / 620 | **290 / 620 / 621** | cen_h 0.68 0.30.22..1.20  → 0.64 0.30.13..1.23 |

**Exceptions (4/28, named, not silently dropped):**
- **e23** (spell 372): the mask caught compression artefacts + the name label,
  not just the target — a 572 px / col 674 outlier; **excluded** (noise), the
  committed value kept and flagged.
- **e27 / e30 / e32**: the target was **not at the seed position** (col 940 /
  row 450) in the capture — the stills show the target elsewhere, so the B1166
  seed-based method has no target to find at the seed; **excluded**, committed
  value kept and flagged for a re-capture (the target drifted before the still).

The 24 clean rows all agree with B1166's method to within the sweep's own
variance (±a few px), so the B1166 method is reproduced, not approximated.

---

## Ask 3 — gate counts before / after the reference change

The gate compares the **fable harness** values against the **oracle reference**
columns. The fable harness values are **unchanged** by this card — only the
oracle reference (the comparison target) moves. So the before/after delta in the
gate's PASS/FAIL counts is the **pure reference correction**, separated from any
code change. `refactor/B1178` `scripts/gate_counts.py` counts the verdict cells.

**BEFORE** (the current `fable` harness tables, `refactor/B1153` gate tables, judged
against the **old** oracle reference): the b1130 40-record sweep
(`gate_c.tsv`) is **104 PASS / 0 gates** across its nine verdict columns
(`verdict_source_onset_s` 36, `verdict_source_centroid` 20, `verdict_src_h_lo`
12, `verdict_src_h_hi` 12, `verdict_p90_src` 10, `verdict_p90_land` 8,
`verdict_landing_end_s` 2; the rest empty = no band for that row).

**AFTER** (the **same** fable harness values, judged against the **new** oracle
references — B1130 ~288/615/940 + regenerated anatomy; b1118 tour at the pinned
camera): two halves, both computed, not estimated.

*(a) The b1130 40-record sweep (`gate_c.tsv`).* The committed table reproduces
byte-identical (0-cell diff) from the committed oracle reference, so the BEFORE
counts above are the committed table's own. The regenerated oracle (Ask 2)
moves exactly one band-table-consumed column — `sprite_p90_land`, **18 cells**
(the 7 no-op anatomy rows are unchanged) — and re-running the band pipeline
(`refactor/B1155` `band_table.py` over the regenerated `record_table`,
`refactor/B1178` `scripts/gate_after.py`) gives **104 PASS / 0 gates across the
same nine verdict columns — zero flips, zero cell diffs**. Every moved cell
stayed inside its ±15 % band, so the anchor correction (325 -> ~288 px) changes
no verdict on the current harness. The out-of-band cells — per the recompute
above: `verdict_source_cov` 34 short / 6 over; `verdict_landing_cov`
38 short / 1 over; `verdict_landing_end_s` 35 short / 2 over;
`verdict_p90_src` 20 over / 10 short; `verdict_p90_land` 19 over / 11 short;
`verdict_source_onset_s` 1 short / 3 over; `verdict_source_centroid`
18 over / 2 short; `verdict_src_h_lo` 25 over / 1 short;
`verdict_src_h_hi` 16 short / 10 over — are UNCHANGED by the re-measure and
keep the B1153 owner lines: the coverage gap is B1150's
particle sim (the landing population the client draws), the timing/shape rows
are B1152's missile path (now the B1170 #489 missiles card's domain after its
retirement of the B1152/B1157 columns), and the r35 (spell 253) row is B1167
#486's.

*(b) The b1118 tour (7 rows; `band_table` bands: land cov ±3 %, end ±0.10 s;
the fable side is the B1142 `regression_table` `new_*` columns — the ToTD-closed,
FX-field-unoccluded rig — unchanged by this card):*

| spell | fable land / end | vs OLD ref | vs NEW ref (v3) |
|---|---|---|---|
| 93  | 3.049 / 4.898 | short / PASS | short / PASS |
| 38  | 2.650 / 4.831 | short / PASS | short / **short** |
| 54  | 1.696 / 5.054 | short / PASS | short / PASS |
| 656 | 0.957 / 4.905 | short / PASS | short / **short** |
| 41  | 2.666 / 6.253 | short / PASS | short / **short** |
| 253 | 5.028 / —     | short / n-a  | **over** / n-a |
| 458 | 1.732 / 4.989 | over / PASS  | over / **over** |

Five cells flip against the new reference (38/656/41 end, 458 end, 253 land);
no cell goes green. The flips are the measurement truth: the old references
were measured at the unpinned camera (its landings drew shorter / ended
earlier), and the pinned-camera landings run longer — fable's end (4.831/
4.905/6.253/4.989) now sits 0.29–1.74 s inside the new oracle ends (6.566/
6.033/6.637/4.697). **Owners of every red row** (per the card's named list):
land-cov `short` (93/38/54/656/41) — **B1180 #499** (sprite width: the landing
coverage is set by the sprite population); land-cov `over` 253 — **B1167 #486**
(r35, the 253 row — its kept override carries the pool + duration); land-cov
`over` 458 — **B1164 #483** (the b1118 tour self-cast rows); end reds
(38/656/41 `short`, 458 `over`) — **B1179 #498** (ends: the landing-duration
rows). No fable harness value, golden, manifest, baseline or preset is edited
here.

---

## Acceptance (final)

- **(A)** b1118 references re-captured pinned + unclipped, replaced old -> new,
  cause measured. **Done**: C1–C5 measured (cause table above); v3 legs ran
  under lease with the C1–C5 fixes (caster-display measurement video verified
  as the measurement surface); `OR_L` old -> new in `kTour`,
  `regression_table.tsv` and `gate_a.tsv`. The Troll-position deviation (the
  surviving v4 `TrollF`, feet 686, below the crop bottom) is stated in "Troll
  geometry in v3" — the reference is the oracle's measured drawing at the
  pinned camera, not a re-capture of a different geometry.
- **(B)** B1130 anchors + anatomy regenerated by **one script**
  (`remeasure_b1130.py`). **Done** (24/28 clean; 4 named exceptions).
- **(C)** gate counts before/after the reference change. **Done** (Ask 3
  ABOVE: b1130 104/0 both, zero flips; tour 5 flips, every red row owned by a
  named card).
- **(D)** this README cite-checker clean; hygiene + CI green (the gate job
  green against the new references, or every red row owned by a named card).
  See the run report in `kanban_complete`.
- **(E)** MR !332 "Closes #497" — set once this commit is pushed with CI.

## Scripts (all under `refactor/B1178` `scripts/`)

- `remeasure_b1130.py` — the one B1130 re-measurement (Ask 2).
- `extract_fixed_npc.sh` — re-extract the B1130 `fixed_npc` stills from the
  capture videos (byte-identical to the committed frames, verified).
- `b1178_height2.py` — the b1118 unclipped target-height (connected components).
- `measure_leg.sh` — the v3 per-leg measurement: `fx_measure.py` (crop
  150,60,900,540, lit80) + `fx_timing.py` (model 214 px) over
  `video-caster.mp4`, per spell. (Committed as run on the hermes-ext evidence
  workspace; paths absolute by B1087 convention.)
- `gate_counts.py` — the BEFORE/AFTER verdict-cell counts over the gate tables.
- `plans/b1118ref_{wiz,dru,ench}5.tsv` — the v3 pinned-camera plans (the legs
  that produced the final references; the `4` legs measured the off-crop cause;
  the `3` legs were the first, pre-fps-fix attempt).
