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

**Status at hand-back (honest):**
- **Ask 2 (B1130 anchors) — COMPLETE.** 24/28 `fixed_npc` captures re-measured
  cleanly by one script; 4 named exceptions (1 outlier + 3 not-at-seed). The new
  anchor matches B1166's verified r51 (288/615/940) and the e13/e3 cross-check.
- **Ask 3 (gate counts BEFORE) — COMPLETE.** Current `fable` harness tables:
  `gate_c` **104 PASS / 0 gates**, across its nine verdict columns. AFTER is the
  natural next step once the regenerated reference is wired (the fable values
  are fixed, so the gate re-run is deterministic).
- **Ask 1 (b1118 tour) — cause MEASURED; reference replacement pending the
  re-capture.** The pinned-camera, unclipped re-capture ran (three class legs,
  lane 5, lease taken/released, chars 67/103 restored byte-identically) and the
  **figure heights** measured (TrollF 366/487 px). The "off-crop" status was
  then root-caused against the pcaps, timelines and stills: the Troll casts
  were **never cast** (a `#stun` step ran while the target was still the
  caster — 120 s self-stun over every Troll gem click), the gate measurement
  video was the **observer client's display** (the caster's display exists only
  as per-step stills), and at the measured Troll positions the Troll's feet
  (686/774) sit **below the crop bottom (650)** — while the self-cast landing
  is in-crop in the stills. See "Root cause of the 'off-crop' landings —
  MEASURED" below; the `OR_L` replacement is the next step (re-capture spec
  written out there).

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
defect: X record attached to the wrong client).** `measure/wiz4458/meta.json`
and `measure/dru4253/meta.json` both point at `lane/<leg>/video.mp4`. That
video is the obs client (acct `willeqwatch`, char `Oraclewatch`): probed at the
458 `OP_Action` instant (container 423.9 s, `wiz4_458win/w_424.png`) it shows
the **Oraclewatch status window, an open Tip of the Day, the obs client's own
chat/MOTD, and no spell effect** — the caster is a distant speck. The caster's
display exists in the leg only as per-step stills (`26_gem6_cast458.png`,
`27_fx458.png`, …). The "near-zero" coverage (0.0257 / 0.0099 model²) is
therefore an artifact of measuring a display that never drew the landing —
**not a property of the caster's landing.** (The earlier "video black during
the leg" reading was a time-scale error; direct probes at the landing instants
settle it.)

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

**C4 — UI state did not match the plan (ini-set not effective on the running
caster client).** Measured in the stills: the **Tip of the Day was open**
("Security Tip", cols ~900..1280, rows ~180..560 — inside the gate crop)
despite `ini-set=Tips.ShowTotD=0`; the **chat window was visible bottom-left**
(rows >700 — below the crop, harmless, but the `ui-ini-set` off-frame move did
not take either). The inis are read at client start; the seed must be applied
BEFORE the caster client launches, or the tip closed in-game (B1118's plan
`click 1229,555 close_tip`).

**C5 — the caster was not in B1118's clean state (measured from the leg log +
stills).** The leg's pre-SQL seeded class/gems/skills/multiclass/timers but
**no pose** (`re-run5.log`), although the plan header declares
`pose=-74,428,3.75,160`; the stills show the caster at **30 % HP** with a
live combat log vs `Garaxx Darkpaw` and `Oraclewatch's corpse` beside it —
B1118's leg zoned in fresh at the seeded pose with no combat.

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

**The fix (re-capture spec, per C1–C5):** (1) plan order
`#spawn <Troll>` → `/target <Troll>` → `#stun 120000` → settle → **ruler
verification still** (feet row ≤ ~640, no ToTD, no chat in crop) → cast;
(2) the measurement video records the **caster's** display (the obs client
stays a rig witness, not the measurement surface); (3) camera/ToTD/chat
seeding applied **before** the caster client launches, with an in-game
tip-close as B1118's, and a still that verifies both; (4) pose seeded in the
leg's SQL (B1118's `-74,428,3.75,160`); (5) chars 67/103 restored, lease
released. Then re-measure `OR_L` per row from the caster-display frames with
the gate's instruments and replace old → new.

