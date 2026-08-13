# T99 — Route commands through `ActionDispatcher`: the Group pilot

**Slot: whichever is free. Headless work, then a LIVE leg — this one needs the
server. eqemu lock normally.**

**Territory: `src/client/action/` (`command_processor.*`, `action_dispatcher.*`
if a method is genuinely missing), `src/client/eq_commands.cpp` (removals
only), `tests/`, `refactor/`, `/mnt/projects/fable/review/T99/`.**

Read T97's Review and `/mnt/projects/fable/review/T97/divergence_table.tsv`,
then T98's Review, then this plan.

**Will's direction: `ActionDispatcher` is to be the route commands take.** This
unit is the pilot that proves the migration recipe on one small family. It is
NOT the whole refactor — ~125 game-registry commands exist and this touches a
handful.

## The layering constraint — migrate, do not wrap

`Application` owns `ActionDispatcher`, `CommandProcessor` and `EverQuest`. The
dependency runs:

```
ActionDispatcher ──► IActionHandler ──► EqActionHandler ──► EverQuest
```

**`EverQuest` is below the action layer.** Making the game registry (which
lives inside `EverQuest`) call `ActionDispatcher` would create a cycle:
`EverQuest → ActionDispatcher → EqActionHandler → EverQuest`. **Do not do
that.** If you find yourself adding an `ActionDispatcher` include or pointer to
`eq.h` or any `eq_commands*.cpp`, stop — you have taken the wrong approach.

The correct shape is **migration upward**: the command's registration and logic
move *out* of the game registry and *into* `CommandProcessor`, which already
routes through `ActionDispatcher`.

## Port the better implementation, do not just pick a side

T97's divergence table is the input. Where the game-registry implementation is
better — which T97 found it usually is — **port that implementation** into the
action layer rather than reviving whatever `CommandProcessor` had.

T98 deliberately deleted the *worse* duplicates. This unit re-adds the *better*
ones in the right layer. That is the sequence working as intended, not churn.

## The pilot family: Group

Chosen because the action-layer methods already exist and are unreferenced —
T98 reported `acceptGroupInvite`, `declineGroupInvite`, `inviteTarget`,
`inviteToGroup` and `leaveGroup` as newly caller-free. Migrating Group gives
them callers instead of deleting them, and **fixes a real user-facing gap on
the way** (see below).

Scope: the game registry's Group-category commands — `invite`, `disband`,
`decline`, and the Social `follow_cmd` group-accept behaviour. Confirm the
exact membership from the registrations; do not trust this list.

## Atomicity — this is the part that will silently fail

The `game` registry is tried **before** `action`. So adding a command to
`CommandProcessor` while it still exists in the game registry changes nothing —
the migration would be invisible and you would be testing the old path.

**Each command must be added to `CommandProcessor` and removed from the game
registry in the same change.** Verify per command that dispatch now logs
`registry=action`, not `registry=game`. T82 established that
`Application::printCommandResult()` logs this.

## Fix the group-invite gap as part of the pilot

T97 and T98 established that accepting a pending group invite via bare
`/follow` is dead by every route: `game`'s Social `follow_cmd` is overwritten
by Movement's `follow`, `action`'s copy was shadowed and is now deleted, and
`eq_bridge.cpp`'s `GroupAcceptIntent` handler has **no live producer**.

`ActionDispatcher::acceptGroupInvite()` exists and is orphaned. Give it a
caller as part of this migration and **verify live that a pending invite can
actually be accepted.** Restoring a broken feature is the pilot's proof that
the migration delivers value rather than just moving code.

Decide and state where it belongs — reviving bare `/follow` conflicts with
Movement's `follow`, so an explicit spelling may be cleaner. Say what you chose
and why.

## Do NOT migrate renderer commands

Renderer commands must stay on the `RendererCommand` bridge queue and drain on
the render thread. Calling `processSlashCommand()` off the game thread runs GL
with no EGL context current — P10 hit exactly this and got deterministic
garbage screenshots rather than a crash, because Mesa fails silently. This is a
documented landing constraint in `docs/decisions.md`. None of the Group family
should be affected, but know the boundary before you touch anything else.

