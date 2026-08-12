# T82 — One live session, three deferred legs, and proof `/who` and `/face` work

**Slot: t1. OWNS EQEMU. x86 only, no Pi.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T82/`, throwaway
session scripts. Production code is READ-ONLY unless Part 3 finds a defect
whose fix is genuinely small and in `eq_commands.cpp` — see Part 3.**

Read `refactor/EXECUTOR_PROTOCOL.md` (**eqemu section — lock first; and
the daylight directive in the media section**), then the Reviews of
**T81** (entity projection), **T77** (`/equip` pre-flight) and **T76**
(which reproduced the equip rejection).

Three units deferred a live leg for want of the server, and they converge
on one session. `/equip` is also how you arm a character for the rest, so
the parts are ordered deliberately.

## Part 1 — `/equip` live leg (T77's outstanding leg)

T66 added `/equip`; T71 fixed its client-model half and proved the wire
path correct with three live round trips; T77 added pre-flight validation
and item-name resolution but **could not test any of it live**.

Exercise, against a real character, and report what each does:
- `/equip list` — does it show equipped items by slot and carried items
  with resolved destinations?
- `/equip <item name>` — does name resolution find the item and pick a
  legal slot?
- `/equip <from> <to>` — the explicit form T71 verified.
- The rejection paths: empty source slot, illegal destination. **The point
  of T77 was error messages a script can act on** — confirm "slot 23 is
  empty" and "X cannot go in Y" rather than "invalid slot".

## Part 2 — `/who` and `/face` must be seen working

T81 populated `Application::m_gameState->entities()` and passed its
falsification test (zero consumer edits). But **nobody has watched these
work**, and this whole thread began with them being broken.

- `/who` — must list real nearby entities, not an empty header.
- `/face <target>` — must turn toward a visible NPC, not report "Entity
  not found".
- Also worth exercising: `follow`, `moveToEntity`, interact-nearest.

**If they still fail, that is the finding**, and it means T81's projection
is populated in tests but not in a live session — trace where the chain
breaks (is `EntityStateSnapshot` published? does `GameStateSyncBridge`
handle it? does the projection contain entities at the moment the command
runs?) and report. Do not fix blind.

## Part 3 — why does the server reject that equip move?

T75 hit `"The selected slot was invalid."` moving slot 23 → 13. T76
reproduced it fresh. **Nobody has explained it.** T71 separately proved
`SendMoveItem` correct with three successful round trips, so the wire path
works for *some* moves — the question is what is different about this one.

Establish the actual cause. Candidates worth checking, not a checklist to
tick: is slot 23 what the client thinks it is; is the item in it equippable
in slot 13 at all; does the character meet its restrictions; is the client's
inventory model out of sync with the server's before the move is sent.

Note T77's pre-flight should now catch several of these **before** sending
— so if `/equip` now refuses locally with a clear reason, that IS the
answer, and Part 3 is closed by Part 1.

**If the cause is a small, clearly-scoped defect in `eq_commands.cpp`, fix
it** and re-verify live. If it is anywhere else, or larger, report and stop
— that is a separate unit.

## Setup and teardown

- **Lock first.** Sanctioned start (`scripts/eqemu/up.sh`), clean shutdown
  (`down.sh`), lock deleted.
- **Force daylight**: `/say #set time 11 0` (space-separated), verified
  early per the protocol.
- `Testmelee` for equipment work (dual wield skill), `Testchar` otherwise.
- Resolve items through `data/willeq.db`'s `item_models`; state item id
  **and** resolved model id for anything equipped. T60's phantom bug came
  from guessing an id and getting a sack.
- **Restore everything** and confirm by `SELECT`. If you need direct DB
  writes to equip, that is a finding about Part 1, not a footnote.

## Verification

The deliverable is evidence, so the build/test bar is a formality — run it
to prove you broke nothing. Put the effort into the captures and the
written findings.

Media to `/mnt/projects/fable/review/T82/` with a README. Video for
anything temporal, stills are fine for command output. **Describe what
each shows; do not grade it.**

## Review

