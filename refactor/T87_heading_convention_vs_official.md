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

(executor fills: the six-direction comparison table; whether a systematic
transformation exists and exactly what it is; residual scatter; how the
extraction method was validated against known Pass B values; confidence in the
session attribution; what remains unanswerable and what capture would answer it)
