# T100 — Migrate the Chat family to the action layer

**Slot: slot1. Headless work, then a LIVE leg. eqemu lock normally — T101 is
running in parallel and also wants it, so do all headless work first and
contend for the lock at the end.**

**Territory: `src/client/action/`, `src/client/eq_commands*.cpp` (removals
only), `src/client/bridge/game_state_sync_bridge.*` if coverage is missing,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T100/`.**

**Follow the migration recipe in T99's Review.** Read it first, in full,
along with T99's Review generally. This plan only adds what is specific to
Chat.

## Scope

The `game` registry's **Chat** category — 9 commands. Confirm exact membership
from the registrations; do not trust a count.

Will has directed the full migration, family by family. This is one batch.

## The recipe's own addendum — do this first

T99 learned it the hard way: **check `GameStateSyncBridge` coverage BEFORE the
live leg, not during it.** T99 found zero cases for Group events, meaning
`Application`'s `GameState` never saw a group change — the two-GameState trap.
Check whether Chat events are covered before you go live, and fix narrowly
following the file's existing pattern if not.

## Chat-specific cautions

- **Several of these were deleted by T98** (`say`, `ooc`, `auction`, `tell`,
  `reply`, `gsay`, `gu`, `emote`). You are porting the *game* implementation up
  into the action layer, not reviving what T98 removed. T97's
  `divergence_table.tsv` says which side was better — use it.
- **`shout` is a live `CommandProcessor` command already**, reachable via `/sh`
  because `game`'s shout alias is `sho`. Do not delete or duplicate it —
  reconcile it deliberately and say what you did.
- **`ConsoleInputHandler` has its own non-slash chat vocabulary** (`say`,
  `tell`, `shout`, `ooc`, `auction` — `console_input_handler.cpp:~373`) that
  builds `ChatMessage` structs directly and never reaches either registry.
  **Do not migrate or delete it**, but report whether it now diverges from the
  migrated path. A third chat implementation is exactly the kind of thing this
  programme exists to surface.
- Chat commands take free-text arguments including quotes and channel targets.
  **Preserve the argument parsing exactly** — `/tell Name multi word message`
  must still split identically. T98 deleted a `cmdTell`-quoting test; check
  whether the behaviour it covered still needs a test on the migrated path.

## Atomicity — the silent failure mode

`game` is tried before `action`. Add to `CommandProcessor` and remove from the
game registry **in the same change**, and verify per command that dispatch logs
`registry=action`. Use a control: leave one command unmigrated and confirm it
still logs `registry=game` in the same session, as T99 did with `/target`.

## Tests

Per T99 and the T95 standard: each migrated command reaches its expected
`ActionDispatcher` method, and **every new test proven able to fail in both
directions** — break the routing, show red, restore, show green. Report it.

## Live leg

Chat is directly observable, which makes this family unusually easy to verify
honestly: **send messages and confirm they arrive**, ideally with a second
client observing (T84 established two concurrent clients; concurrency is
per-account, `willeq_test` and `willeq_buddy`). Do not infer from an ack.

Session discipline: verify binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts far longer than you think; `-d 4` plus
`--log-level=DEBUG`; restore DB state and confirm by `SELECT` **after logout**;
clean teardown, delete the lock.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`** — that is
  the dependency cycle. Migration goes upward only.
- Do not migrate renderer commands (none should be in Chat, but know the line).
- Do not touch `ConsoleInputHandler`'s vocabulary — report divergence only.
- Do not migrate beyond Chat. **STOP and report if the recipe does not fit** —
  that is more valuable than a partial migration, and this batch exists partly
  to test the recipe on a second shape.

## Verification

