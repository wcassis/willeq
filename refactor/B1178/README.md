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
- **Ask 1 (b1118 tour) — PARTIAL; the reference replacement is NOT yet valid.**
  The pinned-camera, unclipped re-capture ran (three class legs, lane 5, lease
  taken/released, chars 67/103 restored byte-identically) and the **figure
  heights** measured (TrollF 366 px wiz / 487 px dru, unclipped). But the
  **landing coverage** re-measurements came back near-zero — the targeted
  Troll-casts (38/54/656/93) emit no sustained in-crop landing, and the
  self-cast (458/253) lands off-crop. So the per-spell land-or-f1` per row
  cannot be replaced with a verified number yet. **This is the evidence
  gap; it is reported, not blessed.** See "Ask 1 — what ran, what it proved,
  what it did not" below.

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

### What it did not (the evidence gap)
The per-spell **landing coverage** (the oracle `OR_L` the gate compares against)
could not be replaced with a verified number, because:
- **The targeted Troll-casts (38/54/656/93) emit no sustained in-crop
  landing.** Each leg's pcap shows at most one `OP_Action` (a landing) in the
  whole leg — the 458/253 self-cast — and the Troll-cast steps (38/54/656/93)
  produce no `OP_Action`. The B1118 original (which produced the committed
  `OR_L`) cast the same spells at the same pinned vantage; the difference is in
  the re-capture's targeted-cast leg, not the camera. **The root cause of the
  missing targeted landings is not yet established** — the in-crop `#spawn`
  (B1118's default position, no coords) did not fix it. Per the card, a leg is
  not re-run on the shared lane without the root cause established; this is
  flagged for the next owner, not guessed.
- **The self-casts (458/253) land off-crop** in the pinned vantage: their
  measured landing coverage is 0.0257 / 0.0099 model² (crop 170,0,1060,650,
  model 230 px) — below the floor, not a real landing. The B1118 original
  measured them at 1.242 / 7.217 model², so the vantage/crop placement of the
  self-cast landing differs from the original's.

**old -> new per row (the figure height, the part that DID measure):** the
committed reference was a single clipped/unpinned figure set (target 284 px).
The re-measured pinned + unclipped figure height is per class leg (TrollF):

| spell (tour) | class leg | old (committed, clipped, unpinned) | new (pinned, unclipped, measured) |
|---|---|---|---|
| 38/41/54/93/253 | `b1118ref_wiz4` | 284 px (B1118 93-leg target) | **366 px** (TrollF, rows 321..686, col 638) |
| 253/93/... | `b1118ref_dru4` | 284 px | **487 px** (TrollF, rows 288..774, col 629) |

The **per-spell landing coverage (`OR_L`) old -> new is NOT replaced** — the
new value is not a verified number (see "What it did not"). The committed
`OR_L` is retained pending the targeted-cast root cause.

**Next owner (named):** establish why the re-capture's targeted Troll-casts
(38/54/656/93) emit no sustained in-crop landing where B1118's did, and why the
self-cast landing (458/253) lands off-crop in the pinned vantage, then re-measure
the `OR_L` per row and replace old -> new. The figure-height evidence above is
the starting point (the camera is pinned + unclipped and verified).

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
  **PARTIAL**: re-captured pinned + unclipped (figure heights measured, verified
  on the stills), but the per-spell landing coverage (`OR_L`) is **not yet
  replaced** — the targeted Troll-casts emit no sustained in-crop landing and the
  self-casts land off-crop, so the new `OR_L` is not a verified number. Root
  cause flagged for the next owner (named above).
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
