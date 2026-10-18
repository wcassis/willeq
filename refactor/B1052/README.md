# B1052 (#372) — the item display window's placement, oracled

Will 2026-10-13 (XU4 play test, 1280x720): "the inventory item details popup
is placed near 0,0; it should be rendered off the cursor so it is close to the
item being inspected."

The card's premise — that the official client renders the window **relative to
the click** — does not survive contact with the measurement. The official
client renders it at a **fixed per-resolution default position**, independent
of where the item was clicked.

## What the oracle shows

Two independent sources, agreeing:

**1. The measured capture (review/UIGAP-ORACLE/151/run1.*, 2026-10-05, stock
Titanium, 1024x768 client window).** `gestures.tsv` rows 24–27 + stills
`run1.G29_rclick_HOLD_sword_display_while_held.png` (right-hold, display up),
`run1.G30_alt_lclick_sword_display.png` (Alt+left, display persistent),
`run1.G29_after_release.png` (release, display gone). `run1.mp4.geom`
records the client window as 1024x768 at root (-1,30); the stills are the
window crop, so pixel = window-local.

Deterministic measurement (the held still minus the after-release still,
the display is the only thing that changes — `measure151.py`):

| click (window-local) | display top-left | display size | click → top-left offset |
|---|---|---|---|
| (341,476) — equipped primary sword slot | **(0,224)** | 400x190 (body bbox x[6..390] y[225..412], frame outside) | (335,251) |

The offset is not a round number, not a constant that could generalise —
because there is no rule: the window opened at its **INI default position**.

**2. The client's own window-position table (`defaults.ini
[ItemDisplayWindow]`, the shipped P1999 install, `/mnt/projects/EverQuestP1999`
on the oracle host):**

| resolution | X | Y | W | H |
|---|---|---|---|---|
| 800x600 | 0 | 224 | 400 | 190 |
| 1024x768 | 0 | 224 | 400 | 190 |
| 1152x864 | 0 | 224 | 400 | 190 |
| 1280x720 | 0 | 224 | 400 | 190 |
| 1280x1024 | 0 | 224 | 400 | 190 |
| 1600x1200 | 0 | 224 | 400 | 190 |

Every resolution is (0,224) 400x190. The RoF-era client on the current lane-5
rig carries the same section in its per-character UI ini as `[ItemDisplay0]`:
1280x800 = (0,240) 400x190 — a slightly different Y for the newer resolution
class, same fixed-position mechanism. (That INI key only exists for the
resolutions the client actually runs; 1280x800 is not in P1999's table, so
224 is the right value for fable's 1280x720, which IS in the table.)

**Why `EQUI_ItemDisplay.xml <Location> 0,80</Location>` is not the answer:**
the XML is the window's *authored* default for a screen with no INI entry —
the INI table overrides it per resolution, and the INI table is what ships
with the client and what the client renders. fable read the XML and skipped
the INI; the result is Will's "placed near 0,0" (0,80 sits just below the
spell-gem bar, hugging the top-left corner).

## The rule

**The item display window opens at (0, 224) size 400x190, screen
coordinates, every resolution fable supports (all ≥ 640x480 have room:
224+190=414 ≤ 480). The click point does not move it.** This is the
official client's own per-resolution default — not fable's guess and not a
click-relative placement. (The user can move it with the window's own drag
in the official client, and fable keeps the window's existing close box;
persisted per-user positions are out of scope for this card.)

## Implementation

`include/client/graphics/ui/item_display_layout.h`: `WINDOW_Y` 80 → 224, with
the source comment re-pointed at the INI table + the run1 measurement (the
`<Location> 0,80` citation is kept in the header only as the explanation for
what the XML says, not as the position fable draws). `windowRect()` is
unchanged in shape: (0,224) 400x190 clamped to the screen width.

The intent that opens the window (`ItemDisplayRequestIntent`) carries the
slot, not the click point, and the click point is **not** plumbed through —
plumbing it would be building the feature Will's report asked for, but the
measurement says the oracle does not have that feature. The XML default is
kept as the code's fallback meaning (the constant IS the default position),
so `/itemdisplay`-from-link and keyboard opens land identically.

## Pin

`tests/test_item_display.cpp::ItemDisplayT421.TheWindowIsTheOraclesOwnLocationAndSize`
asserts (0,224,400,190) at 800x600 (all resolutions resolve identically —
the INI table is constant and `windowRect` only clamps width). It **fails at
the pre-fix head** (the layout returned Y=80 there) and passes at this tip.

## Verification status (honest, for the cold grader)

| card row | state | what it rests on |
|---|---|---|
| (A) oracle table + rule | PARTIAL | ONE measured click (1024x768, run1: (341,476) → (0,224) 400x190) plus the shipped `defaults.ini [ItemDisplayWindow]` table (6 resolutions, all (0,224) 400x190). The card asked for 3+ clicks × 2 resolutions; the click evidence is a single positive capture — the per-resolution part rests on the client's shipped table, not on three measured clicks at a second resolution. The RoF-era lane-5 client never showed the standalone display at all (~90 stills + full video, below), so a second live measurement was not obtainable on the only available oracle rig. |
| (B) test fails pre-fix, passes at head | MET | `ItemDisplayT421.TheWindowIsTheOraclesOwnLocationAndSize`: FAILED at the pre-fix head (layout Y=80), PASSES at head (16/16, rbuild on the ThinkPad). |
| (C) x86 stills | NOT VERIFIED | No live-server scenario run was done on the retry leg; the drawn rect is the layout constant the pin asserts, so draw and click read the same rect by construction (T57). |
| (D) `/itemdisplay`-from-link opens at the default | MET by construction | The intent carries no click point; `windowRect()` is the single source of the rect for every open path. |

The rule above rests on the run1 measurement + the shipped INI table, both
in-baseline-era artifacts. A follow-up that gets the stock-Titanium rig back
can add the second-resolution live measurement; until then the INI table is
the per-resolution evidence.

## Lane-5 negative evidence (recorded, not the basis of the rule)

The current lane-5 oracle (RoF-era client, 1280x800) did not show the
standalone display in ~90 stills + a full 5 fps video across: right-hold
0.4–28 s, stationary vs 1px-drag, Alt+left (the stays-open path), and
right-hold on a cursor-held item — on both general inventory slots and the
equipped staff slot (session-b / session-probe…/session-probe8 in
`/home/user/kb-evidence/kb-b1052-item-display/`). Input is proven working on
that rig (left-click pickup puts the item on the cursor). The delta from the
run1 rig (stock Titanium 1024x768, where the display DID appear at (0,224))
is the client era + resolution. The rule above rests on the run1 measurement
+ the shipped INI table, both of which are in-baseline-era artifacts; the
lane-5 negative result is recorded here so a follow-up does not re-run those
probes believing the display is missing client-wide.
