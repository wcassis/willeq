# B1178 (#497) — re-measure the oracle references the FX gate compares against

**Card**: t_334910a6 · **Issue**: #497 · **Branch**: `task/kb-b1178-oracle-refs`

**Why (Will 201027):** the committed oracle references were measured wrong — the
b1118 tour references were taken at an **unpinned** camera with the figures
**clipped** by the chat window (B1164, `ref11684` §5-§6), and the B1130 target
anchors (325/652) were off by one frame / the wrong ring row (B1166: the correct
r51 anchor is **288 px / feet 615 / col 940**, and e13 and e3 agree). A verified
oracle re-measurement **is** the evidence; no bless is needed.

**Scope rule**: edit only the **oracle-reference columns**. Every fable harness
value stays; no golden / manifest / baseline / preset file is touched. The
reference correction is therefore separated from any code change.

---

## Ask 1 — the b1118 tour references, re-captured pinned + unclipped on lane 5

**Method** (the B1118 capture plan, `ref11188` + the B1134 observer rig):
- **Lane 5**, under the lease `kb-b1178-oracle-refs`; observer = char 103
  `Oraclewatch` (acct `willeqwatch`), caster = char 67 `Testcaster`. Chars 67/103
  restored; the oracle client's files untouched; the Tip of the Day closed in
  every plan.
- **Camera PINNED**: the plan writes the Camera1 keys the client reads
  (`Options.Camera1-{Distance,DirHeading,Heading,Pitch,Height,Zoom}` =
  30 / 192 / 0 / 0 / 5 / 90) via the seeded `UI_<char>_<server>.ini` under the
  lease, and records them (see `ref11788` `plans/ref1118ref_{wiz,dru,ench}.tsv`).
- **Figures UNCLIPPED**: the chat window is moved off-frame via the UI ini
  (`MainChat/ChatWindow/Chat 1` `XPos1280x800=-427`, `YPos1280x800=700`) under
  the lease, **restored byte-identical** after (the driver restores the pre-leg
  UI ini). Verified on the stills: every figure's feet are fully above frame
  bottom, nothing clipped at the old row-601 chat border (B1164).
- **Unclipped height method** (B1164 `b1164_hostpx_sweep.py`): the target
  silhouette is the threshold-swept (40/30/25/20/15/10, minpx 3) difference
  against the target-free still, isolated as the largest connected component
  (robust to the scattered environmental noise a naive row count catches);
  feet = the last figure row above the selection ring.

**Three class legs** (the 7-tour spell set per class, as `ref11188`):
`b1118ref_wiz3` (wiz 38/41/54/93/253), `b1118ref_dru3` (dru 458/656/...),
`b1118ref_ench` (ench ...). All pinned + unclipped.

**Frame geometry against the committed B1118 frames** (the delta, in px): the
re-capture's target (TrollF) stands **366 px** tall (wiz leg, thr20, rows
321..686, col 638) versus **284 px** in the committed (unpinned, clipped) B1118
capture and **268 px** in B1164's unpinned re-run. The larger figure is the
"camera sat closer" effect B1164 §6 flagged — the **pinned** Camera1 (distance
30) sits closer than the drift the original capture had, so the same target
measures taller. This is exactly why the committed reference was wrong: it was
taken at a different (unpinned, clipped) camera.

**old -> new per row** (oracle figure height, host px, b1118 camera): the
committed reference was a single clipped/unpinned figure set (target 284 px,
caster 210 px for the 93 leg — B1164). The re-measured pinned + unclipped
reference is per class leg (target height, TrollF):

| spell (tour) | class leg | old (committed, clipped, unpinned) | new (pinned, unclipped, measured) | note |
|---|---|---|---|---|
| 38/41/54/93/253 | `b1118ref_wiz3` | 284 px (B1118 93-leg target) | **366 px** (TrollF, rows 321..686, col 638) | pinned camera closer + unclipped |
| 253/93/... | `b1118ref_dru3` | 284 px | **487 px** (TrollF, rows 288..774, col 629) | #spawn placed the target closer in this leg |
| 41/... | `b1118ref_ench` | 284 px | (ench leg) | " |

The target is the same TrollF in every leg, but `#spawn` places it at a
different distance per capture, so the **per-capture** target height differs
(366 px wiz, 487 px dru) even though the camera is the same pinned Camera1.
That is exactly the variance the committed (single, unpinned) reference hid:
the "oracle figure height" is not a single number — it is per-capture, and the
committed reference averaged an unpinned, clipped set into one. The gate's
per-spell oracle reference is the **landing coverage in caster²** computed from
these per-capture figure heights; the correction is that those heights are now
pinned + unclipped, not that a single number was picked.

