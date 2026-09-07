# T436 — the merchant, bank and cursor-item windows from the official XML (B429)

Class B (feature; "item icons seem critical" — Will 2026-09-07; three
producers fill an `iconId` nothing draws), slot1, no server (T428 holds
the lock). Read B429, T430's §Review (the icon sheets, `item_icon_sheets`,
the five draw sites, `IDW_IconButton`), T413/T415's XML-placement method
(`inventory_layout.h` rule: oracle placements scaled from the oracle
window size), T434's §Review (`options_layout.h` control table pattern,
`hitTest…` skipping unwired controls, the persistence writer), T342's
§Review (the measured buy/sell/coin behaviour: BuyItem on every
denomination, sell = BuyItem, combine silent), the merchant and bank
producers (`src/client/merchant/merchant.cpp`, `merchant_trade.cpp`,
the bank component), and the static-panel seam.

XML: `EQUI_MerchantWnd.xml`, `EQUI_BankWnd.xml` (NOT `BigBankWnd`/
`GuildBankWnd`/`BarterMerchantWnd`/`PointMerchantWnd` — post-Velious, the
era rule), `EQUI_CursorAttachment.xml`.

1. Merchant window: the slot grid (`A_RecessedBox` buttons), the item
   list, price, Buy/Sell buttons, quantity — every element quoted from
   the XML; the slot cells draw icons via T430's sheets; the list is fed
   by the existing merchant producers (`/vendorlist` is the seam). Buy
   and Sell buttons route to the existing `/buy` `/sell` handlers (no new
   protocol code). Opens when the server sends the merchant open packet
   (read the producer), closes with ESC/close button per T363/T368 shape.
2. Bank window: the 8 bank slots (+ shared? quote the era XML) drawing
   icons; the `/bank` seam; opens on the bank packet.
3. Cursor attachment: the item held on the cursor draws its icon at the
   mouse (`EQUI_CursorAttachment.xml`), which is what makes pick-up/
   drop visible at all. Read how fable models the cursor item today
   (inventory slot 0 / `cursorItem`) and draw that.
4. `include/client/graphics/ui/CLAUDE.md`'s panel table gains the three
   rows; `ui_panel_capture` gains the three panels (with real icon ids in
   the fixtures, T430's lesson) into `review/T436/`.
5. Tests pre-fix failing: each panel renders its XML elements at the
   scaled placements; the merchant list shows a fed item with its icon and
   price; Buy hit → the existing buy intent; cursor item draws at the
   mouse.

## Proof

Build (background), `ctest -j6` (no expected reds on this tip), pre-fix/
post-fix, G05 in place (three render passes are added), visual gate
standalone + ARM (no image golden should move), file sizes (new files
under 1,500; extract per panel as T434 did). Strike B429 with proof. Live
proof (a real vendor, the T342 rig) is OWED to a lock unit — say so in the
Review, do not fake it.

STOP: an image golden moves.
