# B888-CC4: character select + creation visual gate (GitLab #204)

Part of the #204 SCOPE chain (Will 2026-10-13). The feature-gap rule
(`docs/issues.md:66-77`): a feature is not done until a REPEATABLE test pins it.
CC-4 is that test for the two pre-game screens -- a per-control visual gate that
scores willeq's character-select and character-creation screens against the
official client, with a ledger, run on every CI tip.

## What the gate is

`scripts/ci/charcreate_gate.py` drives `tools/hwcaps/charcreate_capture` (the
willeq leg) -- which renders the gate's 14 states through the **production
render path** (`renderCharSelectScreen` / `renderCharCreateScreen` /
`renderFacePick`, the same functions the client calls) into a headless 1280x800
GLES2 frame -- and scores each state against the XML-derived control rects
(`char_select_layout.h`, `char_create_layout.h`, `face_pick_layout.h` at
sw=1280, sh=800, the same image space the client renders into):

- **per-control presence + edge IoU** (a bright pixel with a dark 4-neighbour;
  a control that moves, shrinks or vanishes drops below the 0.9 bar);
- **`REQUIRED_CONTROLS` per state** -- the controls that MUST render. A missing
  required control is the only failure. Empty character slots and willeq's
  deliberately-undrawn Quit / Enter (the layout header marks that tutorial pair
  OUT) are scored in the ledger but never fail, by design.
- **OCR label reads** -- reported as FINDINGS, not failures: the stock client's
  small antialiased type is not OCR-reliable at this crop, and a mismatch is a
  finding for a later card, not a reason to loosen the bar.
- **reference IoU** against `tests/goldens/charcreate/` -- NOT-BLESSED until the
  official-client reference set is blessed in (a coverage gap, not a failure).

Coordinate convention (LOCKED): image space is 1280x800; the client's root is
offset +0+30 (the reference stills are `import -crop 1280x800+0+30` output), and
the layout headers at sh=800 land directly in image/crop space.

## The 14 gate states

The list is ONE list, kept in three places that must not drift apart (netted by
the `CharCreateGateStateList` hygiene test): `GATE_STATES` in
`scripts/ci/charcreate_gate_controls.py`, the `kStates[]` array in
`tools/hwcaps/charcreate_capture.cpp`, and the table below.

## Scores at this head (fable tip 993a22e3)

Measured by `scripts/ci/charcreate_gate.py --willeq-states <dir>` on this
host's llvmpipe (the CI runner is this host). minIoU is over each state's
REQUIRED controls; required-present is the count that drew. Reference IoU is
NOT-BLESSED (the official-client reference set is proposed as a bless, see
below) and OCR is a finding. **All 14 states PASS the willeq-internal gate.**

| state | IoU min | required present | ok |
| --- | --- | --- | --- |
| `select_empty` | 1.000 | 6/6 | OK |
| `select_one` | 1.000 | 6/6 | OK |
| `select_delete_prompt` | 1.000 | 4/4 | OK |
| `create_initial` | 0.981 | 29/29 | OK |
| `create_after_gender` | 0.981 | 29/29 | OK |
| `create_after_race` | 0.981 | 29/29 | OK |
| `create_after_class` | 0.981 | 29/29 | OK |
| `create_stats_part` | 0.981 | 29/29 | OK |
| `create_stats_all` | 0.981 | 29/29 | OK |
| `page1_initial` | 1.000 | 7/7 | OK |
| `page1_deity_city` | 1.000 | 7/7 | OK |
| `page1_getname` | 1.000 | 7/7 | OK |
| `page1_name_rejected` | 1.000 | 7/7 | OK |
| `facepick_open` | 1.000 | 4/4 | OK |

The 0.981 (vs 1.000) on the five creation-page-0 states is the stat up/down
glyph pair: the `stat_up`/`stat_down` rects carry a 1px border that, at this
font size, reads 0.981 of its own edge density -- above the 0.9 bar, not a
regression.

## Findings (mismatches are findings, not loosened bars)

- **OCR of the window title reads garbled** ("character select" -> 'ssr ee
  se tt' on the small stock font at this crop). The control is PRESENT and its
  edge IoU is 1.000; the string is the client's own (resolved from `willeq.db`,
  not "eqstr <id>"). Logged as a finding: OCR at this font size is unreliable
  and is therefore a report, not an assertion.
- **Reference set NOT-BLESSED**: the official-client reference stills are
  proposed under `/home/user/kb-evidence/kb-cc4-charcreate-oracle-gate/
  goldens_proposed/` (3 of the 14 states -- the B1048 oracle session's
  `select_one`, `create_initial`, `select_after`; the other 11 are an open gap
  for a follow-up oracle session). Adding them to `tests/goldens/charcreate/`
  is a BLESS (Will's sign-off); the `charcreate_gate` ctest row skips with 125
  ("reference set not blessed") until then. The willeq-internal per-control
  gate runs on all 14 regardless, so this is a coverage gap, not a failure.

## The ctest rows

- **`charcreate_gate`** (label `charcreate_gate`, graphics build): runs
  `scripts/ci/charcreate_gate.py --bless-required`. Skips with 125 until the
  reference set is blessed into `tests/goldens/charcreate/`; then it runs the
  full gate (willeq capture + per-control IoU + reference IoU) and fails on a
  missing required control or a reference-IoU regression.
- **`CharCreateGateStateList`** (label `hygiene`): the three-way state-list
  agreement check (`scripts/refactor/check_charcreate_gate_state_list.py`).
  Pure text scan, runs in every configuration.

## Not verified / left open

- The 11 reference states not captured by the B1048 session (see Findings) --
  a follow-up oracle session on lane 5 (`scripts/eqemu/oracle/oracle_session.sh
  5` with a 14-state plan) fills them; the bless activates reference-IoU for
  every state it covers.
- OCR label assertion: left as a report. A later card may add a bigger font or
  an OCR-preprocessing pass to promote the title check to an assertion.