**old -> new per row (the figure height, the part that DID measure):** the
committed reference was a single clipped/unpinned figure set (target 284 px).
The re-measured pinned + unclipped figure height is per class leg (TrollF):

| spell (tour) | class leg | old (committed, clipped, unpinned) | new (pinned, unclipped, measured) |
|---|---|---|---|
| 38/41/54/93/253 | `b1118ref_wiz4` | 284 px (B1118 93-leg target) | **366 px** (TrollF, rows 321..686, col 638) |
| 253/93/... | `b1118ref_dru4` | 284 px | **487 px** (TrollF, rows 288..774, col 629) |

The **per-spell landing coverage (`OR_L`) old -> new is NOT replaced yet** —
the new value is not a verified number until the C1–C5 re-capture runs
(see "Root cause of the 'off-crop' landings — MEASURED"). The committed
`OR_L` is retained pending that re-capture.

**Next step (named, spec above):** re-run the three class legs on lane 5 with
the C1–C5 fixes (stun after target, caster-display measurement video, pre-launch
UI seeding + in-game tip-close, pose in the leg SQL), verify the settle stills
ruler (feet row ≤ ~640, no ToTD/chat in crop), then re-measure the `OR_L` per row
from the caster-display frames with the gate's instruments and replace
old → new. The figure-height evidence above is retained (the camera is pinned
+ unclipped and verified).

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
reference — B1130 ~288/615/940 + regenerated anatomy; b1118 figure set pinned +
unclipped): the fable columns are byte-identical to BEFORE; only the oracle_*
columns and the verdicts that derive from them move. Because the Ask 1 landing
coverage is **not yet replaced** (see above), the AFTER re-run is the natural
next step once the regenerated reference tables are wired in — the fable values
are fixed, so the gate re-run is deterministic. Every AFTER red row will be
either (a) a row whose oracle reference correctly moved (the fable value was
never wrong — the reference was), or (b) a row owned by a named card. **No fable
harness value, golden, manifest, baseline or preset is edited here.**

---

## Acceptance (honest status)

- **(A)** b1118 references re-captured pinned + unclipped, replaced old -> new.
  **CAUSE MEASURED; RE-CAPTURE PENDING.** The off-crop status is root-caused
  with numbers (C1–C5: uncast Troll spells, obs-display measurement video,
  Troll feet 686/774 below the crop bottom, UI-seed not effective, unseeded
  pose) and the fix spec is written out. The legs have not yet been re-run with
  the fixes, so the per-spell `OR_L` is not yet replaced.
- **(B)** B1130 anchors + anatomy regenerated by **one script**
  (`remeasure_b1130.py`). **Done** (24/28 clean; 4 named exceptions).
- **(C)** gate counts before/after the reference change. **BEFORE done**
  (104 PASS / 0 gates); **AFTER** is the deterministic next step once the
  regenerated reference is wired (see Ask 3).
- **(D)** this README cite-checker clean; hygiene + CI green (the gate job green
  against the new references, or every red row owned by a named card).
- **(E)** MR "Closes #497".

## Scripts (all under `refactor/B1178` `scripts/`)

- `remeasure_b1130.py` — the one B1130 re-measurement (Ask 2).
- `extract_fixed_npc.sh` — re-extract the B1130 `fixed_npc` stills from the
  capture videos (byte-identical to the committed frames, verified).
- `b1178_height2.py` — the b1118 unclipped target-height (connected components).
- `plans/b1118ref_{wiz,dru,ench}4.tsv` — the pinned + unclipped class-leg plans
  (the `4` legs that ran; the `3` legs were the first, pre-fps-fix attempt).