## Produce the recipe

The lasting deliverable is a **written migration recipe** for the remaining
~120 commands: the steps, the atomicity requirement, how to verify with
`registry=`, what to do when the action layer lacks a method, what must never
migrate, and how long the family took. The next unit should follow it rather
than rediscover it.

Include an honest estimate of the total effort implied.

## Tests

- Each migrated command dispatches through `CommandProcessor` and reaches the
  expected `ActionDispatcher` method.
- The group-accept path has a test.
- **Prove tests can fail in both directions** (the T95 standard): break the
  routing, show red; restore, show green. Report that you did it.

## Live leg — required

- Each migrated command works against the server, observed, not inferred from
  an ack. `outcome=applied` means a handler returned success and nothing more.
- **A real group invite, accepted.** Two accounts exist (`willeq_test`,
  `willeq_buddy`); T84 established two concurrent clients work and that
  concurrency is per-account.
- Confirm `registry=action` for every migrated command.

Session discipline: verify binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts far longer than you think; `-d 4` plus
`--log-level=DEBUG` for wire lines; restore DB state and confirm by `SELECT`
**after logout**; clean teardown and delete the lock.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`.** That is
  the cycle.
- Do not migrate renderer commands.
- Do not migrate beyond the Group family. **STOP and report** if the pilot
  reveals the recipe does not generalise — that is the most valuable thing this
  unit could find, and it must not be buried under a partial migration.
- Do not reorder the registries. Migration makes reordering unnecessary.

## Verification

Full build and suite (expect 2101 total plus your new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens must not move.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T99-action-unification`, off `fable` tip `25510b91`. Full evidence:
`/mnt/projects/fable/review/T99/` (README + two filtered live-session logs).

### Confirmed Group membership

Re-derived from the registrations, not trusted from the plan's prose (which
matched exactly): `game`'s Group category (`eq_commands.cpp`) held exactly
three commands — `invite` (alias `inv`), `disband`, `decline` — plus the
Social section's `follow_cmd` (name `follow`, alias `fol`), which is
Group-*behavior* (accept-pending-invite) sitting in a differently-categorized,
differently-named, and — per T97 — intra-registry-shadowed registration.
All four are what this unit touched. `action::CommandProcessor` had zero
Group registrations before this unit (T98 emptied the category entirely).

### Which implementation was ported, and why

| command | source ported | why |
|---|---|---|
| `invite` | `game`'s `invite_cmd` logic (explicit name, else fall back to current target) | T97: "Equivalent" (same terminal calls either side) — ported for behavior parity, re-expressed against `ActionDispatcher::inviteToGroup()`/`inviteTarget()` (both orphaned since T98) instead of duplicating `EverQuest`'s private-member target lookup |
| `disband` | `game`'s `disband_cmd` leader/member message branch | T97: "Live is better" — `game`'s version branches on leadership for the local message; the old (T98-deleted) `action` copy always sent the member packet. **Finding, not assumed**: `EverQuest::SendLeaveGroup()` is itself just a call to `SendGroupDisband()` (`eq_group.cpp:123-127`), so the two branches are network-*identical* — only the local wording differs. Ported as message-construction logic around a single `ActionDispatcher::leaveGroup()` call, not as two different dispatcher calls |
| `decline` | `game`'s `decline_cmd` pre-check + `ActionDispatcher::declineGroupInvite()`'s existing (already-correct) implementation | T97: "Equivalent" — `EqActionHandler::declineGroupInvite()` already read `GameState`'s pending-invite state and called `SendGroupDecline()` correctly; only needed a caller and the pre-check/message wrapper `game`'s version had |
| `groupaccept` (**new**) | Neither — this command didn't exist reachably anywhere. Wraps the already-orphaned `ActionDispatcher::acceptGroupInvite()` | See below |