Full build and suite (expect 2121 total plus new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens must not move.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T100-chat`, off
`fable` tip `960c7fbb`. Diff against the merge-base (`git diff --stat
960c7fbb`) is 5 files, 457 insertions / 132 deletions — territory-clean: only
`include/client/action/command_processor.h`, `src/client/action/
command_processor.cpp`, `src/client/eq_commands.cpp` (removals), `tests/
test_command_processor.cpp`, and `refactor/deferred.md`. Note: `fable`'s tip
moved during this session (T101 merged in parallel, slot2) — `git diff
fable` now shows T101's changes too; the merge-base diff above is the
accurate scope for this unit. Full evidence: `/mnt/projects/fable/review/T100/`
(README + four filtered live-session logs).

### Confirmed Chat membership

Re-derived from `eq_commands.cpp`'s `RegisterSocialCommands()`, not trusted
from the plan's prose (which matched exactly): the game registry's Chat
category held exactly 9 commands — `say` (alias `s`), `shout` (alias `sho`),
`ooc` (alias `o`), `auction` (alias `auc`), `gsay` (alias `g`), `gu` (alias
`guildsay`), `tell` (aliases `t`/`msg`, `requiresArgs=true`), `emote`
(aliases `em`/`me`), `reply` (alias `r`). All 9 are what this unit touched.

### Which implementation was ported, and why

T97's divergence table rates the whole family "Equivalent" — both registries
terminate in the same `EverQuest::SendChatMessage()` call either way (`game`
directly; `action` via `ActionDispatcher::sendChatMessage()`/`sendTell()`/
`replyToLastTell()` → `EqActionHandler` → the same `SendChatMessage()`).
Checked before writing any code: `ActionDispatcher`'s three chat methods and
`EqActionHandler`'s implementations of them (`m_eq.SendChatMessage(message,
channelToString(channel))`, with `channelToString()` mapping all 9
`ChatChannel` enumerators to the exact channel-name strings `game`'s
handlers used literally — `"say"`, `"shout"`, `"ooc"`, `"auction"`,
`"group"`, `"guild"`, `"emote"`, `"tell"`) were **already correct and
already implemented**, orphaned only for lack of a live caller (same shape
as T99's Group methods) — so this migration needed **zero new
`ActionDispatcher`/`EqActionHandler` code**, just callers:

| command | source ported | why |
|---|---|---|
| `say`/`ooc`/`auction`/`gsay`/`gu`/`emote` | `game`'s one-line handlers (`SendChatMessage(args, "<channel>")`) | Direct 1:1 port to `m_dispatcher.sendChatMessage(ChatChannel::X, args)` — no logic to translate, `requiresArgs` ported verbatim (`false` for all six, matching `game`) |
| `shout` | pre-existing (added before T99, when T98 kept it alive via the escaping alias "sh") | **Reconciled, not ported fresh** — see below |
| `tell` | `game`'s `tell.handler` verbatim: split on the FIRST space only (target/message), else-if-non-empty-single-word shows a usage message | Preserved exactly per the plan's caution — `/tell Name multi word message` splits identically; live-confirmed (see below) |
| `reply` | `game`'s `reply_cmd`'s args-empty branch only | The other pre-check (`m_last_tell_sender.empty()` → "No one to reply to") could NOT be ported — see Findings #3 below and `refactor/deferred.md`'s T100 entry |

`emote` in particular: T97 flagged this as the one "Live is better" row in
the family — the OLD (T98-deleted) `action` copy silently reinterpreted 9
known words as animation triggers instead of real-EQ text-emote behavior.
The port takes `game`'s text-emote behavior only; the animation
reinterpretation was NOT revived (pinned by
`Emote_DispatchesSendChatMessageEmoteChannel_AsLiteralText`).

### `shout`: reconciled, not duplicated

Before this unit, `shout` existed in **both** registries at once: `game`'s
own `shout`/alias `sho` (primary name, always won the dispatch collision)
and `CommandProcessor`'s pre-T100 `shout`/alias `sh` (added before T99,
reachable only via the one escaping alias, per T97/T98's finding that
`game`'s shout alias is spelled "sho" not "sh"). This unit folded `"sho"`
into `CommandProcessor`'s alias list (now `{"sh", "sho"}`) and **deleted
`game`'s `shout` registration** in the same change — so `shout` is now
fully migrated, exactly like its 8 siblings, instead of living in two
registries simultaneously. Live-confirmed: `/shout`, `/sho`, and `/sh` all
three dispatch through `registry=action` in the same session
(`review/T100/t100_a_testmelee_round1_evidence.log`), and a headless test
(`Shout_PrimaryNameAndBothAliasesAllDispatch`) pins all three spellings
resolving to the identical handler.

### `GameStateSyncBridge` coverage — checked first, found not applicable

Per T99's own addendum, checked BEFORE the live leg: none of the 9 Chat
commands read any `state::GameState` field (unlike Group's
`hasPendingInvite()`/`inGroup()` reads that drove T99's fix).
`grep -n "chat\|Chat" include/client/state/game_state.h` — zero hits;
`state::GameState` has no chat domain at all. Chat is write-only from the
action layer's perspective (send a message; there is nothing to read back
through `GameState`), so there was nothing for the bridge to sync — no fix
needed, no gap found. This is the "most families won't hit this" outcome
T99's recipe predicted, confirmed on the second family tested.

### `ConsoleInputHandler`'s third vocabulary — divergence NARROWED, not widened

Read `console_input_handler.cpp:373-409` (not touched, per the plan's
prohibition). It builds `ChatMessage` structs directly for 5 of the 9 words
(`say`/`tell`/`shout`/`ooc`/`auction` — no `reply`/`gsay`/`gu`/`emote`) and
never reaches either command registry, confirmed. But tracing where those
structs actually go — `InputActionBridge::processChatMessages()`
(`input_action_bridge.cpp:291-321`) — shows it calls
`ActionDispatcher::sendChatMessage()`/`sendTell()` **directly**, the exact
same dispatcher methods this migration's `cmdSay`/`cmdTell`/etc. now call
too. Before T100: `game`'s (then-live) `say`/`tell`/etc. called
`EverQuest::SendChatMessage()` directly while `ConsoleInputHandler`'s path
went through `ActionDispatcher` — two different entry points that happened
to converge on the same terminal call (T97: "Equivalent"). After T100: both
the slash-command path and `ConsoleInputHandler`'s bespoke path terminate
at the identical `ActionDispatcher` method calls — the divergence in
*mechanism* narrowed. The one difference that remains: `ConsoleInputHandler`
bypasses `CommandProcessor::processCommand()` entirely, so it never
produces a `CMD outcome=... registry=...` log line — an observability gap,
not a behavioral one. Reported per the plan's instruction, not touched.

### Atomicity: proof via `registry=`, with a control

Each of the 9 commands was added to `CommandProcessor` and removed from
`eq_commands.cpp`'s `RegisterSocialCommands()` in the same commit — `git
diff --stat 960c7fbb` shows only a shrink in `eq_commands.cpp` (-101 net
lines) and only additions in `command_processor.*`, no window where both
existed. Live proof, `review/T100/`: every one of the 9 commands (`say`,
`shout` via all 3 spellings, `ooc`, `auction`, `gu`, `emote`, `tell`,
`gsay`, `reply`) shows `registry=action` in the `CMD outcome=` line, across
two live clients; `/target` (B's intentionally un-migrated control, exactly
T99's pattern) shows `registry=game` in the **same session** — the dispatch
order itself was not disturbed, only the 9 migrated names moved.

### Live evidence: messages arrived, not inferred from an ack

Two live sessions (round 1's `/groupaccept` lost a ~0.7s timing race
against the invite packet's actual arrival — a scripted-wait miscalculation,
not a routing defect; round 2 fixed the timing). Full detail:
`review/T100/README.md`. Headline:

- **`say`, `auction`, `tell`, `reply` demonstrably arrived on the second
  client** — `[CHAT] Testmelee (say): ...` / `(chan4): ...` / `(tell): ...`
  lines on B's screen with correct sender and content, including `tell`'s
  multi-word message **arriving unsplit**: `[CHAT] Testmelee (tell): A
  sends a multi word tell to test migrated tell command`. This is the
  strongest evidence the plan asked for — a real second client's screen,
  not an `outcome=applied` ack.
- **`gu` reached the server and got a real, legitimate rejection**: "You
  are not in a guild." (Testmelee has no guild on this test server) —
  proves the command routes and reaches `SendChatMessage()`/the server;
  cannot be tested further without a seeded guild.
- **`invite` → `groupaccept` → `gsay` → `disband` retested in round 2
  against a real, server-confirmed group**: both clients logged "Group
  update: 2 members, leader: Testmelee"; `/disband` succeeded ("Left
  group", `outcome=applied`) and **both clients independently logged**
  "Your group has been disbanded" — server-confirmed, not a client-side
  echo, the same standard T99 established.
- **`shout`, `ooc`, and `gsay` were sent correctly (confirmed via the
  `Sending X message` debug line and `outcome=applied registry=action`
  every time, across both rounds, including `gsay` retested against a
  real confirmed group) but never visibly arrived on the second client,
  not even as a self-echo back to the sender** — see Findings below. Not a
  T100 regression (the client-side send path is untouched by this unit and
  identical to what `game`'s deleted registrations called), but flagged
  prominently rather than glossed over, per the plan's "do not infer from
  an ack" instruction.

### Tests, and proof they can fail in both directions

17 new `CommandProcessorTest` cases in `tests/test_command_processor.cpp`:
registration/category/alias checks (`T100_ChatCommandsRegisteredWithChatCategory`),
per-channel dispatch tests for `say`/`ooc`/`auction`/`gsay`/`gu`/`emote`,
five `tell`-specific tests (multi-word split, alias, quotes-not-special —
reviving T98's deleted `QuotesAreNotSpecial` coverage now that `/tell` is
live again — missing-message-single-word, and fully-empty-args-via-
`requiresArgs`), three `reply` tests (with message, alias, no message), and
`Shout_PrimaryNameAndBothAliasesAllDispatch` (all 3 spellings). Suite count:
2121 (T99 baseline) + 17 (16 new tests + 1 pre-existing test's alias list
updated, not counted as new) — confirmed exactly by the full-suite run
below (2137 total). One pre-existing test
(`T98_AllSurvivorsRegisteredAndDispatchByNameAndAlias`) updated in place:
`shout`'s expected alias list grew `{"sh"} → {"sh", "sho"}` to match the
reconciliation.

**Proved failure in both directions**, on two representative commands
(one plain channel-routing command, one with real parsing logic):
1. `CommandProcessor::cmdSay` — replaced its body with an unconditional
   `return ActionResult::Failure("T95 BROKEN FOR TEST")`; reran
   `Say_DispatchesSendChatMessageSayChannel`: **failed** (`result.success`
   false, handler never called).
2. `CommandProcessor::cmdTell` — swapped the `sendTell(target, message)`
   call to `sendTell(message, target)` (argument order bug); reran
   `Tell_SplitsTargetFromMultiWordMessage`/`Tell_Alias_SplitsTargetFromMessage`:
   both **failed** (wrong call recorded).
   Restored from a pre-edit backup (`diff` confirmed byte-identical),
   rebuilt, reran all three: all **passed**.

### Recipe refinements

None needed beyond what T99 already wrote down. This family is the "most
of the remaining ~120 will be cheaper" case T99's estimate predicted: no
new `ActionDispatcher` method, no `GameStateSyncBridge` fix, no new
scripted infrastructure — the whole family was direct ports plus one
alias-list reconciliation. The one addition worth recording for future
units: **when a family's commands don't read `GameState`, say so
explicitly and show the grep that proves it** (rather than silently
skipping the check) — T99's step 6 says "check before the live leg", this
unit's contribution is "and when the answer is no, that's a fast,
legitimate, one-line-grep answer, not a step to skip."

### Actual hours vs T99's estimate

T99 estimated "under an hour each" for families like this one — "most
chat/movement/utility commands, per T97's 'Equivalent' rows" — once the
recipe is followed mechanically, versus 4-6 hours for a family needing new
dispatcher methods or bridge-sync cases. Chat needed neither. Actual time
for this unit, end to end: **~2.5-3 hours** — reading T97/T98/T99/the plan
(~30 min), confirming membership and porting the 9 commands + 17 tests
(~45 min), headless verification (build/suite/visual-gate/ARM,
~25 min wall including two full rebuilds for the fail-both-directions
proof), the live leg (~1h, including the round-1 timing miss, diagnosis,
and a clean round-2 re-run), write-up (~20 min). Longer than T99's "under
an hour" estimate for a no-new-infrastructure family, but the estimate was
explicitly for the mechanical-port time, not the full session; the live
leg (two rounds, cross-client verification of 9 commands plus a full
group-formation retest) was the larger share of the actual time, similar
in proportion to T99's own session.

### Findings (reported, not fixed — full detail in `refactor/deferred.md`'s
T100 entry)

1. `shout`/`ooc`/`gsay` sent correctly (confirmed) but never observed
   arriving cross-client, not even self-echoed, across two live sessions —
   likely a server-side relay characteristic for broadcast-style channels
   this project has apparently never live-tested before now; not a T100
   regression (untouched, identical client code path).
2. `eq_chat.cpp`'s two debug-print ternaries are missing a
   `CHAT_CHANNEL_EMOTE` case (outbound) and a `CHAT_CHANNEL_AUCTION` case
   (inbound) — cosmetic log-label gaps only, pre-existing (F06), the actual
   packets are correct.
3. `CommandProcessor::cmdReply` cannot reproduce game's "No one to reply
   to" message — the dispatcher/handler shape (pre-existing, not changed
   by T100) has no path to expose `EverQuest::m_last_tell_sender`'s empty
   state to the action layer; a `/reply` with no pending tell silently
   succeeds instead of erroring. Not exercised live (both test clients
   always had a pending tell); found by inspection while porting.

### Deviations

None. No `ActionDispatcher`/`EqActionHandler`/`GameStateSyncBridge` code
touched (unlike T99, which had a justified out-of-territory bridge fix —
this family needed none). No production code outside the declared
territory. `ConsoleInputHandler` read but not modified. No scope extension
beyond the 9 Chat commands — no other family touched, no registry
reordering, no renderer command migrated.

### Verification

1. `cmake --build build -j24` — clean, including the two rebuild cycles for
   the fail-both-directions proof (break → red → restore → green).
2. `ctest --test-dir build --output-on-failure` — **2136/2137 passed**, sole
   failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   2121 (T99 baseline) + 16 new tests = 2137, matching exactly.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. Visual gate (`visual_gate`, folded into the `ctest` run) — PASS, 5.76s;
   `git status` clean of any golden files throughout.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — 3 ok, 1 WARN
   (`eq_commands.cpp`, pre-existing over-1500-line file, same as T99 —
   this unit made it **smaller** [-101 net lines vs the merge-base], not a
   new FAIL).
7. Live eqemu leg — lock contended cleanly (T101/slot2 held it at the start
   of the headless-verification-complete point; freed within ~5 polling
   iterations, taken immediately after). Two rounds,
   `willeq_test`/`Testmelee` + `willeq_buddy`/`Testbuddy`, both relocated
   to `arena` (T84/T99's pattern) for fast zone loading. Lock released
   cleanly, `down.sh` run, lock file deleted, Testmelee's DB position
   restored (`zone_id=25, x=-259, y=-1201, z=-5.75, heading=160.25`) and
   confirmed via `SELECT` **after both clients had logged out**; Testbuddy
   was already in `arena` pre-session and left untouched, matching T99.
