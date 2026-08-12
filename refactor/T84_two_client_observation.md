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

Executed in `/mnt/projects/fable/wt/t1`, task branch
`task/T84-two-client-observation`, fable tip `d9b8830f`. **No production
code was touched** — `git status` on the task branch shows a clean working
tree throughout (script/config files used for live driving live under
`data/captures/`, which is gitignored, same as T82/T83's session configs).
Full media, README, and raw logs: `/mnt/projects/fable/review/T84/`.

### Part 0 — two clients coexist, cleanly, and here is exactly how

**Yes, unambiguously.** Two `build-gles2/bin/willeq` processes ran
concurrently against the same eqemu stack for the entire session, one per
account (`willeq_test`/`Testmelee` = A, GM status 200; `willeq_buddy`/
`Testbuddy` = B, status 0) — confirmed live in `ps aux` output mid-session
(both PIDs present, both consuming CPU). **No collision of any kind was
observed**: separate `-c <json>.json` config files, separate `--script`
files, separate stdout log redirects, no shared file writes, no port
conflicts (the server itself fans out to both connections — client-side,
there was nothing to collide over). Each client is driven the same way a
single client always has been — `-c <config>.json --headless -d 4
--log-level=DEBUG --script <file>` for pure log-based measurement, or the
same without `--headless` plus `--dump-frames-dir/--dump-every/
--dump-max-frames` when a screenshot was needed — there is no new
mechanism, just two of the existing one, launched as two separate OS
processes with staggered start times when ordering mattered (Part 2) or a
few seconds apart when it didn't.

**One real trap, not a collision but a footgun**: `--headless` mode never
exits after its `--script` file finishes (it drops into the normal
per-tick loop and idles, still sending/receiving). Every session in this
unit was ended with `kill -TERM`, not by waiting for the process to exit
on its own. This was actually useful for Part 1 (the idling client kept
passively logging broadcasts long after its own script ran out), but it
means a two-client harness script for a future unit needs an explicit
timeout/kill, not an exit-code wait.

**Binary freshness, checked per protocol**: `build-gles2/bin/willeq`'s
pre-session timestamp (16:19) predated this branch's HEAD commit
(16:35:46) — stale by T82/T83's exact pattern. Rebuilt (`cmake --build
build -j24` then `scripts/build-gles2-x86.sh`) before any live
measurement; post-rebuild timestamp (16:48:08) confirmed newer than HEAD.

**Character/account reality vs seeded intent** (per the plan's explicit
caution): `env.sh`'s comment names `Testcaster`+`Testbuddy` as the
two-client pairing, but the DB shows `Testcaster` (id 67) sits at the
exact same zone/coords as `Testmelee` (id 66) in `nektulos` — i.e. the
`willeq_test` account already has three characters (`Testchar`,
`Testmelee`, `Testcaster`), and only `willeq_buddy`/`Testbuddy` (id 68)
is a genuinely separate account. Two characters on the SAME account cannot
both be logged in at once (one world-login session per account) — so the
only real two-*client* pairing available is `Testmelee` (or `Testcaster`)
vs `Testbuddy`, which is what this unit used throughout. Both characters
were relocated (documented writes) into `arena` (zoneidnumber 77, empty
`spawn2`) 10 units apart, because Befallen/Nektulos take 2-4+ minutes to
finish graphics-thread mesh loading per T83's own numbers — arena loads in
single-digit seconds, which made a session with many small live
measurements actually tractable. All position writes restored and
confirmed via `SELECT` at teardown (see below).

### Part 1 — position/heading, the four numbers, and a real finding

Sample: A's own player heading, `/turn <deg>` matrix (0/90/180/270/45),
observed live by B. Because `-d 4` logs **every** entity's `POS S->C`
broadcast (not just self/target — confirmed by source,
`eq_movement.cpp`'s `shouldLogPosition` gate), B's passive DEBUG stream
already contained everything needed with no `/list` polling required.

| commanded | (1) A believes (`m_heading`, self) | (2) A sends (`server_heading` on wire) | (3) server rebroadcasts (B's decoded `heading=`) | (4) B renders (`entity.heading`, same value, no further transform) |
|---|---|---|---|---|
| `/turn 0`   | **0.00°** (displayed) | 90.00° (scaled=512)  | 90.0°  | 90.0°  |
| `/turn 90`  | **128.00°** (NOT 90!) | 322.00° (scaled=1831)| 321.9° | 321.9° |
| `/turn 180` | **256.00°** (NOT 180!)| 194.00° (scaled=1103)| 193.9° | 193.9° |
| `/turn 270` | **384.00°** (NOT 270!)| 66.00° (scaled=375)  | 65.9°  | 65.9°  |
| `/turn 45`  | **64.00°** (NOT 45!)  | 26.00° (scaled=147)  | 25.8°  | 25.8°  |

(`p1_A_turns_session.log` lines 528-1245 for column 1/2 exact source;
`p1_B_observe_session.log`'s `POS S->C spawn_id=37` lines for column 3/4.)

**Columns (2)->(3)->(4) agree with each other to within 0.1-0.2° every
time** — that's 12-bit quantization (360/2048 = 0.1758° per step), not an
error. **The server and B's decode are exactly trustworthy: what A put on
the wire is exactly what B received and exactly what B would render.**

**But column (1) is wrong, and this is a real, live, previously-unmeasured
bug**, found only because a second client existed to confirm the
downstream effect was real and not a self-consistent illusion:
`ActionDispatcher::setHeading()` / `/turn <deg>` / `/face <x y z>` all
route through `EqActionHandler::setHeading()`
(`src/client/eq_action_handler.cpp:86-92`):
```cpp
void EqActionHandler::setHeading(float heading) {
    // Convert from degrees (0-360) to EQ heading units (0-512)
    float eqHeading = heading * 512.0f / 360.0f;
    ...
    m_eq.SetHeading(eqHeading);   // stores the *converted* value
    ...
}
```
`EverQuest::SetHeading()` (`eq.cpp:3133`) just assigns `m_heading =
heading` with no further conversion — and **every other site that reads
`m_heading`** (zone-in PlayerProfile handling, the movement-send code in
`eq_movement.cpp` that produced column 2 above, the self `ClientUpdate`
echo path) treats it as a plain 0-360 degree value. `EqActionHandler::
setHeading()` is the one write site that stores a 512-unit value into a
field everyone else reads as degrees — the observed 90->128,
180->256, 270->384(=24 mod 360), 45->64 numbers are each commanded-degrees
* (512/360) = 1.4222, exactly the conversion factor, applied where it
should not be. **Net live effect: `/turn 90` (intended: face East) put a
heading on the wire that B decoded as 321.9°, nowhere near East.** `/face`
(`cmdFace` -> `ActionDispatcher::faceLocation`/`faceEntity` ->
`m_handler->setHeading()`) shares the identical function and is affected
identically by direct code inspection — **not independently re-run live
this session** (time-boxed), but there is no code-path difference that
would spare it. This directly qualifies T82's "`/face` CONFIRMED WORKING"
claim: T82 measured that the command *dispatched* and *pointed the client
at the right formula inputs* (`outcome=applied`), which is true and still
stands, but never measured the *resulting orientation value* against an
observer — this unit's whole thesis. **This is a genuine, reportable bug
in `src/client/eq_action_handler.cpp` — not touched, per the plan's
blanket prohibition on heading/rotation code.**

**This is NOT the documented player/NPC asymmetry** (`eq.cpp`'s `90 -
server_heading_deg` flip for the player's own zone-in/self-echo heading,
vs `eq_entities.cpp`/`eq_movement.cpp`'s direct-no-flip formula for every
other entity). That asymmetry was measured this unit too (next section)
and is **exactly self-consistent with itself and with the wire** — no
inconsistency found there. The bug above is a **third, independent**
conversion site that nobody had reason to compare against the other two
until a second client made the downstream effect externally visible.

### Part 2 — login order, exercised both ways, live regression proof for T83

**A-first, B-second** (`p2a_A_first_session.log` / `p2a_B_second_session.log`):
- B (joined second) received A via the **bulk `ZoneSpawns`** path:
  `Loaded spawn 6: Testmelee (ID: 32)... at (-1009.00, 146.00, 51.00)`.
  Ground truth (DB, arena, pre-swap): A at `x=146,y=-1009,z=51`. Matches
  exactly after the documented X/Y swap.
- A (joined first) received B via the **single `NewSpawn`** path (T83's
  fixed path): `New spawn: Testbuddy (ID: 33)... at (-1009.00, 156.00,
  51.00)`. Ground truth: B at `x=156,y=-1009,z=51`. Matches exactly.

**B-first, A-second** (`p2b_B_first_session.log` / `p2b_A_second_session.log`):
- B (joined first) received A via **`NewSpawn`**: `New spawn: Testmelee
  (ID: 35)... at (-1009.00, 146.00, 51.00)`. Matches DB exactly.
- A (joined second) received B via bulk **`ZoneSpawns`**: `Loaded spawn 8:
  Testbuddy (ID: 34)... at (-1009.00, 156.00, 51.00)`. Matches DB exactly.

**Both directions, both decode paths, exact agreement with the DB every
time.** This is the live regression proof the plan asked for: T83's
`/8.0f` fix on `ZoneProcessNewSpawn` holds under the exact asymmetric
login-order stress this unit was designed to apply, in both orderings,
against a real second observer, not a log self-report.

One un-chased oddity, noted not investigated (out of Part 2's scope, time-
boxed): in the B-first run, two of arena's pre-existing `NewSpawn` NPCs
(`a_rat019`, `a_rat020`) logged positions that looked anomalous —
`a_rat020` at `(0.00, 0.00, 0.00)` and `a_rat019` at `(146.00, -1009.00,
51.00)` (the latter NOT X/Y-swapped relative to every other entity in the
same dump). Format strings were checked and are identical between the
swapped and unswapped-looking lines (`eq_entities.cpp:446` vs `:1247`,
both print `entity.x, entity.y, entity.z` from the same struct field
order), so this is either a genuine per-spawn oddity in the server's own
`a_rat019`/`020` data or a decode edge case specific to those two spawns
— flagged, not chased, since it's outside what Part 2 was measuring
(A/B's mutual view of each other, which was clean).

### Part 3 — orientation table

| entity type | model default rotation | server sends (raw, decoded to °) | code applies | observer (B) renders |
|---|---|---|---|---|
| A's own player, self path (PlayerProfile zone-in) | not measured — no WLD-asset-inspection tooling built this unit, see below | e.g. `heading=36.75°` (zone-in echo of prior DB-persisted heading) | `m_heading = 90 - server_heading_deg` (`eq.cpp:2174`, **the deliberate, documented, UNTOUCHED flip**) | n/a — this is what A believes about itself, not what B sees |
| A's own player, self `ClientUpdate` echo | same, unmeasured | matches PlayerProfile formula | `m_heading = 90 - server_h` (`eq_entities.cpp:1437`) — same flip, same site family | n/a |
| **A, a player character, as seen by B** | not measured | raw 11/12-bit heading, e.g. 90.0/322.0/194.0/66.0/26.0° (Part 1's matrix) | **direct, no flip**: `entity_heading = h_player` in `eq_movement.cpp:1614` (ClientUpdate) and `eq_entities.cpp:296`/`:1106` (ZoneSpawns/NewSpawn) — all three non-self sites use the identical `raw*360/2048` formula, confirmed algebraically identical between the `/4` form (ZoneSpawns) and the direct-2048 form (NewSpawn) | same value, no further transform (`OnSpawnMovedGraphics` receives `entity_heading` unchanged) |
| NPC, `a_training_dummy000` (race 75) | not measured | `heading=2.1°` | same direct formula as above | same value |
| NPC, `DPS_Machine001` (race 75, different instance, same race) | not measured | `heading=4.4°` (different from the dummy above — real per-instance data, not a copy-paste artifact) | same direct formula | same value |
| NPC, `a_rat005` (race 415) | not measured | `heading=179.3°` | same direct formula | same value |
| NPC, `Soulbinder_Gendal000` (race 367, a humanoid/player-model race) | not measured | `heading=0.0°` | same direct formula | same value |

(Source: `p1_B_observe_session.log`'s `POS S->C spawn_id=N ... heading=`
lines, matched to `Loaded spawn N: <name> ... Race R` lines from the same
log's `ZoneSpawns` dump for name/race.)

**Conclusion: the documented player/NPC asymmetry (`eq.cpp` flip for self
only, `eq_entities.cpp`/`eq_movement.cpp` direct formula for literally
everyone else — players and every NPC race sampled alike) is measured,
live, and self-consistent. No inconsistency found in it.** The genuine bug
this unit found (Part 1) lives in a different file entirely
(`eq_action_handler.cpp`) and was never part of the documented asymmetry.

**Gap, honestly flagged**: the "model default rotation" column could not
be filled in. Doing so needs per-`.wld`-asset default-orientation
extraction, which has no existing tool in this codebase and is squarely
outside a log/wire measurement unit's scope — reported as a real gap for
whichever future unit needs it, not fabricated.

### Part 4 — event propagation, per-event, observer-confirmed

| event | command issued (by A) | B observed? | evidence |
|---|---|---|---|
| chat (say) | `/say Hello from A, this is a chat propagation test` | **YES** | B's log: `[CHAT] Testmelee: Hello from A...` at 17:25:55.627, ~50ms after A's own send (17:25:55.577) |
| sit | `/sit` | **YES** | B's log: `SpawnAppearance spawn_id=39 type=14 parameter=110` -> `set animation 'p02' pose=1` |
| stand | `/stand` | **YES** | B's log: `SpawnAppearance spawn_id=39 type=14 parameter=100` -> `set animation 'o01' pose=0` |
| group invite | `/invite Testbuddy` | **YES** | B's log: `Group invite received: Testmelee invited Testbuddy` / `[SYS] Testmelee has invited you to join a group` |
| group accept | `/follow` (B, empty args) | **NOT EXERCISED** — timing mistake in this session's own script (B's `/follow` ran at 17:23:59, ~2 minutes BEFORE A even connected/invited at 17:26:04) — the command hit `"Usage: /follow <entity_name>"`, T82's already-documented registry-1-shadowing (empty-arg accept-invite only lives in `CommandProcessor::cmdFollow`, registry 2, which registry 1's own same-named `follow` always wins over) rather than a fresh finding | `p4_B_events_session.log` line ~608-610 |
| movement (walk, smooth vs teleport) | `/moveto a_rat000` | **NOT MEANINGFULLY EXERCISED** — `a_rat000` happened to sit at A's own exact spawn point (0.0 units away per A's own `/list`), so `/moveto` had nothing to walk toward; B's log shows A's position genuinely unchanged (`pos=(-1009.00, 146.00, 51.00)` before and after) — a test-design gap (bad target choice), not a propagation finding either way | `p4_A_events_session.log` line 601; `p4_B_events_session.log` lines 1019/1048 |
| animation: attack swing | not attempted | — | time-boxed out; would need a live target + weapon setup this session didn't build |
| appearance: equip visible weapon | not attempted | — | time-boxed out for the same reason |

**Three of five attempted events propagated cleanly and were confirmed by
the observer, not an ack** (chat, sit, stand, group-invite — four,
correcting the count: chat/sit/stand/invite all confirmed). **Two were not
meaningfully exercised due to this session's own scripting mistakes**
(mistimed accept, badly-chosen move target) — reported honestly as gaps in
this session's test design, not as propagation failures, since neither
command was ever actually given a fair chance to propagate or not.

### Anything believed wrong but not touched

- **`src/client/eq_action_handler.cpp:86-92`, `EqActionHandler::
  setHeading()`** — the double-conversion bug described in Part 1. This is
  the unit's headline finding. Not touched, per the plan's blanket
  heading/rotation prohibition.
- The `a_rat019`/`a_rat020` NewSpawn position oddity noted in Part 2 —
  flagged, not chased, evidence preserved in `p2b_A_second_session.log`.
- The large pale-blue wedge-shaped render artifact visible in
  `frames_B_testbuddy_pov/frame_000005.png` (see `review/T84/README.md`)
  — described, not diagnosed; could be a mis-clipped wall polygon or a
  camera-angle artifact of arena's boundary geometry, not investigated
  further (outside this unit's scope, which is measurement not rendering
  diagnosis).

### Build/test

1. `cmake --build build -j24` — clean (no production code changed; this
   confirms the pre-existing tree still builds, not a fix verification).
2. `ctest --test-dir build --output-on-failure` — **2066/2067**, sole
   failure `AudioManagerTest.PreloadCommonSounds` (documented host ALSA
   exception). `visual_gate` (test #2067) — PASS.
3. `bash scripts/build-gles2-x86.sh` — clean (also the binary used for all
   live measurement above).
4. Visual gate — PASS, goldens untouched.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile, `=== Build
   complete ===`.
6. `check_file_sizes.sh fable` — **"no C++ files to check... nothing was
   checked, this is not a pass."** This is the expected, correct output
   for a unit with zero production-code changes (confirmed via `git
   status` — clean working tree on the task branch) — not a failed check,
   a check with nothing to check.

### Teardown

- All four characters' `zone_id`/`x`/`y`/`z`/`heading` restored to their
  documented/original baselines and confirmed via `SELECT`:
  - `Testchar` (65): never touched this unit — unchanged throughout
    (`zone_id=36, x=-77.875, y=21.875, z=3, heading=190`).
  - `Testmelee` (66): moved `nektulos`->`arena` and back — restored to
    `zone_id=25, x=-259, y=-1201, z=-5.75, heading=160.25` (its original
    value, confirmed via a pre-move `SELECT` at session start).
  - `Testcaster` (67): never touched — unchanged throughout.
  - `Testbuddy` (68): moved `tutorialb`->`arena` and back — restored to
    `zone_id=189, x=18, y=-147, z=18.3424, heading=0` (original value).
- `scripts/eqemu/down.sh` run — clean container teardown, confirmed via
  its own output (all `akk-stack-*` containers stopped/removed).
- Lock file `/mnt/projects/fable/.eqemu-lock` deleted, confirmed via `ls`
  failing (`No such file or directory`).

### Deviations from the plan

- Part 1's matrix used `/turn <deg>` (a direct, simple, known-input
  command) rather than trying to force an exact server-side heading via a
  GM `#goto`-style command with a heading argument — this was the right
  call in hindsight, since it's exactly what surfaced the
  `EqActionHandler::setHeading()` bug; a GM-forced heading would have
  bypassed the buggy client-side conversion entirely and hidden the
  finding.
- Part 3's "model default rotation" column could not be filled in — no
  tooling exists in this codebase to extract per-`.wld` default
  orientations; reported as a gap, not fabricated.
- Part 4: group-accept and movement-smoothness were not meaningfully
  exercised due to this session's own scripting/timing mistakes (see
  table); attack-swing and equip-visibility were time-boxed out entirely
  and never attempted. Reported as gaps rather than silently dropped.