### Where group-accept landed, and why not bare `/follow`

New explicit command: **`/groupaccept`** (alias `/gaccept`), Group category,
`action::CommandProcessor`. Bare `/follow` was rejected deliberately: T97
found `game`'s own Social `follow_cmd` (the one with the accept-pending-
invite branch) was *already* intra-registry-shadowed by Movement's later
`follow` registration (entity-following, a different feature, no accept
branch) — reviving bare `/follow` in the action layer would not have fixed
anything, because `game`'s `follow` (Movement's, no accept branch) is tried
first and would keep winning every dispatch. An explicit spelling sidesteps
the collision entirely rather than re-creating it one layer up. The dead
Social `follow_cmd` block was deleted from `eq_commands.cpp` (not revived)
since its only live-relevant behavior (invite-accept) now has a real home
and its "follow a player" branch was always redundant with Movement's own
`follow`.

### Atomicity: proof via `registry=`

Each of `invite`/`disband`/`decline` was **added to `CommandProcessor` and
removed from `eq_commands.cpp` in the same commit** — verified by `git
diff --stat` showing only removals in `eq_commands.cpp` and only additions
in `command_processor.*`, no window where both existed. Live proof, second
(fixed) session, `review/T99/`: every migrated command shows
`registry=action` in the `CMD outcome=` line; `/target` (an intentionally
un-migrated control, still fully in `game`) shows `registry=game` in the
**same session**, proving the dispatch order itself was not disturbed —
this is the stronger check the plan asked for ("Verify per command"), not
just "no regression" but "the untouched sibling still resolves the old way
while the migrated ones resolve the new way, side by side."

### Group-accept: the live evidence

Second (fixed) session, `review/T99/t99_b_testbuddy_evidence.log`:
```
Testmelee has invited you to join a group
[System] You have joined Testmelee's group
CMD outcome=applied registry=action recognized=true message="Accepted group invite" cmd="/groupaccept"
Group update: 2 members, leader: Testmelee
[GROUP] In group (2 members), leader: Testmelee
```
Happened twice in the session; both times the group was later disbanded and
**both clients independently logged** "Your group has been disbanded" —
server-confirmed, not a client-side echo. This is `outcome=applied` backed
by more than an ack: a real invite packet arrived, a real accept packet was
sent, and the server's own broadcast confirmed group membership on both
ends.

### The infrastructure gap this pilot found (and had to fix to verify live)

**First live session (not kept) found every migrated command dispatching
correctly (`registry=action`, `outcome=applied`) but the group never
`felt` formed on the read side** — `m_state.group().hasPendingInvite()`/
`inGroup()` always read false, no matter what actually happened on the
wire. Root cause: `GameStateSyncBridge` (`src/client/bridge/
game_state_sync_bridge.cpp`) — the decorator that feeds
`Application::m_gameState` from bridge events, with an existing case per
domain (Pet/Spell/Vendor/Bank/Trainer/Entity/Inventory) — had **no case for
`GroupChanged`/`GroupMemberUpdated`/`GroupInviteReceived` at all**.
`EverQuest`'s own internal state was always correct (confirmed: its own
chat line "Testmelee has invited you to join a group" printed correctly
every time), but nothing ever copied that into the `GameState` instance
`ActionDispatcher`/`CommandProcessor` actually read. This is T49's "two
GameState instances" trap, on the write side this time — and it had never
been hit before because no prior unit had a live, reachable caller into any
Group action-layer method.

