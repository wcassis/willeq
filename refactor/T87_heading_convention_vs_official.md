# T87 — Does WillEQ's heading match the official client's, on the wire?

**Slot: slot2. NO EQEMU, NO LIVE SESSION. Pure analysis of data already
captured. Do not bring the server up for any reason.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T87/`, throwaway
analysis scripts. PRODUCTION CODE IS READ-ONLY — and see the prohibition
below, which is the most important part of this plan.**

Read, in order and in full: **`docs/CLAUDE.md`'s "Heading/Rotation
Conventions"**, the **load-bearing comment in `src/client/eq.cpp`** at the
PlayerProfile heading conversion, T84's Review, T85's Review, and
`/mnt/projects/eq-oracle/captures/README.md`.

## The question this finally makes answerable

WillEQ's heading handling has been validated only against itself. T84 proved
two WillEQ clients agree with each other; T85 proved our encoder is internally
consistent — no second bug between a correct `m_heading` and the wire. **None
of that establishes that our convention is correct**, because a
self-consistent misinterpretation is invisible to every WillEQ instance.

The baseline capture changes that. It contains the **official Titanium client
facing each cardinal direction**, announced with in-stream `/say` markers. The
official client is ground truth: whatever it puts on the wire for "north" *is*
north.

So: **for the same facing, does WillEQ put the same value on the wire as the
official client?**

## THE PROHIBITION — read before forming any opinion

**You must not modify heading, rotation, or coordinate-conversion code. Not
one line. Not even if this analysis shows a disagreement. Especially then.**

The `90 - server_heading_deg` offset applied to the player but not to other
entities is **deliberate and hard won**. Players and NPCs do not share the same
default rotation and the model files carry differing default rotations. T84
found a real heading bug, reported it, changed nothing. T85 did the same. Do
the same.

Note also a distinction that will matter here: **that guarded offset is on the
INBOUND path** (decoding the player's own PlayerProfile). This unit examines
the **OUTBOUND** path. They are different code. Do not conflate them, and do
not treat an outbound finding as evidence about the inbound offset.

A disagreement is **a finding to report with evidence**, and it becomes its own
unit with its own review.

## Method

### Segmenting
Both actors announced with `/say` before turning, and those markers are
**inside the packet stream**, which makes them more reliable than any external
timeline. Use them.

- `Wizzy` — official client, human-driven, source `10.0.30.169` (the only
  remote address, trivially separable). Shot list item 1: each cardinal plus
  intermediates, stationary, `/say` announced.
- **WillEQ** — Role 2, two passes, `/say` announced per direction:
  - **Pass A** drove heading via `/move` (verified live to bypass
    `EqActionHandler::setHeading()`), so `m_heading` held a correct degree
    value. **Pass A is the meaningful comparison.**
  - **Pass B** used `/turn` — T84's known double-conversion bug. Expect it to
    be wrong; it is the control, and it should show the ×512/360 signature.

### The comparison
For each direction, extract the **outbound** position-update packets and
decode the heading field. Build:

| direction | official raw | official decoded | WillEQ Pass A raw | Pass A decoded | Pass B raw | delta |

### Look for a systematic transformation, not exact equality
`Wizzy` faced each direction **by human visual judgment**, so he may be several
degrees off true north. **Do not treat small differences as a defect.** What
matters is whether there is a *systematic* relationship — a constant offset
(90°, 180°), a scale factor (×512/360), an inversion, or an axis swap — across
all six directions. One direction proves nothing; a consistent pattern across
all of them proves everything.

State the residual scatter explicitly so a reader can judge how tight the fit
is.

## Capture facts you must account for — measured, not assumed

1. **A LAN client's packets appear THREE times** on the `-i any` capture.
   Dedupe before counting.
2. **Both VM clients share source `10.0.30.23`**; they separate by `ip.port`
   session, and the oracle note's WillEQ attribution is **by elimination, not
   protocol fact**. Cross-check against WillEQ's own `.sleq` fragments and
   state your confidence.
3. **Session identity is per zone** — new source port on every zone change.
   Zone ports: **7007 = South Qeynos**, **7012 = North Qeynos**.
4. **All three characters were GM-flagged (status 250).** Position and heading
   are believed unaffected, but say so rather than assuming it.

## Use the existing protocol tooling

Do not hand-roll a Daybreak parser. `src/common/net/` implements the protocol
(sequencing, fragmentation, CRC, compression) and `tests/test_daybreak_*` /
`tests/test_pcap_*` already read a pcap. Reuse that. The Daybreak layer
compresses — a raw byte search of the pcap will not show you heading fields,
and concluding anything from an unparsed search would be wrong.

**Validate your extraction before trusting it**: WillEQ's Pass B is a known
quantity (T84/T85 measured `/turn 90 → m_heading 128`, `/turn 225 →
m_heading 320`). If your method does not reproduce the known Pass B values, it
is not yet correct — fix it before reading anything into Pass A.

## The evidence

- `/mnt/projects/eq-oracle/captures/baseline_official_official_20260812-234154/`
  — `capture.pcap`, `running-order.md`, `summary.md`
- WillEQ's six `.sleq` fragments in `/mnt/projects/fable/wt/t1/data/captures/`
  (gitignored; copy what you need, that worktree will be reused)
- T85's Review for the Role 2 timings and its six-direction internal table

## Possible outcomes, all of them useful

- **Match** — our outbound convention is correct, and the whole heading defect
  is T84's `setHeading()` double-conversion. Clean, and it closes the question.
- **Systematic disagreement** — our outbound convention is wrong in a specific,
  characterized way. **Report it; do not fix it.** Include the exact
  transformation needed, so the follow-up unit starts from measurement.
- **Cannot be determined** — say so plainly and say what would be needed. A
  clear "not answerable from this capture" beats a confident guess, and the
  oracle session can capture more.

## What you may not do

- **NO production code changes.** None.
- **Do not start the eqemu server.**
- **Do not conclude from a single direction.** Six directions were captured
  precisely so a pattern could be distinguished from noise.
- **Do not read an outbound finding as evidence about the guarded inbound
  offset.**

## Verification

Build + full suite to prove nothing broke (expect 2066/2067; sole failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Analysis scripts,
the comparison table and findings to `/mnt/projects/fable/review/T87/` with a
README.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T87-heading-vs-official`, HEAD `b4d11d5f`. **No production code
touched** -- `git status` on the task branch shows only this plan file and
`refactor/deferred.md` changed; nothing under `src/`/`include/`. No eqemu
server started, `.eqemu-lock` never touched. Full analysis scripts, raw
decoded CSVs, and README: `/mnt/projects/fable/review/T87/`.

