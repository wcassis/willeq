# T222 — /reply with no pending tell; rejectTradeRequest never fires m_on_cancelled

Slot: slot2. x86. No eqemu at start (T220 holds it); code+tests first, live
leg only if the lock is free when you get there — otherwise report pending.
Class A backlog items (deferred.md T100 / T103 entries). Do NOT touch
include/client/bridge/*, src/client/bridge/*, eq_bridge.cpp (T220 territory).

## Defect 1 — /reply (T100, command_processor.cpp ~1043)
`/reply` with no pending tell silently succeeds because `m_last_tell_sender`
is not reachable from the action layer. Add the smallest accessor (e.g.
`hasTellToReplyTo()` / `lastTellSender()` on the handler interface), make
`/reply` fail with "You have no one to reply to" (match the official
client's wording if a string exists in eq_strings), test it.

## Defect 2 — rejectTradeRequest (T103, trade_manager.cpp ~54-63)
`TradeManager::rejectTradeRequest()` never fires `m_on_cancelled`; the action
layer works around it. Fire the callback, remove the workaround if it is now
redundant (verify by test), and add the missing own-side offered-items
projection field to TradeState ONLY if it is a one-liner with a reader —
otherwise leave it and say so.

## Verification
Red/green measured for each; full ctest; write the Review into this file;
evidence review/T222/. Commit: `T222: /reply refuses with no pending tell; trade reject fires its callback`.

## Review (2026-08-25, slot2, base fable d7925e08)

### Defect 1 — /reply with no pending tell (T100)
- Root cause confirmed as recorded: `m_last_tell_sender` lives on
  `EverQuest`; `IActionHandler` exposed nothing for the dispatcher to check,
  so `ActionDispatcher::replyToLastTell()` returned `Success()` and
  `EqActionHandler::replyToLastTell()` silently LOG_DEBUG'd and returned.
- Fix: one new pure virtual `IActionHandler::hasTellToReplyTo() const`
  (`include/client/action/action_dispatcher.h`); `EqActionHandler` returns
  `!m_eq.GetLastTellSender().empty()`; `ActionDispatcher::replyToLastTell()`
  now returns `Failure("You have no one to reply to.")` before touching the
  handler. `cmdReply` untouched except its comment (the check belongs in the
  dispatcher, so the R-key/`openReplyToTell` path benefits too when wired).
- Wording: searched `/mnt/projects/EverQuestP1999/eqstr_us.txt` for
  "reply" and "no one" — no official string for this case (4517 "Message to
  forward or reply to was not found." is the mail window; 13247 is the duel
  refusal). Client-authored string in the same register.
- `RecordingActionHandler` gained `bool tellToReplyTo = true` (query not
  recorded, so no existing call-log assertion moved).
- Red/green (review/T222/red_*.txt, green_*.txt):
  - `ActionDispatcherTest.ReplyToLastTell_NoPendingTell_FailsWithoutDispatching`
    RED (Success + handler called) -> GREEN.
  - `CommandProcessorTest.Reply_NoPendingTell_FailsWithMessageWithoutDispatching`
    RED -> GREEN.
  - `ZoneChatTest.ActionHandler_HasTellToReplyTo_FollowsReceivedTell`
    (real EverQuest + replayed OP_ChannelMessage tell) — green on first run:
    it proves the accessor tracks the real state, it is not the defect test.
  - Note: my first red run of `ReplyToLastTell_WithPendingTell_Dispatches`
    also failed for a test-authoring reason (fixture needs
    `connectHandler()`/`connectZone()`); fixed in the test, not a finding.

### Defect 2 — rejectTradeRequest never fires m_on_cancelled (T103)
- `TradeManager::rejectTradeRequest()` now invokes `m_on_cancelled` after
  `resetTradeState()` (same order as `cancelTrade()`).
- T103's in-territory workaround in `EqActionHandler::declineTrade()`
  (`m_eq.GetGameState().trade().reset()`) REMOVED. Verified by test: with
  the workaround removed and the callback not yet firing,
  `DeclineTrade_ClearsProjectionViaCancelledCallback` and
  `TradeManager_RejectTradeRequest_FiresOnCancelled` both RED
  (review/T222/red_test_eq_action_handler_trade.txt); GREEN after the
  one-line fix. The decline test now also asserts a `TradeCancelled` bridge
  event is published (it never was before for a local decline).
- **Deviation / side effect, flagged:** the shared `setOnCancelled` lambda
  (`eq_merchant.cpp:691`) prints "Trade cancelled." unconditionally, so a
  local decline now shows TWO chat lines: "Trade cancelled." then
  "Declined trade request from Buddy". Asserted explicitly in the decline
  test so it is visible, not hidden. Changing the lambda's message would
  also change the partner-initiated cancel path (out of scope, and
  `eq_merchant.cpp` is shared); left for the orchestrator to rule on.
- **TradeState own-offered-items field: NOT added.** Re-verified at this
  tip: `addItemToTrade()`/`removeItemFromTrade()` still have zero
  production callers (grep over src/ + include/; only comments reference
  them), so there is no writer for such a field and no event carries own
  trade-slot contents — it would be a field with a reader and nothing ever
  setting it. Not a one-liner; left as documented in `trade_state.h`.

### Verification
1. `cmake --build build -j24` — pass.
2. `ctest` (GLES2-config tree): **2769/2770**, the one failure is the
   documented ALSA `AudioManagerTest.PreloadCommonSounds`
   (review/T222/ctest_full.txt). +5 tests from this unit.
3. `scripts/build-gles2-x86.sh` — pass.
4. Visual gate — PASS, goldens untouched (review/T222/visual_gate.txt).
5. `scripts/build-arm-noble.sh` — pass, ARM_EXIT=0 (review/T222/build_arm_noble.txt).
6. `check_file_sizes.sh fable` — 7 ok, 4 warn (pre-existing sizes), 0 fail.

### Live leg
`/mnt/projects/fable/.eqemu-lock` was held by T220 (slot1) when code +
tests completed — **live leg pending** (refusal with no pending tell;
success after a tell from Oraclerogue). Not spin-waited.
