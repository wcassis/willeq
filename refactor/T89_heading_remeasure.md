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
