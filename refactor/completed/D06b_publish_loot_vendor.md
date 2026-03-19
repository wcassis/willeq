# D06b: Publish Loot + Vendor Events

## Plan

### Overview

Add event publishing alongside existing direct renderer calls for loot window
operations and vendor window operations.

### Steps

1. Publish LootWindowOpened in `RequestLootCorpse()` (line ~20132):
   - After `m_renderer->getWindowManager()->openLootWindow(...)`, push LootWindowOpened
     with corpseId and corpseName

2. Publish LootItemAdded in `ZoneProcessLootItemToUI()` (line ~4278):
   - After `m_renderer->getWindowManager()->addLootItem(...)`, push LootItemAdded
     with corpseId, slot, itemId, itemName

3. Publish LootItemRemoved in `ZoneProcessLootedItemToInventory()` (line ~4329):
   - After `m_renderer->getWindowManager()->removeLootItem(...)`, push LootItemRemoved
     with corpseId and slot

4. Publish LootWindowClosed in `CloseLootWindow()` (line ~20264):
   - After `m_renderer->getWindowManager()->closeLootWindow()`, push LootWindowClosed
     with corpseId

5. Publish VendorWindowOpened in `ZoneProcessShopRequest()` (line ~4728):
   - After `m_renderer->getWindowManager()->openVendorWindow(...)`, push VendorWindowOpened
     with npcId, npcName, sellRate

6. Publish VendorItemAdded in `ZoneProcessVendorItemToUI()` (line ~4977):
   - After `m_renderer->getWindowManager()->addVendorItem(...)`, push VendorItemAdded
     with vendorSlot, itemId, itemName, price, quantity

7. Publish VendorWindowClosed in `ZoneProcessShopEndConfirm()` (line ~4934):
   - After `m_renderer->getWindowManager()->closeVendorWindow()`, push VendorWindowClosed

8. Publish VendorWindowClosed in `RequestCloseVendor()` (line ~5081):
   - After `m_renderer->getWindowManager()->closeVendorWindow()`, push VendorWindowClosed

9. Publish VendorWindowClosed in death handler (line ~13332):
   - After `closeVendorWindow()`, push VendorWindowClosed

10. Publish VendorWindowClosed in vendor death handler (line ~13357):
    - After `closeVendorWindow()`, push VendorWindowClosed

11. Publish VendorWindowClosed in zone change handler (line ~13601):
    - After `closeVendorWindow()`, push VendorWindowClosed

12. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged
- Bridge receives loot and vendor events
- All push calls guarded by `if (m_bridge)`
- No new warnings or errors in build

## Review

All steps completed as planned. No deviations.

Call sites covered:
- Loot: RequestLootCorpse (LootWindowOpened), ZoneProcessLootItemToUI
  (LootItemAdded), ZoneProcessLootedItemToInventory (LootItemRemoved),
  CloseLootWindow (LootWindowClosed)
- Vendor: ZoneProcessShopRequest (VendorWindowOpened),
  ZoneProcessVendorItemToUI (VendorItemAdded),
  ZoneProcessShopEndConfirm (VendorWindowClosed),
  CloseVendorWindow (VendorWindowClosed),
  death handler (VendorWindowClosed),
  vendor death handler (VendorWindowClosed),
  zone change handler (VendorWindowClosed)

VendorWindowClosed published before clearing m_vendor_npc_id so the
event carries the correct NPC ID. All 11 push sites guarded by `if (m_bridge)`.

Build succeeds, 73 relevant tests pass. All acceptance criteria met.