- The plan asked for "the same moment" paired captures; the two capture
  processes are separate OS processes launched a few seconds apart with no
  cross-process sync point, so the pairing is "same settled static scene,
  each side's own last frame of its own scripted window," not
  frame-synchronized to an identical server tick — see `review/T84/
  README.md` for the exact caveat.
- Nektulos (the zone `Testmelee`/`Testcaster` are normally parked in) was
  abandoned in favor of `arena` for all live two-client work after a
  throwaway timing test showed nektulos's graphics-thread zone-geometry
  load alone takes 4+ minutes (458 entities) on this llvmpipe path — far
  too slow for a session needing many short live measurements. This is
  consistent with, and reinforces, T83's zone-load-time findings; not a
  new defect, just a scheduling constraint this unit worked around.

## Board

**Two-client harness, now proven and available for reuse**: drive two
`build-gles2/bin/willeq` processes concurrently via `-c <config>.json
--headless -d 4 --log-level=DEBUG --script <file>`, one process per
account (only `willeq_test` and `willeq_buddy` are independently
loggable-in — `willeq_test`'s own `Testchar`/`Testmelee`/`Testcaster` share
one account and only one can be connected at a time). Poll the first
client's own log for `"Fully connected to zone"` before launching the
second when login order matters (Part 2's technique); otherwise a few
seconds' stagger is enough. **`--headless` never exits on its own after
its `--script` finishes** — always `kill -TERM` the process rather than
waiting on it. `arena` (zoneidnumber 77) is a good default zone for
two-client work needing many short iterations — empty `spawn2`, loads in
single-digit seconds, unlike Nektulos/Befallen's 2-4+ minute graphics
loads. Example configs: `data/captures/t84_a_testmelee_willeq.json` /
`t84_b_testbuddy_willeq.json` (gitignored, but the pattern is now
documented here for the next unit to copy). At `-d 4`, `POS S->C` DEBUG
lines are logged for **every** entity's position/heading broadcast, not
just self/target — a passive two-client session captures orientation data
for free with no need to script `/list` polling.

**Headline finding for follow-up**: `src/client/eq_action_handler.cpp`'s
`setHeading()` double-converts degrees to EQ's 0-512 range and stores the
converted value into a field every other site treats as plain degrees —
affects `/turn` and (by shared code path, not independently re-verified
live) `/face`. Real, live, reproducible, unfixed. Added to
`refactor/deferred.md`.