### Headline finding: SYSTEMATIC DISAGREEMENT, not a match

WillEQ's outbound wire heading (`SendPositionUpdate()`,
`src/client/eq_movement.cpp`) is offset from the official client's by
**~252° (mod 360), measured** -- not a round 90/180/270. Residual scatter
across the four reliable directions (East/South/West/NE) is **244.97° to
261.42°, spread 16.45° (±8.2° from the mean)** -- tight enough to be a real
systematic pattern, not noise. Per the plan's prohibition, **nothing was
fixed**; this is a finding for a follow-up unit, with the measured
transformation as its starting point. **This is an outbound-path finding
only** -- it says nothing about, and does not touch, the guarded inbound
`90 - server_heading_deg` PlayerProfile flip (`eq.cpp`'s load-bearing
comment).

### Six-direction comparison table

| direction | official raw | official decoded | WillEQ Pass A raw | Pass A decoded | Pass B raw | Pass B decoded | delta (official − Pass A) |
|---|---|---|---|---|---|---|---|
| North | 2046 | 359.65° | 838 | 147.42° | 512 | 90.00° | 212.23° *(flagged)* |
| East  | 1538 | 270.35° | 122 | 21.51°  | 375 | 66.00° | 248.84° |
| South | 1033 | 181.58° | 1593 | 280.16° | 1103 | 194.00° | 261.42° |
| West  | 520 | 91.41°  | 1174 | 206.44° | 1831 | 322.00° | 244.97° |
| NE    | 1808 | 317.81° | 368 | 64.78°  | 147 | 26.00° | 253.03° |
| SW    | 765 | 134.47° | 1174 | 206.44° *(dup of West, flagged)* | 739 | 130.00° | 288.03° *(flagged)* |

All headings are the same wire field (`OP_ClientUpdate`'s
`heading_and_padding` low 11 bits, `raw*360/2048` degrees) on both sides --
confirmed identical encoding, not an assumption (see validation below).

**North** is flagged: WillEQ's `/move` for this direction queued 12
conversion log lines before settling (every other Pass A direction: 5-9),
consistent with T85's own caveat that Pass A's waypoints were only
approximately compass-aligned. Its delta (212.23°) is ~40° off the clean
four's mean but still squarely in the same large-offset regime, not near
zero. **SW (WillEQ)** reproduces T85's own flagged artifact exactly: it
landed on the identical `m_heading` as the preceding West sample (243.56°)
byte-for-bit, most likely because the prior NE move hadn't fully completed
within the script's 5s hold. Excluded from the mean. **SW (official)**
needed one methodology fix, documented and corrected, not silently
absorbed -- see `review/T87/comparison_table.md`.

### Validating the extraction (mandatory per the plan)