**This is outside the plan's listed territory** (`src/client/action/`,
`eq_commands.cpp` removals, `tests/`) — `game_state_sync_bridge.cpp` is
bridge-layer, not action-layer. Judgment call made to fix it anyway,
narrowly: the plan's own explicit, repeated, non-optional requirement
("verify LIVE that a pending invite can actually be accepted," "Give it a
caller and verify LIVE...") is **structurally impossible** without this fix
— no amount of correct `action/` code changes the fact that
`ActionDispatcher`'s own state input was never wired up for Group. The fix
follows the exact established pattern for every other domain in the same
file, doesn't touch `EverQuest`/`eq.h`/`eq_commands*.cpp`, doesn't add any
new dependency direction, and is 3 cases / ~35 lines. Test-proven red before
the fix, green after
(`GameStateSyncBridgeTest.GroupChangedCancelInviteClearsPendingInviteWithoutTouchingGroup`
caught a real bug in the fix's first draft — see Deviations).

### Two new pre-existing bugs found live (reported, not fixed — see `refactor/deferred.md`'s T99 entry for full detail)

1. `EverQuest::AcceptGroupInvite()` reads `m_pending_inviter_name` **after**
   passing it by const-reference into `SendGroupFollow()`, which clears
   that same member before returning — an aliasing bug. The accept's own
   local log/chat text prints blank ("Accepted group invite from ", "You
   have joined 's group"), confirmed live twice, even though the accept
   itself works end-to-end (server confirms). `eq_group.cpp`, below the
   action layer — not fixed.
2. The group leader's own client sees an empty `leadersname` on its own
   `GroupUpdate` packet (A's log: `leader: ` blank; B's log, same event,
   same timestamp: `leader: Testmelee`) — so `m_is_group_leader` stays
   false on the leader's own client, and the ported disband leader/member
   message branch picked the member wording on A's own screen (network
   effect unaffected — confirmed by B seeing the group actually disband).
   Likely an eqemu server quirk; not fixed.

Neither is a T99 regression — both are pre-existing `eq_group.cpp`/server
behavior, invisible until this pilot gave these code paths their first live
callers. Exactly the "restoring a broken feature surfaces what nobody could
previously observe" pattern the plan anticipated.

### Tests, and proof they can fail in both directions

13 new `CommandProcessorTest` cases (`tests/test_command_processor.cpp`):
registration/category checks, and per-command dispatch-reaches-the-right-
`ActionDispatcher`-method assertions for `invite`/`disband`/`decline`/
`groupaccept` (including the pre-check-fails-cleanly-without-dispatching
cases and both disband branches). 7 new `GameStateSyncBridgeTest` cases
(`tests/test_game_state_sync_bridge.cpp`) covering the three new
`GameStateSyncBridge` cases, including the cancel-invite-without-being-in-
-a-group edge case that caught a real bug in the fix's first draft.

**Proved failure in both directions, twice:**
1. `CommandProcessor::cmdGroupAccept` — inserted an unconditional
   `return ActionResult::Failure("BROKEN FOR TEST")` at the top; reran
   `GroupAccept_PendingInvite_DispatchesAcceptGroupInvite`/`_Alias...`:
   both **failed** (`result.success` false, handler never called). Restored
   from a pre-edit backup (byte-identical diff confirmed), reran: both
   **passed**.
2. (Found organically, not staged) `GameStateSyncBridge`'s `GroupChanged`
   handler's first draft relied on `setInGroup(false)`'s internal
   transition-only side effect to clear the pending invite; the
   cancel-invite-while-already-not-in-a-group test **failed** red on that
   draft, diagnosed, fixed with an explicit `clearPendingInvite()` call,
   reran **green**. This is the T95 standard demonstrated the way it
   matters most — a test that was RIGHT to fail, on a bug that was real.

### Migration recipe (for the remaining ~120 commands)

1. **Confirm membership from the registrations, not from any prior unit's
   prose list** — grep `eq_commands*.cpp` for the category/name, don't
   trust a plan's summary (this unit's own plan had a slightly imprecise
   membership list — the Social `follow_cmd` accept-behavior wasn't a
   clean "Group category" member, it took reading T97's shadowing finding
   to place it correctly).
2. **Look up the command in T97's `divergence_table.tsv`** to see which
   side is better and why, before writing a line of code.
3. **Port the better side's *logic*, not necessarily its call shape.**
   `game`'s handlers touch `EverQuest`'s private members directly; the
   ported version must go through `ActionDispatcher`/`GameState` instead.
   Where an existing dispatcher method already does the right thing (this
   unit's `decline`), the "port" is mostly a caller + message wrapper.
   Where the dispatcher method is missing entirely, add it to
   `action_dispatcher.*` (this unit needed none — Group's methods all
   pre-existed from an earlier, orphaned pass).
4. **Add to `CommandProcessor` and remove from `eq_commands.cpp` in the
   same commit.** Never a two-step migration — the game registry is tried
   first, so a command present in both places is dispatch-invisible to add
   and silently-still-old to test.
5. **Verify per command with `registry=`,** not just "the test suite is
   green" — dispatch a live or scripted instance of every migrated command
   and confirm the `CMD outcome=` line says `registry=action`. Include an
   intentionally-un-migrated sibling command in the same session as a
   control (this unit used `/target`) — proves the dispatch order itself
   wasn't disturbed, not just that the new command works.
6. **Before declaring live verification done, check whether
   `GameStateSyncBridge` has a case for the state domain the migrated
   commands read.** This unit's single biggest time cost was discovering
   Group had none. Any future family whose commands read `GameState`
   fields fed by bridge events (not just written by the dispatcher) should
   check this FIRST, not after a failed live session — grep
   `game_state_sync_bridge.cpp` for the relevant `GameEventType` cases
   before starting.
7. **When a method the dispatcher needs doesn't exist**, add it to
   `action_dispatcher.h`/`.cpp` following the existing `checkZoneConnection()`/
   `checkHandler()` pre-flight pattern already used throughout that file.
8. **Never touch `eq.h` or `eq_commands*.cpp` except to remove the migrated
   registration.** No new include, no new pointer, no new call upward.
9. **Live-verify with a real server round trip** for anything whose
   correctness depends on server state (a real invite, not just a
   client-side echo) — an `outcome=applied` ack proves the handler ran, not
   that the effect landed. Group's own gap (accept was dead by every route)
   is proof this distinction is not academic.

### Effort estimate for the remaining ~120

This family (4 commands, one of them new) took roughly **one long working
session**: ~1.5h reading T97/T98/the plan and confirming membership, ~1h
porting the four command bodies and writing the 13 `CommandProcessor`
tests, ~2h on the live leg (two full live sessions, the first of which
found the `GameStateSyncBridge` gap, the fix, its own 7 tests, and a
rebuild/re-verify cycle), ~1h writing up the recipe/README/deferred.md
entries. Call it **~5-6 hours for a 3-4 command family that needed a new
infrastructure fix.**

Most of the remaining ~120 will be cheaper per-command than Group was,
because:
- Group needed a brand-new command (`/groupaccept`) and a state-plumbing
  fix; most families won't need either — T97's divergence table shows the
  large majority are "Equivalent" or "dead is a stub", meaning the port is
  closer to "add a caller + pre-check wrapper" than "port real logic."
- The `GameStateSyncBridge` gap, once found, is a **one-time** class of
  risk — the next family to touch a `GameState` domain that already has
  bridge coverage (most do; Group was a genuine outlier) won't hit it.

But some will be *more* expensive:
- **Spells** (`mem`/`gems`/`skills`/`forget`) — T97 found these are
  literal stubs in the dead `action` copies (`mem`/`forget` return a false
  success without doing anything). Porting `game`'s real
  `SpellManager`-based logic is a bigger lift than Group's was, and spell
  state has its own gem-slot/cooldown mechanics to get right.
- **`cast`/`interrupt`** — T97 found these route through an entirely
  different (older, `CombatManager`-based) subsystem than the live
  `SpellManager` path `game`'s `/cast` actually uses. This isn't a
  message-wrapper port; it may need new `ActionDispatcher` methods that
  call `SpellManager` directly, not `CombatManager`.
- **`pet`** — T96/T97 found `game`'s vocabulary is a strict superset of
  the dispatcher wrappers' (`leader`, `spellhold`/`nospellhold`, `notaunt`,
  `nohold`, `standup`, `report` alias are all missing from the action
  layer). New `ActionDispatcher`/`EqActionHandler` methods needed, not
  just new callers.
- **`filter`/`camp`** — both route through the `RendererCommand` bridge
  queue on the `game` side (real renderer effects); a "port" here means
  routing `CommandProcessor` through the same bridge queue `game` already
  uses, not calling `ActionDispatcher` directly — worth flagging explicitly
  since the plan's "do not migrate renderer commands" prohibition is about
  a different thing (commands that must STAY renderer-thread-only,
  `RendererCommand`-bridge sends are already safely cross-thread) but the
  distinction is easy to blur.

**Honest total estimate: 60-90 hours** across the remaining ~120 commands,
done in family-sized batches like this one (not one command at a time —
most of this unit's overhead was fixed cost: reading T97/T98, writing the
recipe, discovering the `GameStateSyncBridge` gap once). A family with no
new commands and no missing dispatcher methods (most chat/movement/utility
commands, per T97's "Equivalent" rows) should take under an hour each once
the recipe above is followed mechanically; a family needing new dispatcher
methods or bridge-sync cases (Spells, `cast`/`interrupt`, `pet`) should be
budgeted like this unit was, 4-6 hours.

### Does the recipe generalise?

**Yes, with one addendum.** The plan's core mechanics (port-the-better-
implementation, atomic add+remove, verify via `registry=`) worked exactly
as designed and needed zero deviation. The one addition this pilot found —
step 6 above, check `GameStateSyncBridge` coverage before the live leg, not
after a failed one — is a real, generalizable lesson, not a sign the
recipe breaks down. No family-specific blocker was found that would
prevent the same recipe from being applied to the next ~120 commands.

### Deviations

1. **`src/client/bridge/game_state_sync_bridge.cpp` edited — outside the
   plan's listed territory.** Justified above ("The infrastructure gap
   this pilot found"); flagged here explicitly per the plan's own
   instruction to flag deviations rather than absorb them silently. No
   `EverQuest`/`eq.h`/`eq_commands*.cpp` touched, no new dependency
   direction, follows the file's existing per-domain-case pattern exactly.
2. Two new, real, pre-existing bugs found live and reported (not fixed,
   both below the action layer) — see above and `refactor/deferred.md`.
3. First live session (data not kept in `review/T99/`, superseded by the
   second) found the `GameStateSyncBridge` gap; the second (fixed) session
   is the one cited as evidence throughout this Review.
4. No scope extension beyond Group — no other command family touched, no
   registry reordering, no renderer command migrated.

### Verification

1. `cmake --build build -j24` — clean, both before and after the
   `game_state_sync_bridge.cpp` fix.
2. `ctest --test-dir build --output-on-failure` — **2121/2121 passed except
   the documented ALSA `AudioManagerTest.PreloadCommonSounds`** (2114 total
   after T98's 2101 baseline + this unit's 20 new tests [13
   `CommandProcessor` + 7 `GameStateSyncBridge`] = 2121; matches exactly).
3. `bash scripts/build-gles2-x86.sh` — clean, run twice (pre- and
   post-bridge-fix), binary freshness confirmed newer than `HEAD` before
   each live session.
4. Visual gate (folded into the `ctest` run, `visual_gate` test) — PASS;
   `git status` clean of any golden files throughout both sessions.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile, run twice
   (pre- and post-fix).
6. `scripts/refactor/check_file_sizes.sh fable` — 5 ok, 1 WARN
   (`eq_commands.cpp`, pre-existing over-1500-line file — this unit made it
   **smaller** [-76 net lines], not a new FAIL).
7. Live eqemu leg — two full sessions (first superseded, second kept as
   evidence), `willeq_test`/`Testmelee` + `willeq_buddy`/`Testbuddy`, both
   relocated to `arena` (T84's pattern) for fast zone loading. Lock
   taken/released cleanly, `down.sh` run, lock file deleted, DB positions
   restored and confirmed via `SELECT` after both clients exited.
