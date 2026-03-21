# D06a: Publish Inventory + Currency Events

## Plan

### Overview

Add event publishing alongside existing direct renderer calls for inventory slot
changes, cursor item changes, equipment stat changes, and currency updates.

### Steps

1. Publish InventorySlotChanged in `ZoneProcessMoveItem()` (line ~3541):
   - After `m_inventory_manager->processMoveItemResponse(p)`, extract from_slot
     and to_slot from packet, push InventorySlotChanged for each slot
   - Check if slot has item via `m_inventory_manager->getItem(slot)`

2. Publish InventorySlotChanged in `ZoneProcessDeleteItem()` (line ~3561):
   - After `m_inventory_manager->processDeleteItemResponse(p)`, extract slot
     from packet, push InventorySlotChanged with hasItem=false

3. Publish CurrencyChanged in `ZoneProcessMoneyUpdate()` (line ~4809):
   - After currency values are updated (m_platinum etc.), push CurrencyChanged

4. Publish CurrencyChanged in bank currency move callback (line ~4454):
   - After local currency update in `setOnBankCurrencyMove` lambda, push
     CurrencyChanged for inventory currency
   - Also push BankCurrencyChanged for bank currency

5. Publish BankCurrencyChanged in bank currency convert callback (line ~4523):
   - After local bank currency update in `setOnBankCurrencyConvert` lambda,
     push BankCurrencyChanged

6. Publish EquipmentStatsChanged in `UpdateInventoryStats()` (line ~19990):
   - After `m_renderer->updateCharacterStats(...)`, push EquipmentStatsChanged
     with AC, ATK, HP, mana, weight from equipment
   - Place after existing PlayerStatsChanged push

7. Publish CurrencyChanged in `UpdateInventoryStats()`:
   - Already publishes PlayerStatsChanged there; add CurrencyChanged since
     updateCharacterStats() includes currency values

8. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged
- Bridge receives inventory and currency events
- All push calls guarded by `if (m_bridge)`
- No new warnings or errors in build
- Zone-loading-time inventory processing does NOT publish events
  (ZoneProcessCharInventory is bulk load)

## Review

All steps completed as planned. No deviations.

Call sites covered:
- ZoneProcessMoveItem: InventorySlotChanged × 2 (from_slot + to_slot)
- ZoneProcessDeleteItem: InventorySlotChanged × 1 (deleted slot, hasItem=false)
- ZoneProcessMoneyUpdate: CurrencyChanged
- Bank currency move callback: CurrencyChanged + BankCurrencyChanged
- Bank currency convert callback: BankCurrencyChanged
- UpdateInventoryStats: EquipmentStatsChanged + CurrencyChanged
  (alongside existing PlayerStatsChanged push from D05)

ZoneProcessCharInventory correctly excluded — bulk zone-load operation.
All 7 push sites guarded by `if (m_bridge)`.

Build succeeds, 73 relevant tests pass. All acceptance criteria met.
