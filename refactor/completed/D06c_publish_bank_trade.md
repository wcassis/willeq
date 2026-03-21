# D06c: Publish Bank + Trade Events

## Plan

### Overview

Add event publishing alongside existing direct renderer calls for bank window
operations and trade window operations.

### Steps

1. Publish BankWindowOpened in `OpenBankWindow()` (line ~5173):
   - After `m_renderer->getWindowManager()->openBankWindow()`, push BankWindowOpened
     with npcId and npcName

2. Publish BankWindowClosed in `CloseBankWindow()` (line ~5196):
   - After `m_renderer->getWindowManager()->closeBankWindow()`, push BankWindowClosed
     with npcId (before clearing m_banker_npc_id)

3. Publish TradeStarted in `setOnStateChanged` callback (line ~4070):
   - When state == TradeState::Active, after `wm->openTradeWindow(...)`, push TradeStarted
     with partnerId, partnerName, isNpc

4. Publish TradeCancelled in `setOnStateChanged` callback (line ~4075):
   - When state == TradeState::None, after `wm->closeTradeWindow()`, push TradeCancelled

5. Publish TradeItemUpdated in `setOnItemUpdated` callback (line ~4079):
   - After partner item updated in trade window, push TradeItemUpdated
     with who=1 (partner), slot, itemId, itemName

6. Publish TradeAcceptStateChanged in `setOnAcceptStateChanged` callback (line ~4116):
   - After renderer accept state updates, push TradeAcceptStateChanged
     with ownAccepted and partnerAccepted

7. Publish TradeCompleted in `setOnCompleted` callback (line ~4174):
   - After `wm->closeTradeWindow(false)`, push TradeCompleted

8. Publish TradeCancelled in `setOnCancelled` callback (line ~4181):
   - After `wm->closeTradeWindow(true)`, push TradeCancelled

9. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged
- Bridge receives bank and trade events
- All push calls guarded by `if (m_bridge)`
- No new warnings or errors in build

## Review

All steps completed as planned. No deviations.

Call sites covered:
- Bank: OpenBankWindow (BankWindowOpened), CloseBankWindow (BankWindowClosed)
- Trade: setOnStateChanged Active (TradeStarted), setOnStateChanged None
  (TradeCancelled), setOnItemUpdated partner add/clear (TradeItemUpdated × 2),
  setOnAcceptStateChanged (TradeAcceptStateChanged), setOnCompleted
  (TradeCompleted), setOnCancelled (TradeCancelled)

TradeCancelled and TradeCompleted reuse TradeAcceptStateChangedData as the
variant carrier since no dedicated data structs exist for these event types.
The event type field on GameEvent properly identifies the event.

BankWindowClosed published before clearing m_banker_npc_id.
All 10 push sites guarded by `if (m_bridge)`.

Build succeeds, 73 relevant tests pass. All acceptance criteria met.
