# T85 — Join the official-client baseline capture as a third client

**Slot: t1. Live session. x86 only, no Pi.**

**THIS UNIT DOES NOT OWN EQEMU. Another session owns it and a capture is
live. Read "Lock and teardown — READ FIRST" before anything else; it
inverts the standard protocol and getting it wrong destroys someone else's
work.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`/mnt/projects/eq-oracle/notes/willeq-session-handoff.md` (the join spec from
the oracle session), then T84's Review, then this plan.

## Lock and teardown — READ FIRST

The eqemu server is up and **owned by a different session**
(`H05-tier0-official-client-oracle`), which holds
`/mnt/projects/fable/.eqemu-lock` deliberately so multiple clients can share
one capture. A human is driving an official EverQuest client through a
scripted routine while a full pcap of login/world/zone traffic runs.

Therefore, inverting the usual rules:

- **DO NOT acquire the lock. DO NOT create, modify, or delete it.**
- **DO NOT run `scripts/eqemu/up.sh` or `scripts/eqemu/down.sh`.**
- **DO NOT restart world or zone processes.** That breaks the capture's
  continuity and the running order stops lining up.
- **DO NOT tear anything down at the end.** Exit your own WillEQ process and
  stop. Leave the server exactly as you found it.

If the server appears down or unreachable, **STOP and report** — do not bring
it up.

You are a guest in someone else's session. The only thing you own is your own
WillEQ process.

## Connection details

From the join spec. Use **`willeq_a` and nothing else** — concurrency on this
server is per-account, and other accounts are in use by the official clients.

| | |
|---|---|
| host | `10.0.30.23` (or `127.0.0.1`, you are on the same VM) |
| login port | `5998` |
| server long name | `WillEQ` |
| account | `willeq_a` |
| password | `WillEqH03Pass` |
| character | `Oraclealpha` (Human male Warrior, level 1) |
| starting zone | South Qeynos (zone 1), already there — no travel needed |

Config shape (see `data/captures/tour_willeq.json` for a working example):

```json
{ "clients": [ { "host": "...", "port": 5998, "user": "willeq_a",
  "pass": "WillEqH03Pass", "server": "WillEQ", "character": "Oraclealpha",
  "eq_client_path": "/mnt/projects/EverQuestP1999" } ] }
```

**Other clients in the zone — do not disturb them:**
- `Wizzy` (`hc_test1`) — the official ACTOR, driven by a human
- `Oracledelta` (`willeq_d`) — the official OBSERVER, parked at
  `zone 1, x=-653, y=314, z=3.75`
- `Oraclecharlie` (`willeq_c`) — **pristine control. Never touch it.**

Park near the official observer's coordinates.

## LOG IN EARLY — this is a real risk, not a nicety

WillEQ still builds meshes for **every entity in the zone at load time with no
distance gate** (the unfixed prepare-all-entities defect; `nektulos` took
~230s in T82 and never finished). South Qeynos is populated.

**Get logged in, zoned, settled and holding well before the routine starts.**
If your load runs into the human's cardinal sequence, the timeline alignment
is ruined and it cannot be redone without re-playing the whole session.

Report your **actual login wall-clock time** and the wall-clock of every
action you take — the oracle session marks its timeline from these, and a
capture that cannot be segmented is worth a fraction of one that can.

Run with `--record-session <path>.sleq` so the comparison is replayable
(`data/captures/run_tour.sh` is a worked invocation), and `-d 4` for command
output. Note `--headless` does not exit after `--script` finishes (T84) —
plan to `kill -TERM`.

## Role 1 — WillEQ as OBSERVER (tests our decode, inbound)

**Do this first, and stay still.** While the official actor runs its
cardinal-heading routine, record what WillEQ *believes* about the others:

- decoded **position and heading** for `Wizzy` and `Oracledelta`, sampled
  while they are stationary;
- the **rendered** orientation — screenshot it, and describe what you actually
  see rather than what you expect.

This is the cell that catches a self-consistent misinterpretation: two
official clients agree with each other, so **any disagreement is ours**.

## Role 2 — WillEQ as ACTOR, and it must be TWO PASSES

Only after the official actor has finished. This is the part where the obvious
approach would waste the opportunity.

**T84 established that `EqActionHandler::setHeading()`
(`src/client/eq_action_handler.cpp:87-93`) double-converts** — it turns degrees
into EQ 512-units and hands the converted value to `EverQuest::SetHeading()`,
which stores it raw into `m_heading`, a field treated as degrees everywhere
else. `/face` shares that function. So a routine driven by `/turn` or `/face`
will send a heading we *already know* is wrong, the official observer will
faithfully record it as wrong, and we will have learned nothing new.

The open question is different: **is our wire encoding correct when the
internal value is correct?** Those are two separate defects and one pass
cannot distinguish them.

So run the routine **twice**:

- **Pass A — bypass the bug.** Turn using actual rotation/movement input, NOT
  the slash command, so `m_heading` holds a correct degree value. Establish
  and state how you verified the internal value was correct before sending.
- **Pass B — through `/turn`.** The known-buggy path, for contrast.

Each pass: `/say facing north`, face north, hold still ~5s; repeat for east,
south, west, plus a couple of intermediate angles. Announce before turning —
the `/say` timestamps are what segment the capture.

**The deliverable is the difference between the passes.** If both are wrong
identically, the encoder is sound and this is purely T84's bug. If Pass A is
*also* wrong, there is a second defect beneath it — and that is a finding this
project has no other way to reach.

## What you may not do

- **NO production code changes.** None. This unit measures.
- **Do not touch heading, rotation, or coordinate-conversion code**, even if
  certain it is wrong. The `90 - server_heading_deg` player offset is
  deliberate and hard won — see the load-bearing comment in `src/client/eq.cpp`
  and `docs/CLAUDE.md`'s Heading/Rotation Conventions, and read both before
  forming an opinion. T84 found a real heading bug, reported it, and changed
  nothing; do the same.
- **Do not infer an effect from an ack.** `outcome=applied` means a handler
  returned success and nothing more. **Here the official client is the
  oracle** — if it did not see it, it did not happen.
- **Open every image you produce and describe what is actually in it.**
  Describe; do not grade.
- Do not interact with, target, follow, or move the other characters.

## Verification

- Build + full suite afterward to prove nothing broke (expect 2066/2067; sole
  failure is the documented ALSA `AudioManagerTest.PreloadCommonSounds`).
- **Verify binary freshness before drawing any live conclusion** — check the
  binary timestamp against HEAD and rebuild if stale. This has cost two units
  already.
- Media to `/mnt/projects/fable/review/T85/` with a README.
- Leave your `.sleq` where the oracle session can find it and say where it is.

## Review

(executor fills: login wall-clock and per-action timeline; Role 1's decoded
position/heading for both official characters and what was rendered; Role 2
Pass A vs Pass B and the difference between them; whether the internal heading
value was verified correct before Pass A; anything believed wrong but not
touched; confirmation that the lock and server were left untouched; deviations)

## Board

Record what the three-client arrangement makes possible, and hand the oracle
session the `.sleq` path plus the action timeline.
