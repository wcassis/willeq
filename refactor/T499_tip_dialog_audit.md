# T499 — B795: the "Tip of the Day" re-check of every archived oracle frame

Class A follow-on to **B795** (found by T492 Leg H). Lane: **OFFLINE** — no eqemu, no Pi,
no VM, no client launch. Nothing is re-scored and no constant is changed; this unit only
says, per archived frame, whether the dialog was in it, and whether that frame fed a number.
Branch `task/T499-tip-dialog-audit` off fable `817a76f0`. Board entries start at **B828**.

## The facts this plan rests on

- [measured] The official Titanium client pops a modal **Tip of the Day** dialog after
  entering the world. Its `[TipWindow]` rect in the character UI ini is **(664,234) 360x300**;
  the rig runs the client at 1024x768 with the X window at root **(-1, 30)**.
- [measured] T492 Leg H's target gate OCR'd **`ow Another Tip`** where a character name
  belonged — the dialog's own "Show Another Tip" button, i.e. the dialog occupies the region
  the official TARGET window uses. Board B795.
- [measured] T492 Leg I turned the dialog off (`[Tips] ShowTips=0`) and dismissed it at
  launch, so **only frames recorded before Leg I can carry it**.
- [assumed] B795's open half — "earlier units' post-login grabs should be re-checked before
  being trusted" — was never discharged. This unit discharges it.

## Method

### The corpus

Every `.png` under any directory named `oracle/` beneath `review/T*/` (17 such directories,
including the nested `review/T310/legC/oracle/`), plus every `.png` under
`review/T492/llmbench*/players/p1/` (the official player's frames and the `.camto_probe` /
`.target_probe` probes). Frame count is recorded in the Review.

### The detector — `scripts/oracle/find_tip_dialog.py`

Two independent signals, **both** required for a `dialog` verdict:

**(a) OCR.** tesseract `--psm 6` over the dialog rect, upscaled 2x (the dialog's text is 8 px
and tesseract is unreliable below ~20 px), matched against
`/Tip of the Day|Show Another Tip|Don't show any tips/i` — the title bar, the button and the
checkbox, all three inside the rect.

**(b) The title-bar bitmap.** The dialog's title bar is a fixed 17-row x 359-column UI bitmap
(rounded gold ends, the engraved caption, the beveled border rows). It is byte-identical in
every frame that shows the dialog because it is chrome, not scene content. The template is
lifted at run time from the known-positive frame and matched **anywhere in the frame**:
candidate rows are found cheaply by a horizontal run of >= 300 px of the measured top-border
colour `(140,130,113)`, then the full 17-row template is compared at each candidate with a
per-channel tolerance of 2; a match is >= 90% of template pixels. Because the search is
position-independent, the detector does **not** assume every unit's rig placed the dialog at
the same ini rect.

### The coordinate mapping [measured, T499]

Two kinds of grab exist in the archive, and they are one affine shift apart:

| grab | size | dialog rect used | how it was measured |
|---|---|---|---|
| window | 1023x738 | x `[664,1024)`, y `[229,534)` | on the known positive, the title-bar top-border row is **y=229** and the dialog spans **x=664..1022** (the 360th column is clipped off by the 1023-wide grab) |
| root | 1024x768 | x `[663,1023)`, y `[259,564)` | on `review/T492/llmbench-legH/players/p1/.camto_probe.png` the same top-border row sits at **y=259 = 229+30** and the same run starts at **x=663 = 664-1** — exactly the window's root offset `(-1,+30)` |

The ini's `y=234` is the **client area below the title bar**; the bar itself starts 5 rows
higher, which is why the rect used is 229..534 and not 234..534. Any other frame size gets the
1024x768 rect scaled by `(w/1024, h/768)` as a default; when the template fires, the OCR crop
is taken at the **matched** position instead, so the default only matters for OCR-only frames.

### Verdicts

`dialog` both signals fired · `clear` neither fired · `unreadable` exactly one fired (ambiguous —
a human must look), or the frame could not be opened / OCR could not be run.

### Validation before the sweep

The script refuses to sweep unless `--self-test` passes on three frames:
a known positive in **window** coordinates
(`review/T492/llmbench-legH/players/p1/frames/19:24:01.004_login_camera_untouched_window.png`),
the same in **root** coordinates (`…/llmbench-legH/players/p1/.camto_probe.png`), and a known
negative with the UI off
(`review/T478/oracle/T478_oracle_h160_login_camera_noui_window.png`).

## Tasks

1. Write and validate `scripts/oracle/find_tip_dialog.py`; record the validation result.
2. Sweep the corpus to `review/T499/tip_dialog_audit.tsv` (frame, unit, size, ocr_hit,
   pixel_hit, verdict, template_frac, ocr_text) with a per-unit summary.
3. For every `dialog` frame, grep its basename through that unit's `review/<unit>/` tree
   (README.md, logs, `*.tsv`, `*.md`, `*.txt`, `*.json`) and through `refactor/` — the unit's
   plan and its Review — and list every score or table that cites it.
4. Board: strike B795's "earlier units deserve a re-check" half with the counts; open one entry
   per affected unit whose scores are contaminated, from **B828** (class A if the score decided
   a shipped constant, B otherwise). If nothing is contaminated, say so with the counts.
5. `review/T499/README.md`; commit scripts + board + plan; do **not** merge.

