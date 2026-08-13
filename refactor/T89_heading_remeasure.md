# T89 — Re-measure WillEQ's outbound heading against the official constants

**Slot: slot1. OWNS EQEMU (lock normally — it is free). x86 only.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T89/`, throwaway
session scripts. PRODUCTION CODE IS READ-ONLY — see the prohibition.**

Read T87's Review, the qualification in its merge commit (`git log 381a8bd1`),
T88's Review, then `docs/CLAUDE.md`'s Heading/Rotation Conventions **in full**.

## Why a fresh capture, and why no official client is needed

T87's Pass A headings came from `/move` travel directions computed while
WillEQ's `/move` target had been given in the wrong frame (T88). That data is
contaminated at source — **re-analysing the old pcap cannot fix it.**

But T87 recovered the official client's wire values, and those are constants:

| direction | official raw | official decoded |
|---|---|---|
| North | 2046 | 359.65° |
| West | 520 | 91.41° |
| South | 1033 | 181.58° |
| East | 1538 | 270.35° |
| NE | 1808 | 317.81° |
| SW | 765 | 134.47° |

Their spacing is 91.76 / 90.17 / 88.77 — essentially perfect 90° steps
matching the documented 0/128/256/384 convention, which is what makes them
trustworthy ground truth.

So this unit needs **only WillEQ and the server**. No official client, no
human actor, no coordination.

## Method

1. **Verify the frame before trusting anything.** T88 made `Move()` log both
   frames and `/move` echo `client=(...) -> server=(...)`. Use it. State the
   client-frame and server-frame position for every sample. If those two do
   not relate by the documented `m_x = server_y, m_y = server_x` swap, stop —
   something regressed.
2. **Face each of N / E / S / W / NE / SW** by a method that does **not** go
   through `EqActionHandler::setHeading()` (T84's double-conversion bug —
   `/turn` and `/face` both use it). Movement-driven facing is fine; state
   exactly how you did it and how you verified `m_heading` held the intended
   degree value **before** the packet went out.
3. Capture WillEQ's outbound `OP_ClientUpdate` heading for each, stationary.
4. Compare against the constants above.

## The validity test that matters most

**Check your own measurements' spacing before interpreting any delta.**
WillEQ's N→W→S→E steps must be ~90° apart, as the official client's are. If
they are not, your samples are not clean cardinals and **no offset you compute
from them is meaningful** — that is precisely how T87 produced a "~252°
systematic offset" that was an artifact of unevenly-spaced inputs.

A constant offset preserves spacing. If spacing is uneven, fix the measurement
before reporting a transformation.

Also: T87's Pass A produced byte-identical values for SW and West, meaning it
never achieved a distinct SW facing. Confirm each of your six directions is
actually distinct.

## Outcomes, all useful

- **Match** — our outbound convention is correct and the whole heading defect
  is T84's `setHeading()` bug. That closes a question open since T84.
- **Systematic disagreement, with even spacing** — real, and characterize it
  exactly: constant offset, scale factor, inversion, or axis swap. **Report
  it; do not fix it.**
- **Uneven spacing again** — the measurement is still wrong. Say so and say
  what you would need. Do not compute a mean over distorted samples.

## Prohibition

**Do not modify heading, rotation, or coordinate-conversion code. Not one
line — especially if you find a disagreement.** The `90 - server_heading_deg`
player offset is deliberate and hard won; players and NPCs do not share
default rotations and the model files carry differing ones. That offset is on
the **inbound** path; this unit measures **outbound**. Different code — an
outbound finding is not evidence about it.

T84, T85 and T87 each found real heading problems, reported them, and changed
nothing. Do the same.

## Session discipline

- Lock first (`/mnt/projects/fable/.eqemu-lock`), clean teardown, lock deleted.
- **Verify binary freshness against HEAD before any live conclusion** — this
  has cost three units now.
- Force daylight: `/say #set time 11 0` (space-separated).
- Pad scripts far longer than you think you need; a script that runs dry exits
  the client (T85).
- `-d 4` for command output; `--record-session` so the run is replayable.
- Restore any DB state and confirm by `SELECT` **after logout** — for a
  logged-in character `character_data` reflects the last save, not live state.

## Verification

