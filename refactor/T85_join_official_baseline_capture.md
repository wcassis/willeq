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

Executed in `/mnt/projects/fable/wt/t1`, task branch
`task/T85-join-baseline-capture`, HEAD `8641adff`. **No production code was
touched.** Media, screenshots, wire-evidence and README:
`/mnt/projects/fable/review/T85/`. All `.sleq`/`-d4` logs live in
`data/captures/` in the worktree (gitignored; several are 10-36MB).

### Binary freshness

`build/bin/willeq` was stale at session start (16:44, HEAD at 23:57) —
rebuilt (`cmake --build build -j24`) before any live use; confirmed newer
(00:02) than HEAD before connecting. Used the full GLES2 test tree
(`build/`) throughout rather than also rebuilding the smaller
`build-gles2/` tree — a deliberate choice under time pressure (a human's
live capture was running) since both trees use the identical GLES2
render path; flagged to and accepted by the coordinator mid-session.

### Login and full action timeline (all times UTC, 2026-08-13)

| time | action |
|---|---|
| 00:09:07 | process 1 launched (`t85_session.sleq` / `t85_role1_run.log`) |
| 00:09:10.173 | **South Qeynos zone-in confirmed** (PlayerProfile received) |
| 00:09:11.988 | `/say` arrival announcement |
| 00:09:14.019-00:09:36.891 | `/move` to park near Oracledelta (-650,310,3.75) |
| 00:09:17-00:16:14 | Role 1 periodic screenshots (session 1) |
| 00:13:51.549 | `Wizzy` (spawn 462) spawns into view |
| 00:19:01.320 | session 1 script ran dry, clean `Application shutdown complete` (self-inflicted: under-padded script — corrected per coordinator instruction to pad scripts far longer than estimated) |
| 00:19:01-00:19:15 | **~14s gap — WillEQ off the wire. Genuine, undocumented-until-now coverage loss, not recoverable.** |
| 00:19:15 | process 2 launched (`t85_role1b_session.sleq`) |
| 00:19:15-00:24:51.320 | session 2, continued Role 1 observation |
| 00:24:48 | process 3 launched *before* killing process 2 (overlap, not gap) |
| 00:24:51.320 | process 2 shut down (server-forced, duplicate-account login) |
| 00:24:51.689 | process 3 zoned in — **~0.4s handover, negligible** |
| 00:24:51-00:41:55 | session 3, extended Role 1 observation + GM relog + qeynos2 detour (see below) |
| 00:41:55 | process 3 killed (`kill -TERM`) for a clean GM-flag relog, per Will's direct instruction (GM status had just been granted server-side) |
| 00:41:55.947 | shutdown confirmed |
| 00:42:57 | process 4 launched |
| 00:43:00.155 | South Qeynos zone-in |
| 00:43:03.574 | `/say #zone qeynos2` sent (**too early** — before confirmed zone-in per the arrival screenshot showing "Entering world... 50%"; landed anyway, but by luck, corrected in the next session) |
| 00:43:05.223 | North Qeynos (qeynos2) zone-in |
| 00:47:14 | first combat chat observed (ambient: Corporal Lancot kills Fippy Darkpaw) |
| 00:52:55 | (coordinator-confirmed) WillEQ observing Wizzy's combat correctly via chat/combat log |
| 00:54:15 | process 4 killed for Role 2 |
| 00:54:54 | process 5 launched (Role 2) |
| 00:54:57.127 | zone-in (qeynos2, the persisted zone from process 4) |
| 00:55:00.083 | `/say #zone qeynos` sent — **deliberate**, to reach Oracledelta's parked location in South Qeynos for Role 2 (not a lost-persistence bug; see Deviations) |
| 00:55:01.481 | South Qeynos zone-in |
| 00:55:12-00:56:16 | **Role 2 Pass A** (movement-driven), 6 directions |
| 00:56:19-00:57:16 | **Role 2 Pass B** (`/turn`-driven), 6 directions |
| 00:58:46 | Role 2 script idle-padding, process 5 killed |
| 00:58:59 | process 6 launched, this time waiting for confirmed zone-in before zoning |
| 00:59:02.571 | South Qeynos zone-in **confirmed** |
| 00:59:18.563 | `/say #zone qeynos2` sent (16s after confirmed zone-in, corrected timing) |
| 00:59:20.111 | North Qeynos zone-in confirmed (~1.5s transfer) |
| 00:59:20-01:04:15 | held near Oracledelta's qeynos2 spawn point, combat chat/log observed (`Wizzy says, 'starting combat'` / `'finished'`, kills a grass snake, loot drops) |
| 01:04:15 | `kill -TERM` sent |
| 01:04:20.399 | **final clean shutdown confirmed** |