## STOP conditions

- Do not re-score anything and do not change any constant. A contaminated score is **boarded**,
  not corrected.
- Do not judge any frame by eye beyond the two validation frames; the verdict is the detector's.
- If the detector's self-test fails, stop and report rather than sweep.

## Review

**The re-check is discharged and almost nothing was contaminated.** Counts, all
[measured]: **1,183** archived official frames across **17** units scanned —
**382 dialog, 799 clear, 2 unreadable**. **363 of the 382 are T492's own.** The other
**19 sit in six units** (T377 1, T386 1, T389 1, T401 3, T466 2, T478 11) and **none of
them fed a number**: 13 are cited anywhere at all
(`review/T499/dialog_frame_citations.tsv`), every citation is a grab-log line or a
"what this file shows" README row, and the two that back a claim back an OCR reading of
chat or inventory text. Per-frame table: `review/T499/tip_dialog_audit.tsv`.

**The one contaminated score is T492's own, and it is in the delivered corpus.**
[measured] `scripts/llmbench/verify_labels.py` scores the official frame over
`CROP = "1000x480+12+40"` in root coordinates (x 12..1012, y 40..520); the dialog's root
rect is x 663..1023, y 259..564, so it covers **349 x 261 px = 19.0% of the scoring
crop** while the T466 reference stills are `_noui_` and have none.
**6 of the 24 scored rows in `review/T492/llmbench/verify_labels.tsv` are on dialog
frames** — 4 PASS (`A_cam_p2_firstperson`, `A_camget_p2`, `A_cam_p3_firstperson`,
`A_camget_p3`) and 2 INCONCLUSIVE (`F_sky_{p2,p3}_overhead`);
`review/T499/t492_verify_labels_provenance.tsv` has the frame for every row. [measured]
the two PASS ratios that are readable in the table, **1.66 and 1.62**, are the two
closest to the 1.5 margin in the whole table. [assumed] that the dialog is the whole of
that difference — nobody re-scored anything, which this unit was forbidden to do. Board
**B828**.

**The T466 near miss is the reason this was worth running.** [measured]
`T466_oracle_alpha_login_camera_untouched_1024x768.png` — the frame that decided the
official untouched login camera is First Person (0.232 vs ≥ 0.897), the measurement T471
applied to fable — **has the dialog in it**, and its scoring crop is `660x450` upper-left
while the dialog's leftmost column in that frame is **664**. Clear by four pixels.
[assumed] that "660x450 upper-left" means `+0+0`; the tsv header is the only record of
the crop and no script carries it. Either way the root-grab offset would put the dialog
at 663, still outside.

**Two things not contaminated that could have been.** [measured] T478's Chase fit and
held-item sheets read only `_noui_` frames and **all of those are clear** — its 11 dialog
frames are `_ui_` grabs kept as chat/inventory evidence. [measured] T492 Leg F's F9
cycle, the 20 grabs that validated the MARGIN ≥ 1.5 criterion at min ratio 4.90, are
**all clear**; that run toggled the UI off with F10.

**Detector validation, before the sweep** (the script exits non-zero and refuses to sweep
if it fails): 3 known positives and 1 known negative, **0 false positives, 0 false
negatives**, chrome match **1.000 / 1.000 / 1.000 / 0.000**. The third positive is a
*caption variant* and it is why the template masks the caption columns. A fifth,
unplanned check landed in the sweep itself: [measured] T492 Leg I — the only run with
`[Tips] ShowTips=0` plus a launch-time dismissal — comes out **0 dialog in 63 frames**,
while Leg E on the same rig, zone and character comes out **139 in 153**.

**A finding B795's title does not cover.** [measured] the same 360x300 window at
(664,234) carries a different caption depending on what the player just did:
`Consider Tip window`, `Death Tips`, `Inventory Tips`, `Looting Tips`, `Sitting Tips`,
`Keyboard Options Tips` — six context-sensitive variants across five units, alongside the
login `Tip of the Day` (whose *body* heading — "Etiquette Tip", "Safety Tip", "Gameplay
Tip", "Convenience Tip", "Performance Tip" — is not a caption and was not mistaken for
one). B795's fix dismisses one window at one moment. [measured] Leg I's 31 steps contain
no sit, death, loot, consider or options action, so its clean sweep is evidence for the
login window and nothing else. Board **B829**. [measured] the delivered corpus
`review/T492/llmbench/` is **103 of 251** official frames and its README says nothing:
board **B830** (P).

**The two `unreadable` rows** are both `Keyboard Options Tips` frames (T377, T466) where
OCR read the caption but the chrome match reached **0.907** against a 0.95 bar, because
the Options window sits over part of the title bar. [assumed] they are true positives;
they were deliberately **not** promoted by hand, and neither is cited by anything.

**What this unit did not do**, by its own STOP conditions: nothing was re-scored, no
constant changed, and no frame was judged by eye beyond the four validation frames.

### Owed / next

- B828 wants `verify_labels.py` re-run on six labels with the dialog rect masked out of
  both crops, or those four `A_*` steps re-recorded behind Leg I's dismissal. Offline-able
  for the mask; the re-record needs the eqemu lane.
- B829 wants one probe: `ShowTips=0`, then `/sit` and an options page, OCR the rect.
- B830 is two README lines.
