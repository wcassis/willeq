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