### Role 1 — WillEQ as observer

**Oracledelta** (spawn_id 439), decoded live and stationary throughout:
pos=(314.00, -653.00, 3.75), heading=270.0° — matches the join spec's
parked coordinates exactly. Confirmed both via `-d4`'s `POS S->C
spawn_id=439` line and via the raw `.sleq` `OP_ClientUpdate` record at
the same timestamp.

**Wizzy** (spawn_id 462): a single decode at spawn/zone-sync time —
pos=(313.62, -658.12, 3.75), heading=84.6°/83.7° (varies slightly between
sessions' zone-sync echoes) — and then **nothing further for ~28 minutes
of live observation** despite Will's confirmation that he was actively
moving and turning through his cardinal-heading routine (steps 1-4)
during that exact window. This is the headline finding of Role 1 — see
"The Wizzy question" below.

**Rendered**: in South Qeynos, all screenshots (`role1_*.png`,
`role1b_*.png`, `role1c_*.png`) show night-time (daylight was never
forced this session — a real, admitted deviation from
`EXECUTOR_PROTOCOL.md`'s mandatory step, see Deviations) with a
purple/black checkerboard ground pattern (missing-texture fallback) and
no visible zone geometry. In North Qeynos, the frame is dominated by a
flat magenta or dark-blue/red checkerboard/flat-color fallback covering
most surfaces (`role1_qeynos2_t300/t600.png`, `role3_qeynos2_*.png`) —
described in full in `review/T85/README.md`. **Combat log observation
was sound in both zones** (chat/combat lines for both ambient NPC fights
and Wizzy's own combat arrived correctly and in order); **combat visual
observation in qeynos2 could not support any conclusion** about
animation or weapon appearance — the frame is too texture-broken to
distinguish a rendering bug from a real animation.

### The Wizzy question — settled from the wire, not speculated

Parsed the raw `.sleq` captures directly (binary format,
`include/common/net/session_log.h`; one-off parser at
`review/T85/parse_sleq.py`, full output in
`review/T85/wizzy_wire_evidence.txt`), independent of any `-d`/log-level
application gating. Across sessions 1-3 (~00:09:07-00:41:55, spanning
Wizzy's spawn at 00:13:51 through the end of the long observation
window — ~28 minutes with Wizzy present and, per Will, actively moving):

- `OP_ClientUpdate` (0x14cb) records for spawn_id 462 (Wizzy): **exactly
  one per zone-(re)session**, always inside the first ~4 seconds (the
  bulk zone-sync echo), **zero thereafter**.
- In the identical window, other spawn_ids (460=self, 40, 32, 42, 172,
  448, 450, 453, ...) produced dozens to hundreds of `OP_ClientUpdate`
  records each — proof the opcode path, the capture, and the decode
  handler (`eq_entities.cpp` `ProcessClientUpdate`) were all live and
  working, not silently broken.
- The `-d4`/DEBUG application log's `POS S->C spawn_id=` line (gated
  only by `s_debug_level>=2`, satisfied by `-d4`) matches the `.sleq`
  1:1 — one line for 462 per session, no more.

**Conclusion: this is not a WillEQ decode or logging failure — the
packets for Wizzy simply never arrived on `willeq_a`'s wire after the
initial echo.** Why the server didn't forward them is outside what a
client-side capture can determine; candidate causes (distance/interest-
based server-side broadcast filtering, GM-flag-related suppression on
`hc_test1`, or a subscription issue specific to third-party-account
visibility) are unconfirmed hypotheses, not established causes. **This
needs the oracle session's server-side pcap to settle**: does the
server's own capture show `OP_ClientUpdate` broadcasts for spawn 462
going out toward `willeq_a`'s socket during this window, or not at all?
If the server-side pcap shows them going out and WillEQ still didn't log
them, that would flip this conclusion — but nothing in what WillEQ
itself captured supports that.

### Role 2 — WillEQ as actor, two passes

**How the internal heading value was verified correct before Pass A**:
Pass A used `/move <x> <y> <z>` (not `/turn`/`/face`) to walk short
distances, which drives `m_heading` through `EverQuest::UpdateMovement()`
-> `CalculateHeading()` (`eq_movement.cpp`), a completely different code
path from `EqActionHandler::setHeading()` (the buggy site `/turn`/`/face`
share). Verified two ways: (1) by code inspection — grepped every call
site of `EqActionHandler::setHeading()` and confirmed `/move`/`/moveto`
never reach it; (2) live, by cross-checking that the `-d4` log's own
`CalculateHeading: ... adjusted=X°` value (computed from the real
per-tick `dx,dy` of the movement) **exactly equals** the subsequent `POS
C->S [SELF] m_heading=X°` value logged when the position update was
sent — proving the write came from the movement path, not the converter.
Example (the "north" test): `CalculateHeading: from (-645.5,327.1) to
(-646.2,327.6), dx=-0.7, dy=0.4, ... adjusted=302.6°` immediately followed
by `POS C->S [SELF] m_heading=302.58deg`.

**Caveat, honestly flagged**: Pass A's destinations were fixed absolute
coordinates chosen to approximate pure N/E/S/W/diagonal travel from the
*intended* prior stopping point, but `/move`'s path-arrival tolerance
left each actual starting position ~2-6 units off the intended waypoint
(visible in the `dx,dy` pairs above). So the six Pass A headings below
are genuine, movement-derived, non-buggy values — but they are
**approximately**, not exactly, compass-pure. The SW sample in
particular is unreliable: it landed on the exact same `m_heading` as the
preceding West sample (243.56°), most likely because the prior NE move
hadn't fully completed within the script's 5s hold, leaving the "SW" move
too short to register a distinct value. Flagged, not silently absorbed.

| direction (intended) | Pass A `m_heading` (movement-driven) | Pass B commanded `/turn` | Pass B `m_heading` (slash-command-driven) | Pass B wire (`server_heading`, `heading_scaled`) |
|---|---|---|---|---|
| North | 302.58° | `/turn 0` | 0.00° | 90.00° / 512 |
| East | 68.49° | `/turn 270` | 384.00° | 66.00° / 375 |
| South | 169.84° | `/turn 180` | 256.00° | 194.00° / 1103 |
| West | 243.56° | `/turn 90` | 128.00° | 322.00° / 1831 |
| NE (intermediate) | 25.22° | `/turn 45` | 64.00° | 26.00° / 147 |
| SW (intermediate, **unreliable — see caveat**) | 243.56° (dup. of West) | `/turn 225` | 320.00° | 130.00° / 739 |

**Pass B reproduces T84's finding exactly**, including the new SW/225°
data point extending the pattern: every commanded value comes out
`commanded * 512/360 = 1.4222` too large before being (correctly) put on
the wire — `/turn 0->0` (no visible error at 0), `/turn 270->384`,
`/turn 180->256`, `/turn 90->128`, `/turn 45->64`, `/turn 225->320`.

**The diff, which is the deliverable**: Pass A's values (real, movement-
derived degrees: 302.58, 68.49, 169.84, 243.56, 25.22) and Pass B's
internal values (slash-command-derived: 0.00, 384.00, 256.00, 128.00,
64.00, 320.00) are **completely different numbers for nominally the same
four-plus-two directions** — there is no meaningful overlap. This alone
doesn't prove the encoder is broken (Pass A's directions were only
approximately compass-aligned, per the caveat above, so a direct A-vs-B
numeric comparison isn't apples-to-apples). What it **does** prove,
directly: **Pass A's values never touch `EqActionHandler::setHeading()`'s
`*512/360` conversion** — no Pass A value is any Pass B value's
counterpart scaled by that factor, and no Pass A value shows the
`commanded*1.4222` signature Pass B shows against its own commanded
input. Pass A's wire encoding (the `POS C->S [SELF] m_heading=X ->
server_heading=Y` line, e.g. `295.87 -> 154.13`, `68.49 -> 21.51`,
`169.84 -> 280.16`, `243.56 -> 206.44`, `25.22 -> 64.78`) applies the
**same, single, correct** `server_heading = 90 - m_heading` (mod 360)
conversion in every sample, movement-driven or slash-command-driven
alike — i.e. **the wire encoder itself is sound in both passes**; the
divergence lives entirely upstream, at `EqActionHandler::setHeading()`,
exactly where T84 found it. **No second, independent encoding defect was
found underneath the known bug.** (Whether the official observer's pcap
confirms the *rebroadcast* matches this wire value is for the oracle
session's pcap to confirm — WillEQ's own screenshots during Role 2 are
not useful evidence here: the camera was pitched almost straight up at
the sky in every Pass A/B frame, see `review/T85/README.md`.)

### Believed wrong, not touched (per the plan's blanket prohibition)

1. **`EqActionHandler::setHeading()` double-conversion** (T84's finding,
   `refactor/deferred.md`) — reconfirmed live this unit, extended with a
   6th data point (`/turn 225`). Not touched.
2. **The Wizzy ClientUpdate silence** (this unit's own finding, above) —
   a real, load-bearing gap in what WillEQ can observe about other
   players' movement in a populated, multi-account scenario. Not a code
   change to make blind — needs the server-side pcap first.
3. **The magenta/checkerboard texture fallback** in North Qeynos (and,
   in a different color, the South Qeynos night scenes) — visible across
   multiple independent screenshots at multiple times, not a one-off
   capture glitch. Consistent with the constrained 64MB texture budget
   (`Constrained rendering mode: Custom (800x600, 64MB texture...)`,
   logged at startup) under memory pressure from a densely populated
   zone (the known prepare-all-entities defect this plan itself warned
   about). Not root-caused, not touched.
4. **Role 2's Follow-camera pitching up at the sky** during rapid short
   `/move`/`/turn` sequences — every Pass A/B screenshot shows this.
   Not investigated further (out of scope, time-boxed), but real and
   reproducible.

### Deviations

- **Daylight was never forced during Role 1** (`/say #set time 11 0`) —
  a direct miss of `EXECUTOR_PROTOCOL.md`'s mandatory step. Only
  remembered once GM status arrived for Role 2, where it was applied.
  South Qeynos screenshots from sessions 1-3 are all night-time as a
  result.