**WillEQ side** reproduces T84/T85's known Pass B values exactly on all six
directions, including the plan's two named checks: `/turn 90 -> m_heading
128` (reproduced: 128.00° -> wire 322.00°/1831) and `/turn 225 -> m_heading
320` (reproduced: 320.00° -> wire 130.00°/739). Pass A also reproduces
T85's own Pass A table exactly, including the West/SW duplicate. One real
bug was found and fixed in the extraction script itself: the source log
mixes bare `\r` (in-place stdout progress writes) with `\n` terminators,
and Python's universal-newline text mode splits on a lone `\r` too, which
silently desynced this script's line numbers from `grep -n`'s and, worse,
made "first POS line after the marker" resolve to a stale pre-command echo
(the 250ms `SendPositionUpdate()` throttle fires a queued send from the OLD
heading before the new `/move`/`/turn` takes effect). Fixed by reading the
file as bytes and splitting on `\n` only, and by switching from "first
match after the marker" to "steady-state value across the marker-to-next-
marker window." Both fixes are commented in
`review/T87/extract_willeq_headings.py`, not silently made.

**Official client side** has no external ground truth to check against
(that is the point of this unit), so it is validated two other ways: (1)
internal consistency -- all eight of Wizzy's `/say`-marked directions (six
used in the table + NW/SE bonus, no WillEQ counterpart) land within ~5.5°
of `docs/CLAUDE.md`'s documented 0/128/256/384-unit compass rose, an
independent cross-check the extraction pipeline has no way to fake; (2) the
SW anomaly above was caught by that same cross-check, root-caused, and
fixed.

### Session/attribution confidence

- **Official client: certain, not by elimination.** `Wizzy`
  (`10.0.30.169`) is the pcap's only non-server-side-NAT remote address
  per the oracle README's own documented fact -- no ambiguity, and this
  unit never touched WillEQ's own packets *from the pcap* (the two-VM-
  client ambiguity the capture-facts section warns about does not apply
  here).
- **WillEQ side: certain, not inferred.** WillEQ's data came directly from
  WillEQ's own `-d4` application log (`t85_role2_run.log`), produced by
  WillEQ's own process during T85's Role 2 session -- not a network
  capture requiring attribution.
- **Zone**: both sides' data is South Qeynos (Wizzy's item-1 routine
  00:14:14-00:17:42 UTC, zone port 7007; WillEQ's Role 2 Pass A/B
  00:55:12-00:57:16 UTC, same zone per T85's timeline) -- no cross-zone
  confound.
- **Dedup**: `tests/pcap_test_utils.h`'s `readPcapFile()` used with
  `remove_duplicates = true` throughout, per the "LAN client's packets
  appear three times" capture fact (unconditional payload-byte dedup,
  applies regardless of which side is LAN vs remote).
- **GM flag (status 250)**: all three baseline-session characters were
  GM-flagged. Position/heading are **believed** unaffected by GM status
  (a plain field in a generic movement opcode, not permission-gated in any
  packet format this unit inspected) -- **not independently verified**,
  flagged rather than silently assumed, per the plan's instruction.

### Tooling used (no hand-rolled Daybreak parser)

Official-client decode reused the REAL production
`DaybreakConnection::ProcessPacket()` (`src/common/net/daybreak_connection.cpp`),
fed via `tests/pcap_test_utils.h`'s pcap reader -- the same technique
`tests/test_daybreak_connection_pcap.cpp` already uses (`#define private
public` to seed connection state from a manually-parsed
`OP_SessionResponse`), just pointed at the C->S direction of Wizzy's
session instead of the S->C direction that existing test exercises.
Compiled ad hoc with g++ (not wired into CMake/ctest -- throwaway analysis
tooling in `review/T87/`, per the plan's territory); command line and full
source are in `review/T87/README.md` and
`review/T87/extract_wizzy_headings.cpp`.

### What remains unanswerable from this data

- **The exact root cause of the ~252° offset.** Out of scope for this unit
  (characterizing the wire-level disagreement, not root-causing it, per
  the plan's blanket prohibition on heading/rotation code). A follow-up
  unit would need to trace `CalculateHeading()` (`eq_movement.cpp`, Pass
  A's `/move`-driven path) against a known-good ground-truth movement
  vector to see whether ~252° decomposes into the documented 90°
  self-heading flip plus something else, or is unrelated to it.
- **Whether ~252° is stable across zones/characters/sessions.** Exactly
  one clean data run exists (South Qeynos, one WillEQ character, one
  session). A follow-up oracle capture with longer `/move` distances (to
  shrink the ~2-6 unit waypoint slop T85 flagged) would sharpen the
  four-sample spread from ±8.2° to something tighter and confirm the
  constant is fixed rather than drifting.
- **North and SW's WillEQ-side reliability**, specifically. Both are
  flagged, not discarded -- a rerun with movement distances long enough to
  guarantee full path completion before the 5s hold would settle whether
  North's outlier delta (212° vs the clean four's 252° mean) is
  measurement noise or a second, direction-dependent effect.

### Verification

- Build: `cmake --build build -j24` -- **PASS** (exit 0, all targets
  built including `willeq`).
- Full suite: `ctest --test-dir build --output-on-failure` -- **99% tests
  passed, 1 tests failed out of 2067** (2066/2067). Sole failure:
  `AudioManagerTest.PreloadCommonSounds` -- the documented ALSA host
  quirk, matches the expected baseline exactly. No new failures.
- `git status` on the task branch: only `refactor/T87_heading_convention_
  vs_official.md` and `refactor/deferred.md` changed; nothing under
  `src/`/`include/`.
- Desktop-GL/ARM cross-build and visual gate steps were **not run** --
  this unit made no rendering-affecting change (analysis-only, no code
  touched), so those steps would exercise nothing new; the plan's
  Verification section scoped this unit to build + full suite.