Executed in `/mnt/projects/fable/wt/t1`, task branch
`task/T82-live-verification`, at `fable` tip `9d3c85b0`. No production code
was touched — every finding below either turned out to already be handled
by existing code (T77's pre-flight) or was judged out of the "small,
clearly-scoped" fence and reported instead of fixed (see Part 3). Full
media, README, and raw command logs: `/mnt/projects/fable/review/T82/`.

### Zero-th finding: the pre-existing `build-gles2/bin/willeq` was stale

Before any of the three parts could be tested, `/help equip` against the
already-built `build-gles2/bin/willeq` (timestamped Aug 5) printed `Unknown
command: equip` — the binary predated T66 (which added `/equip` in the
first place), so `/equip 23 13` silently fell through DispatchSlashCommand's
three registries to the renderer fire-and-forget fallback
(`outcome=queued`) instead of ever reaching `EverQuest::m_command_registry`.
This produced a full false trail (logged and then discarded, not used as
evidence anywhere below) before being caught by cross-checking `/help
equip`'s output against the source. Rebuilt via `bash
scripts/build-gles2-x86.sh` (see `review/T82/diag_stale_binary_before_rebuild.log`
vs `diag_after_rebuild.log` for the before/after proof). **Lesson for the
next live-verification unit: confirm the binary you're about to test
against post-dates the feature under test, the same way item/model ids get
resolved through the DB rather than assumed.**

### Part 1 — `/equip` live leg — ALL THREE FORMS CONFIRMED WORKING, live

Character: `Testmelee` (race 6 Dark Elf, class 1 Warrior, level 1), zone
`nektulos`. Evidence: `review/T82/testmelee_session*.log`.

- **`/equip list`**: shown correctly at every stage — "Nothing currently
  equipped." / "Carried, equippable via /equip <name>: <Item> (slot N) ->
  <Dest>" before an equip, "Equipped (N items): <Slot>: <Item>" /
  "Nothing else carried that could be equipped." after.
- **Empty source slot**: `/equip 5 13` -> `"Nothing to move: Neck (5) is
  empty."` — exactly the actionable-reason bar T77 set (not "invalid
  slot").
- **Wrong slot type**: `/equip 22 13` (slot 22 = "Skin of Milk", itemtype
  food, `slots` bitmask 0) -> `"Skin of Milk cannot go in Primary (13) --
  wrong slot type for this item."`
- **Restriction failure**: `/equip 28 13` (slot 28 = item 1062, Kerran
  Fishingpole, `races` bitmask excludes Dark Elf/Erudite) -> `"Kerran
  Fishingpole cannot go in Primary (13) -- your character does not meet its
  class/race/level requirements."` — genuinely a race restriction (Testmelee
  is a Dark Elf), confirmed by decoding the item's `races` bitmask
  (65499 = 65535 minus bits for Erudite and Dark Elf) against
  `character_data.race=6`.
- **`/equip <item name>` — successful resolution and equip, full wire round
  trip**: seeded item 1327 "Grizznot's Claws" (resolved via
  `data/willeq.db`'s `item_models` table: item id 1327 -> model id 68;
  `slots`=8192/Primary, `reqlevel`=0, `classes` includes Warrior, `races`=
  all — legal for Testmelee, checked against the DB before use, not
  guessed). `/equip Claws` -> `"Resolved 'Claws' to Grizznot's Claws (slot
  29) -> Primary (13)."` then `"Moving item: slot 29 -> Primary (13)."`;
  `/equip list` immediately after confirmed `"Equipped (1 item): Primary
  (13): Grizznot's Claws"` — the full pre-flight -> local-model-sync ->
  `SendMoveItem` -> server-accepted path, in one live round trip.
- **`/equip <from> <to>` explicit form (T71's originally-verified form),
  round trip to a genuinely empty slot**: `/equip 13 27` (unequip) ->
  `"Moving item: Primary (13) -> slot 27."`, confirmed by `/equip list`
  showing `"Nothing currently equipped."`; `/equip 27 13` (re-equip) ->
  `"Moving item: slot 27 -> Primary (13)."`, confirmed equipped again. No
  rejection, no desync — this form still works exactly as T71 left it.

**Deviation, self-inflicted, corrected in-session**: this executor's first
attempt seeded test items via direct SQL at `inventory.slot_id` 22/28/29,
assuming those matched this client's own `inventory_constants.h`
(`GENERAL_BEGIN=22`..`GENERAL_END=29`). They do not — the *wire/DB* general
slot range is 23-30 (confirmed by the server's own resync message naming
"slot 23" for what the client calls slot 22, see Part 3's second finding),
one higher than the client's internal numbering. The DB-side insert at
`slot_id=22` landed nowhere valid and was silently relocated by the server
to a different client-visible slot; the `slot_id=29` insert never
surfaced client-side at all (see `testmelee_session1_slot_mixup.log`). This
produced a confusing first pass (still containing genuinely valid empty-slot
and wrong-slot-type evidence, since those didn't depend on the seeded
items' *positions* being what was intended) but not the clean
successful-equip proof; that was re-run cleanly in
`testmelee_session2_claws_name_resolution.log` /
`_session4_clean_roundtrip.log` after correcting the DB slot base to 23-30
and re-deriving the client-visible slot from `/equip list`'s own output
rather than assuming a mapping. **Numbers typed into a running client's own
`/equip <from> <to>` command are always client-side numbers (self-consistent,
22-29) — the 22-vs-23 offset only bites raw DB writes.** Recorded here
because the plan's "never guess an item/model id" caution applies equally to
slot ids for direct DB seeding, and nothing previously documented this
specific offset.

### Part 2 — `/who` and `/face` — `/face` CONFIRMED WORKING live; `/who` is UNREACHABLE (shadowed), a new finding

Character: `Testchar` (race 1 Human, class 1 Warrior, level 2), zone
`befallen`. Evidence: `review/T82/testchar_session.log`,
`testchar_0{1,2,3,4}_*.png`.

- **`/face a_scaled_wolf_pup`**: `outcome=applied registry=action` — this
  IS `ActionDispatcher::faceEntity()`, which reads `m_state.entities()`
  directly (T81's projection), confirmed by source (`action_dispatcher.cpp`)
  and by the dispatch log's `registry=action` tag. **This is the live proof
  the plan asked for**: T81's `Application::m_gameState->entities()`
  projection is populated at the moment a live command runs and a real
  consumer (`/face`) reads it successfully, with zero code changes needed —
  matching T81's own falsification-test claim, now confirmed outside a
  test harness.
- **`/who` does NOT exercise T81's projection at all, and never will as
  currently wired — traced, not assumed.** `/who` dispatched as
  `registry=game`, i.e. `EverQuest::m_command_registry`'s OWN `who` command
  (`eq_commands_movement.cpp`), which reads `EverQuest::m_entities` (the
  raw network map, a completely different, pre-existing data source T81
  never touched) and was never broken. `CommandProcessor::cmdWho`
  (registry 2, the one T81's Review and falsification test actually cite as
  reading `m_state.entities()`) is **shadowed**: `Application::
  DispatchSlashCommand()` always tries registry 1 (`EverQuest::
  m_command_registry`) first (`application.cpp`'s documented T29 dispatch
  order), and since a command literally named `who` already exists there
  and always succeeds, registry 2's `who` is never reached — from
  `--script`, from piped stdin, or from the live in-game chat window (all
  three funnel through the same `DispatchSlashCommand` first stage). This
  is confirmed live, not just read from source: the observed output text
  for `/who` (`"[1] zone controller (Lvl 200)", "[2] Boondin Babbinsbort
  (Lvl 25)", ..., "... and 153 more"`, cross-checked against the exact
  timestamp of the `/who` script step) matches registry 1's
  `"[{}] {} (Lvl {})"` format string in `eq_commands_movement.cpp` exactly,
  never registry 2's distinct `"=== Nearby Entities === ... Total: N"`
  format, which never appeared anywhere in any capture.
  Registry 2's `cmdWho` is real, unit-tested (per T81's Review), and
  **permanently unreachable through any live input path that exists today**
  — this is the project's now-familiar "implemented but unreachable"
  pattern (`refactor/deferred.md`), in a new shape: not zero producers, but
  two producers under the same name where the wrong one always wins.
  `/moveto` and `/follow` have the **identical** shadowing shape (both
  registered under the same names in `EverQuest::m_command_registry`,
  reading `m_entities`, not `m_state.entities()`) — both were exercised live
  (`"Moving to entity: a_scaled_wolf_pup"`, `"Following:
  a_scaled_wolf_pup000"` / `"Stopped following"`) and both work, but neither
  proves anything about `ActionDispatcher::moveToEntity()`/`followEntity()`
  (T81-relevant, registry 2), which are equally shadowed and equally
  unreachable live.
- **`/list` is NOT shadowed (registry=action) but ALSO does not exercise
  T81's projection** — a second, independent way to reach the same "looks
  like it tests T81 but doesn't" trap. `ActionDispatcher::listEntities()`
  forwards to `m_handler->listEntities(filter)`, a cross-thread call into
  `EverQuest::ListEntities()` (`eq_entities.cpp`), which prints `"Entities
  in zone (N total): ..."` from `EverQuest::m_entities` — the SAME raw map
  `/who`/`/moveto`/`/follow` read, not `m_state.entities()`. Confirmed live:
  this is the text that actually appeared for `/list` in the capture, not
  the `"=== Nearby Entities ==="` format from `CommandProcessor::cmdList`.
  Of everything tested, **`/face` is the only command with a real, live,
  non-shadowed, non-cross-thread-forwarded path to T81's projection.**
- **`interactNearest()`** (plan's fourth item): not exercised. It has no
  slash-command binding at all in either registry — its only caller is
  `InputActionBridge` (`input_action_bridge.cpp`), reachable exclusively
  from a raw hotkey/action event, which `--script`'s slash-command tour
  cannot drive. Reported, not worked around — consistent with the same
  reachability pattern as above, one level further (zero slash-command
  producers, not a shadowed one).

Screenshots (`testchar_01`..`04`) show a fully-loaded, textured `befallen`
interior with the "Setting world time to 11:00 AM." confirmation and a
cluster of overlapping NPC nameplates (Befallen's own static spawns)
pinned at the player's position; camera framing is visually identical
across all four (this camera mode doesn't visibly swing on `/face`/`/moveto`
at 0-units range) — the proof for each command is in the chat/log text, not
the pixels, and is described as such in `review/T82/README.md`.

### Part 3 — the server rejection: root cause, PLUS a second live-reproduced instance

**Cause #1 (T75/T76's original `/equip 23 13` -> "The selected slot was
invalid"): closed by Part 1's pre-flight, confirmed live.** Reproduced
fresh against `Testchar`: `/equip 23 13` (slot 23 holds item 12654, "Scaled
Curskin" — `items.slots` = 0, fits **no** equipment slot at all; confirmed
against the live DB, not assumed) now prints `"Scaled Curskin cannot go in
Primary (13) -- wrong slot type for this item."` and sends **no packet at
all** — T77's pre-flight rejects it locally before `SendMoveItem` is ever
called. Item 12654 is Testchar's default-inventory filler (all eight
general slots hold it, confirmed via `SELECT`) — it was never a weapon, and
the server was always going to bounce it; T77 just moved that bounce from
the server round trip to a local, actionable message. **No code change was
needed — Part 3's original case is closed entirely by Part 1's existing
pre-flight.**

**Cause #2, newly found this session: T77's pre-flight has a real, narrower
gap — reverse-direction swap legality is never checked.** `/equip 13 22`
(Testmelee, Primary -> a general slot that already held an item, "Skin of
Milk") hit the **identical** server-side rejection live:
```
Moving item: Primary (13) -> slot 22
The selected slot was invalid.
Inventory Desyncronization detected: Resending slot data...
Source slot 13 resyncronized.
Destination slot 23 resyncronized.
```
(`review/T82/testmelee_session3_swap_rejection.log`). Root cause: `doMove()`'s
direct slot<->slot branch (`eq_commands.cpp`) is a genuine swap when `toSlot`
is occupied — it puts the incoming item at `fromSlot` (here, "Skin of Milk"
into Primary) with **no legality check on that side of the swap**.
`CheckEquipDestination()` is only ever called for the command's own
`toSlot`, never for `fromSlot` when the swap displaces an item into it. The
server correctly rejects it (a non-equippable item landing in Primary), and
the client **self-heals** cleanly via its existing desync-detection/resync
path — this is not data corruption or a stuck state, confirmed by the
immediately-following `/equip list` showing the pre-move state intact. Also
notable: the server's own resync message names the swapped-out general slot
"slot 23", confirming server-side general slots really are numbered 23-30
against this client's internal 22-29 (see Part 1's deviation write-up) —
the same offset, independently corroborated.

**Not fixed.** This is a real, reproducible gap, but it is not the
"small, clearly-scoped" fix the plan's exception allows: a correct fix needs
`CheckEquipDestination()` (or a caller) to also validate the *displaced*
item against `fromSlot`'s own equip-slot requirements whenever `fromSlot` is
an equipment slot and `toSlot` is occupied — a second legality check with
its own edge cases (what if `fromSlot` isn't an equipment slot either;
what's the right message when both directions are illegal), not a one-line
change to the existing function. Per the plan's explicit guardrail
("anywhere else or larger: report and stop, it becomes a separate unit"),
reported here for a future unit rather than patched under this one's time
budget. It is also lower urgency than it looks: the failure mode is a
harmless, self-correcting bounce, not silent corruption or duplication.

### Fixed: nothing

No production code was touched. Both Part 3 causes turned out to be either
already closed by existing code (#1) or out of the narrow in-session fix
fence (#2, see above).

### Teardown

- **Server**: `bash scripts/eqemu/down.sh` run; lock file
  (`/mnt/projects/fable/.eqemu-lock`) deleted and confirmed via `ls` failing
  (see command trace below this section's writing — teardown was the last
  step taken).
- **Testchar** (`character_data.id=65`): inventory never touched (still all
  eight general slots = item 12654, confirmed via `SELECT`, unchanged from
  session start). Position/heading drifted during Part 2's `/face`/`/moveto`
  testing (heading 190 -> 307.5, small x/y drift) — restored via direct
  `UPDATE` to the documented baseline (`zone_id=36, x=-77.9232, y=21.9086,
  z=3, heading=190`) and confirmed via `SELECT` showing the exact baseline
  values.
- **Testmelee** (`character_data.id=66`): inventory fully restored to the
  exact 5-row baseline captured at session start (slots 23-27 =
  9990/9991/18751/21779/32601, confirmed via `SELECT` matching the
  pre-session `SELECT` byte-for-byte) — all seeded test items (1062, 1001,
  2848 from the mixed-up first pass; 1327 from the corrected pass) deleted.
  Position/zone never modified (no movement commands run against Testmelee
  this session) — confirmed unchanged via `SELECT`.
- Two extra zone processes started this session beyond `up.sh`'s default
  single `arena` zone (`befallen` for Testchar, `nektulos` for Testmelee),
  via the same sanctioned `server_exec_bg`/`./bin/zone <name>` mechanism
  `up.sh` itself uses — not a separate unsanctioned path. Left running for
  `down.sh` to tear down along with everything else; not restored
  individually (there is no "restore" state for a zone process, only
  stop, which `down.sh` covers).

### Verification

1. `cmake --build build -j24` — clean (no source changes this unit; already
   up to date at the branch tip).
2. `ctest --test-dir build --output-on-failure` — **2066/2067**, sole
   failure the documented ALSA host exception
   (`AudioManagerTest.PreloadCommonSounds`). `visual_gate` (test #2067 in
   this tree) — PASS, 7.11s.
3. `bash scripts/build-gles2-x86.sh` — clean (this is also the rebuild that
   fixed the stale-binary finding above).
4. Visual gate — PASS (see #2), goldens untouched (`git status` clean
   throughout — no production file was ever modified this unit).
5. `bash scripts/build-arm-noble.sh` — clean cross-compile, `willeq` and
   tool binaries produced for ARM.
6. `check_file_sizes.sh fable` — **"no C++ files to check ... nothing was
   checked, this is not a pass"** — correct and expected: this unit made
   zero production-code changes (`git status` clean, `git diff --stat`
   empty against `fable` throughout), so there is nothing for the script to
   check.

### Deviations

1. Stale pre-built `build-gles2/bin/willeq` (predated T66/T71/T77) produced
   a full false trail before being caught and rebuilt — see "Zero-th
   finding" above. No captures/logs from the stale-binary runs are cited as
   evidence anywhere in this Review.
2. First-pass DB seeding for Testmelee used the wrong general-slot base
   (22-29, this client's internal numbering, instead of the wire/DB range
   23-30) — see Part 1's deviation write-up. Corrected in later passes;
   left one still-informative but confusingly-labeled log
   (`testmelee_session1_slot_mixup.log`) rather than discarding it, since it
   still contains genuine evidence for two of the four required `/equip`
   behaviours.
3. No Testmelee screenshots. `nektulos`'s zone-geometry/entity loading (452
   entities, many `global*_chr.s3d` cache misses) did not finish within any
   session window tried (up to ~230s wall-clock, still at 87% when the
   process was killed) — the render thread never got far enough to process
   a queued `/screenshot` command. All Part 1 evidence is command-dispatch
   log text instead, which is complete and unambiguous. `Testchar`'s zone
   (`befallen`) loaded fully in the same session's time budget without
   issue, so this is specific to `nektulos`'s asset weight on this VM, not
   a general capture-tooling problem.
4. `--log-level=INFO` alone did not surface `LOG_INFO`-level chat/system
   output to stdout in this build (only ERROR-and-above lines appeared);
   `-d 4` (the numeric debug-level flag, matching `scripts/eqemu/
   record_tour.sh`'s own precedent) was used instead and worked correctly.
   Not investigated further (out of this unit's territory) but worth a
   note for the next session that reaches for `--log-level=` expecting
   command-output text in the log.
5. Two zone processes (`befallen`, `nektulos`) started beyond `up.sh`'s
   single default zone — see Teardown; sanctioned mechanism, not a new
   path.

Board: `refactor/deferred.md` updated — T77's and T81's "live leg
outstanding" notes both closed with this unit's result (Part 1/Part 2
above), and a new entry records Part 3's two causes (#1 closed, #2 reported
as a separate follow-up unit's scope). See board diff in this commit.
