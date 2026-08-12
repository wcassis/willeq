# T84 — Two clients, one observing: break the single-client circularity

**Slot: t1. OWNS EQEMU (exclusive lock — cannot overlap T83). x86 only, no Pi.**

**Territory: `refactor/`, `scripts/eqemu/`, `/mnt/projects/fable/review/T84/`,
throwaway session scripts. PRODUCTION CODE IS READ-ONLY. This unit measures.
See "What you may not do" — it is not boilerplate.**

Read `refactor/EXECUTOR_PROTOCOL.md` (eqemu section — lock first; daylight
directive; media section), then `docs/CLAUDE.md`'s **Heading/Rotation
Conventions** section in full, then T83's Review, then this plan.

## Why this unit exists

**Every live session WillEQ has ever run used a single client.** That makes
live verification circular: we ask the client whether the client thinks the
command worked. T82 is the worked example — it concluded `/face` succeeded
from an `outcome=applied` log line while every entity in the zone sat at
distance zero from the player. The client agreed with itself and was wrong.

Many EQ events travel **client -> server -> nearby clients**, and the server
**silently drops or transforms bad client input before rebroadcasting it**.
So a message that fails to reach a nearby observer is strong evidence that
what we sent the server was malformed. That is a check a single client
structurally cannot perform.

The official Titanium client cannot be automated. A second willeq instance
can. That is the whole idea.

**The infrastructure already exists and has never been used.**
`scripts/eqemu/env.sh:~25` seeds a second account for exactly this —
"two-client tours (trade/group/duel)" — with `Testcaster` on `willeq_test`
and `Testbuddy` on `willeq_buddy`. Confirm what actually exists in the DB
before assuming; seeded intent and seeded reality are not the same thing.

## Part 0 — Stand up two live clients at once

Nobody has done this. Prove it works before anything else, and report the
mechanics honestly — this is a deliverable in its own right, because every
future two-client unit depends on it.

- Two headless instances, same server, **same zone, in visual range of each
  other**. Each ran ~1.3-1.5GB RSS in T82; the host has 24 cores.
- Establish how each is driven and logged independently (separate
  `--script`, separate log files, `-d 4` for command output — note
  `--log-level=INFO` did NOT surface chat/system output in T82).
- **Verify binary freshness first.** T82 lost a pass to a stale
  `build-gles2/bin/willeq` predating three merged units, which silently fell
  through to `Unknown command`. Check the binary's timestamp against HEAD.
- Record any collision between the two instances: config paths, screenshot
  paths, session recordings, ports, lock files, character-in-use conflicts.

**If two instances cannot coexist, that is the finding** — report exactly
where it breaks and stop. Do not spend the session fighting it.

## Part 1 — Position and heading, as seen by the observer

The core question: **does what willeq believes about itself match what the
server tells everyone else about it?**

For a matrix of positions and headings — including at least the cardinals
(N/E/S/W) and a few intermediate angles — have client A adopt a known state,
then record, from **client B's** point of view, where and how A appears.

For each sample, state all four numbers side by side:
1. what A believes its own position/heading is (A's own log)
2. what A sends on the wire (outbound position update)
3. what the server rebroadcasts (B's inbound packet)
4. what B decodes and renders for A

**Do the same in reverse** (B acts, A observes) — the two directions are not
symmetric, see Part 2.

**Also capture what each client renders**, not just what it logs. An
orientation error is visible: two clients facing each other should each see
the other facing them.

## Part 2 — Exploit login order (this is the sharp instrument)

Login order exercises the two spawn-decode paths **asymmetrically against
the same pair of characters**:

- The client that logs in **second** sees the first through the bulk
  `ZoneSpawns` path.
- The client that logs in **first** sees the second arrive through the
  single `NewSpawn` path.

T83 is investigating a divergence between exactly those two paths
(`eq_entities.cpp:281` divides `z_raw` by `8.0f`; `eq_entities.cpp:1083` does
not). **So park two characters in the same spot and have each report where it
thinks the other is.** If the paths disagree, the two clients disagree about
each other — and you read the bug straight off with no packet decoding at all.

Run it **both ways** (A first then B, then the reverse) and report both. If
T83 already fixed the divergence, this becomes the regression test that
proves it — say so, and keep the evidence.

## Part 3 — Orientation: characterize, do not fix

**Read the load-bearing comment in `src/client/eq.cpp` (PlayerProfile
heading) and the Heading/Rotation Conventions section of `docs/CLAUDE.md`
before you form any opinion here.**

The player's own heading gets a `90 - server_heading_deg` flip; other
entities' headings do not, and take an extra `/4.0f` instead. **This
asymmetry is deliberate and hard won.** Players and NPCs do not share the
same default rotation, and the model files themselves carry differing default
rotations, so the code difference is compensating for a real asset
difference.

Your job is to produce the table nobody has, **not** to change anything:

| entity type | model default rotation | server sends | we apply | observer renders |

Cover at minimum: the observing client's own player, another *player*
character, and a couple of NPCs of different races. Note explicitly where a
model's own default rotation differs from another's.

**If you believe you have found an inconsistency, write it up as a question
with evidence. Do not act on it.** The instinct to unify these sites is
exactly the instinct that would destroy working code here.

## Part 4 — Event propagation spot-check

Cheap additions now that two clients exist. For each, confirm the observer
sees it, and report any that do not propagate:

- chat (say / shout / ooc / tell)
- movement (does A walking show as smooth motion to B, or teleporting?)
- animations: attack swing, casting, sitting/standing
- appearance: equipping a visible weapon — does B see it on A?
- group invite/accept, if it works without excessive setup

**A silent non-propagation is a finding, not a footnote** — it means the
server rejected or transformed what we sent. Report each one with the exact
command issued and what the observer did and did not see.

## What you may not do

- **No production code changes.** None. This unit's output is a measurement
  table and a set of findings.
- **Do not touch heading, rotation, or coordinate conversion code**, even if
  you are confident. That is a separate unit with its own review, and Part 3
  exists to inform it.
- **Do not infer an effect from an ack.** `outcome=applied` means a handler
  returned success, nothing more — the comment above the emitter in
  `src/client/application.cpp:~1180` says so explicitly. **In this unit the
  observer is the oracle.** If B did not see it, it did not happen, whatever
  A's log says.
- **Open every image you produce and describe what is actually in it.**
  T82's README claimed "fully-loaded indoor zone (textured floor/wall
  geometry)" for frames containing no ground at all. Describe; do not grade.

## Verification

- Build + full suite to prove nothing broke (expect 2066/2067; sole failure
  is the documented ALSA `AudioManagerTest.PreloadCommonSounds`).
- Restore any DB state and confirm with `SELECT`. Clean teardown
  (`scripts/eqemu/down.sh`), lock deleted — even if the unit goes badly.
- Media to `/mnt/projects/fable/review/T84/` with a README. **Paired
  captures**: the same moment from both clients' points of view, side by
  side, clearly labelled which is which.

## Review

(executor fills: whether two instances coexist and exactly how; the
four-number position/heading comparison per sample; the login-order result in
both directions; the orientation table; which events propagated and which
did not; anything you believe is wrong but did not touch; teardown
confirmation; deviations)

## Board

Record the two-client harness as available infrastructure with a pointer to
how it is driven, so the next unit does not rediscover it — `env.sh` has had
a second account seeded for this purpose since before T54 and nothing ever
used it.