Build + full suite (expect 2070/2071; sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`). Findings and the comparison table to
`/mnt/projects/fable/review/T89/` with a README. Open any image you produce
and describe what is actually visible.

## Review

(executor fills: how facing was achieved and how `m_heading` was verified
pre-send; the six-direction table against the official constants; **your own
samples' spacing**; whether all six are distinct; the verdict and, if a
transformation exists, exactly what it is; deviations)
## Review

**Executed in `/mnt/projects/fable/wt/slot1`. The live session completed and
its data survived; the executor agent died on an API error at the write-up
stage. Server was town down cleanly and the lock released before the failure —
verified independently (no containers, no lock, no client processes). This
Review was completed by the orchestrator from the surviving capture.**

Raw evidence: `/mnt/projects/fable/review/T89/` — `t89_full.sleq` (327K),
`willeq_run.log` (440K), `t89_tour_script.txt`.

### Verdict: a constant +90 degree offset, and this one passes the validity test

| direction | WillEQ raw | official raw | WillEQ deg | official deg | delta |
|---|---|---|---|---|---|
| North | 512 | 2046 | 90.00 | 359.65 | 90.35 |
| West | 1024 | 520 | 180.00 | 91.41 | 88.59 |
| South | 1536 | 1033 | 270.00 | 181.58 | 88.42 |
| East | 0 | 1538 | 0.00 | 270.35 | 89.65 |
| NE | 256 | 1808 | 45.00 | 317.81 | 87.19 |
| SW | 1279 | 765 | 224.82 | 134.47 | 90.35 |

**Mean delta 89.09 degrees, spread 3.16 degrees.**

Validity checks, which T87 failed and this passes:
- **Spacing is uniform**: WillEQ's transmitted N->W->S->E steps are 90.0 /
  90.0 / 90.0, matching the official client's 90-degree steps. A constant
  offset preserves spacing, and this one does.
- **All six directions are distinct** (T87's Pass A produced byte-identical
  values for SW and West).
- Residual spread of 3.16 degrees is consistent with a human facing by eye.

### Method

Facing was achieved by `/move`ing 100 units along each client-frame axis from
a fixed centre at client `(-1009, 146, 51)`, so the six directions are
90 degrees apart **by construction** rather than by judgement, and
`setHeading()` (T84's double-conversion bug, used by `/turn` and `/face`) is
never involved.

T88's both-frames logging confirmed the position frame was correct throughout:
`client_pos=(-1009.00,146.00,51.00) -> server_pos=(146.00,-1009.00,51.00)`,
the documented `m_x = server_y, m_y = server_x` swap.

**Measurement caveat, stated because it initially caught the orchestrator
out**: the comparison must be WillEQ's *transmitted* heading against the
official client's *wire* value. An early pass compared client-frame
`m_heading` against official wire values and produced a spurious "exact
negation" result. The outbound code does apply `server_heading = 90 -
m_heading` (`eq_movement.cpp:1048`); the log line that appears to show a
pass-through is 45 degrees being a fixed point of that reflection.

### What this means

Mapped onto the documented convention the offset is concrete. The official
client's cardinals land on 0 / 512 / 1024 / 1536, exactly matching
`docs/CLAUDE.md`'s 0=North, 128=West, 256=South, 384=East (x4 for 12-bit) —
so the human's facings were true and the constants are sound.

WillEQ moving client **+Y**, which `docs/CLAUDE.md` itself defines as north,
transmits **512 = West**.

Inbound (`m_heading = 90 - server_heading`) and outbound
(`server_heading = 90 - m_heading`) are exact mutual inverses, which is why
T88's round-trip test passes identity. **Being a self-consistent pair does not
make the convention correct** — a self-consistent misinterpretation is
precisely the defect class that no amount of WillEQ-only testing can detect,
and the reason the official-client oracle was built.

### NOT FIXED — this needs Will's decision

Heading code was not touched, per the standing prohibition. T84, T85 and T87
each found real heading problems, reported them and changed nothing; this unit
does the same.

The obvious-looking change (adjust the 90-degree term) is **exactly** the kind
of edit the load-bearing comment in `src/client/eq.cpp` and
`docs/CLAUDE.md`'s Heading/Rotation Conventions warn against. The inbound
offset is hard won, players and NPCs do not share default rotations, and the
model files carry differing ones. A 90-degree correction applied to the wrong
end, or applied to both ends, would break rendering that currently works.

**One assumption remains unverified**: that WillEQ's client +Y is the same
physical direction the human faced as north. The official constants and our
documentation agree that it should be, but this capture cannot prove it
independently. If client +Y is not true north, the 90-degree offset is
expected rather than defective. Resolving it needs one official client and one
WillEQ client facing the same landmark — a short two-client capture.


---

## CORRECTION — the +90° finding above is WRONG. WillEQ's outbound heading is correct.

Prompted by Will asking why the official client's heading could not be
verified: it can, and two ways. He read facings off the official client's
**compass window**, and — needing nothing from him — `Wizzy` moved during the
capture, so his own position deltas give the server-axis-to-heading mapping
empirically.

Derived from T87's `wizzy_clientupdate.csv`, using 83 samples filtered to
sustained translation (>4 units between updates) with a rock-steady heading
(<1° change), displacement-weighted circular mean, **R = 0.957**:

> **official: `heading = 91.39° − travel_angle(server frame)`**

Testing WillEQ's four cardinals against that mapping (client delta → server
delta via the documented `client (x,y) → server (y,x)` swap):

| script label | server travel | predicted | WillEQ sent | diff |
|---|---|---|---|---|
| "north" | 0.0° | 91.39 | 90.00 | −1.39 |
| "west" | 270.0° | 181.39 | 180.00 | −1.39 |
| "south" | 180.0° | 271.39 | 270.00 | −1.39 |
| "east" | 90.0° | 1.39 | 0.00 | −1.39 |

**The difference is identical across all four directions.** A constant
common-mode difference is the signature of a residual in the *reference*
estimate — the human's facing not being perfectly aligned with his direction
of travel — not of error in the thing being measured. WillEQ's relation is
exactly `heading = 90 − travel_angle`, which is the official convention.

### Where the +90° came from

The tour script labelled its moves "north", "west", "south", "east" by
**client-frame axis**. Client +Y is server +X, which is not north. The
comparison above measured WillEQ's travel-derived heading against those
*labels* rather than against a physical direction, and the 90° was the
client-to-server axis rotation showing up as a fake offset.

The labels were wrong. The code was not.

### Consequences

- **The outbound heading convention is CORRECT and needs no change.** Nothing
  should be edited on the strength of the superseded section above.
- The heading defect surfaced by this whole arc is **only** T84's
  `EqActionHandler::setHeading()` double-conversion. That closes the question
  opened at T84.
- The "one unverified assumption" flagged above is resolved, and no further
  capture is needed.

### The methodological lesson, which is the third instance in this arc

T87 computed a mean over unevenly-spaced samples. T86 asserted a defect from a
correct client fed mirrored input. T89 compared against human-assigned labels
instead of measured physical directions. **Each time the measurement was sound
and the frame of reference was not.** The check that works is the one used
here: derive the reference empirically from the independent implementation's
own behaviour, then predict the thing under test.