- **One real, undocumented-until-now coverage gap**: session 1 ran dry
  (under-padded script) at 00:19:01, and the replacement did not zone in
  until 00:19:15 — a ~14s window where WillEQ was off the wire entirely.
  Not recoverable. All later transitions were either overlapped
  (negligible sub-second gaps) or cleanly sequenced with explicit
  zone-in confirmation before the next action, per the coordinator's
  mid-session correction.
- **`#zone qeynos2` was sent before confirmed zone-in once** (00:43:03,
  ~2.5s after a "still loading" screenshot) — landed anyway, but by
  luck. Corrected in the next qeynos2 attempt (00:59:18, sent 16s after
  explicit zone-in confirmation).
- **The apparent "lost zone-persistence" the coordinator initially
  flagged to Will was not a bug** — it was this unit's own Role 2 script
  deliberately sending `/say #zone qeynos` to reach Oracledelta's parked
  location. Coordinator retracted this with Will; recorded here per
  their request so it isn't misfiled as a defect.
- **Used `build/bin/willeq` (full GLES2 test tree) instead of also
  rebuilding `build-gles2/`** for the entire session, to protect the live
  capture window. Both trees use the same GLES2 render path; flagged
  and accepted mid-session.
- Build + full suite (deferred per the coordinator's explicit
  instruction, run only after the live capture concluded): **2066/2067**,
  sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`
  — matches the expected baseline exactly.

### Server/lock discipline

The eqemu lock (`/mnt/projects/fable/.eqemu-lock`) was never created,
modified, or deleted by this unit — confirmed still held by
`H05-tier0-official-client-oracle` at the end of the session. No
`up.sh`/`down.sh` run. No zone/world process restarted. Only WillEQ's own
process was ever started or stopped, and the final shutdown
(01:04:20.399) was a clean `kill -TERM`, not a crash.

### `.sleq` fragments (in order, for the oracle session to reassemble)

| file | span (UTC) | covers |
|---|---|---|
| `data/captures/t85_session.sleq` | 00:09:07 - 00:19:01 | Role 1, South Qeynos, session 1 |
| `data/captures/t85_role1b_session.sleq` | 00:19:15 - 00:24:51 | Role 1, South Qeynos, session 2 |
| `data/captures/t85_role1c_session.sleq` | 00:24:48 - 00:41:55 | Role 1 extended + GM relog + qeynos2 detour, session 3 |
| `data/captures/t85_role2_qeynos2_session.sleq` | 00:42:57 - 00:54:15 | qeynos2 combat observation (post-GM), session 4 |
| `data/captures/t85_role2_session.sleq` | 00:54:54 - 00:58:46 | Role 2 (Pass A + Pass B), South Qeynos, session 5 |
| `data/captures/t85_role3_qeynos2_session.sleq` | 00:58:59 - 01:04:20 | Final qeynos2 combat observation, corrected zone timing, session 6 |

All six live in `/mnt/projects/fable/wt/t1/data/captures/` (gitignored,
not committed). Corresponding `-d4`/DEBUG application logs sit alongside
each with matching `t85_*_run.log` names.

## Board

**Three-client arrangement, proven live**: WillEQ coexisted cleanly for
~55 minutes alongside two officially-driven clients on the same eqemu
stack, sharing one account-namespace (`willeq_a` only, never touching
`hc_test1`/`willeq_d`/`willeq_c`), with zero interaction, targeting, or
movement of the other characters. This opens a matrix cell two official
clients alone cannot: **an independent third decoder** confirming
Oracledelta's position/heading self-consistently with what the official
clients presumably agree on, PLUS a genuinely new finding (the Wizzy
ClientUpdate silence) that needed a third, non-official client watching
to even notice — neither official client would have had reason to
suspect it.

**Headline findings, for the oracle session and for whoever picks up
`refactor/deferred.md` next**:
1. Wizzy's `OP_ClientUpdate` broadcasts never reached `willeq_a` after
   the initial zone-sync echo, across ~28 minutes covering his confirmed
   live cardinal-heading routine — settled from the wire (`.sleq`), not
   speculated. **Needs the oracle session's server-side pcap** to
   determine whether the server ever sent these broadcasts toward
   `willeq_a` at all.
2. Role 2's two-pass test found **no second heading-encoding defect** —
   the wire encoder (`server_heading = 90 - m_heading`) is provably
   identical and correct across both movement-driven and
   `/turn`-driven headings; the entire observed error is
   `EqActionHandler::setHeading()`'s known double-conversion (T84),
   reconfirmed with a 6th data point.
3. The magenta/checkerboard texture-fallback defect reproduces reliably
   in North Qeynos (and, differently colored, in dark South Qeynos
   scenes) — likely related to the documented 64MB constrained texture
   budget + prepare-all-entities defect, not root-caused this unit.

**Handoff to the oracle session**: all six `.sleq` fragments, with exact
time spans, are listed in the table above (Review section) — they live
in `/mnt/projects/fable/wt/t1/data/captures/`. The full wall-clock action
timeline (every login, zone transfer, script action, and shutdown) is in
the Review section's timeline table. `review/T85/wizzy_wire_evidence.txt`
has the raw parse output for the Wizzy question, ready to cross-check
against the server-side pcap.