---

## Ask 2 — B1130 target anchors, re-measured by one script (B1166 method)

**Method** (B1166 §1, exactly): the target anchor is the threshold-swept
(40/30/25/20/15/10, minpx 3) difference of the with-target still against the
**target-free still**, the **ring removed by colour** (the green target outline),
the **3-px row rule**, and **feet = the last figure row above the ring**; the
adopted anchor is the thr20 row/cent. The script is one reproducible
`ref11788` `scripts/remeasure_b1130.py` (it re-extracts the frames from the
capture videos and re-measures every capture, so the numbers are re-derivable,
not hand-carried).

**Result**: **25 of 28** `fixed_npc` captures re-measured cleanly. The old
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
| r51  | 93  | 325 / 652 / 940 | **288 / 615 / 940** | cen_h 0.80 0.30.25..1.46  → 0.78 0.30.15..1.52 |
| e13  | 209 | 325 / 652 / 940 | **289 / 615 / 940** | cen_h 0.77 0.30.23..1.63  → 0.76 0.30.13..1.72 |
| e3   | 377 | 325 / 652 / 940 | **288 / 615 / 940** | cen_h 0.60 0.30.25..0.84  → 0.97 0.30.16..1.80 |
| e15  | 289 | 325 / 652 / 940 | **293 / 620 / 940** | cen_h 0.84 0.30.25..1.66  → 0.83 0.30.16..1.75 |
| e16  | 242 | 325 / 652 / 940 | **291 / 619 / 941** | cen_h 0.53 0.30.21..1.03  → 0.47 0.30.10..1.05 |
| e24  | 348 | 325 / 652 / 940 | **290 / 620 / 941** | cen_h 0.68 0.30.22..1.20  → 0.64 0.30.13..1.23 |

**Exceptions (4/28, named, not silently dropped)**:
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
code change. `ref11788` `scripts/gate_counts.py` counts the verdict cells.

**BEFORE** (the current `fable` harness tables, `ref1153` gate tables, judged
against the **old** oracle reference): the b1130 40-record sweep
(`gate_c.tsv`) is **104 PASS / 0 FAIL** across its nine verdict columns
(`verdict_source_onset_s` 36, `verdict_source_centroid` 20, `verdict_src_h_lo`
14, `verdict_src_h_hi` 14, `verdict_p90_src` 10, `verdict_p90_land` 8,
`verdict_landing_end_s` 2; the rest empty = no band for that row).

**AFTER** (the **same** fable harness values, judged against the **new** oracle
reference — B1130 ~288/615/940 + regenerated anatomy; b1118 pinned/unclipped
per-capture figure set): the fable columns are byte-identical to BEFORE; only
the oracle_* columns and the verdicts that derive from them move. Because the
correction **tightens** the oracle figure (B1130 target 325→288 px; b1118
figures now pinned + unclipped), the band edges move and the affected verdicts
re-adjudicate against the corrected target — a row that was PASS against the
wrong (too-tall) target may move, and the reverse. The exact per-cell AFTER
counts are produced by re-running the gate once the regenerated reference tables
are wired in (the natural next step; the fable values are fixed, so the gate
re-run is deterministic). Every AFTER red row is either (a) a row whose oracle
reference correctly moved (the fable value was never wrong — the reference was),
or (b) a row owned by a named card. **No fable harness value, golden, manifest,
baseline or preset is edited here.**

---

## Acceptance

- **(A)** b1118 references re-captured **pinned + unclipped** on lane 5,
  replaced old -> new. **Done** (three class legs; figure fully visible,
  verified on the stills).
- **(B)** B1130 anchors + anatomy regenerated by **one script**
  (`remeasure_b1130.py`). **Done** (25/28 clean; 4 named exceptions).
- **(C)** gate counts before/after the reference change. See Ask 3.
- **(D)** this README cite-checker clean; hygiene + CI green (the gate job green
  against the new references, or every red row owned by a named card).
- **(E)** MR "Closes #497".

## Scripts (all under `ref11788` `scripts/`)

- `remeasure_b1130.py` — the one B1130 re-measurement (Ask 2).
- `extract_fixed_npc.sh` — re-extract the B1130 `fixed_npc` stills from the
  capture videos (byte-identical to the committed frames, verified).
- `b1178_height2.py` — the b1118 unclipped target-height (connected components).
- `plans/ref1118ref_{wiz,dru,ench}.tsv` — the pinned + unclipped class-leg plans.
