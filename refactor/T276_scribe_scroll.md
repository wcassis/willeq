# T276 — scribe a spell scroll into the spellbook (T264 F5)

Slot: slot3. x86. OWNS eqemu (noclobber) for a live scribe. Class A.
Territory: the `ScribeSpellIntent` arm and its handler hop (T264's Review
names them; `tests/test_reachability_sweep.cpp` still lists it blocked —
delist when wired), the OP_MemorizeSpell/scribe sender (eqemu uses the SAME
opcode with a different `scribing` field — verify), CommandProcessor for
the command form, the spellbook popup click path T272 built (reuse it),
tests. Do NOT touch race data/getters (T274 slot1), ui_renderer (T275 slot2).

## Gap
A spell scroll in inventory can be scribed into the spellbook. eqemu:
`Handle_OP_MemorizeSpell` with `scribing=1` writes `character_spells`;
T272's live run already observed `MemorizeSpell response: ... scribing=1`
on the memorize path, so the reply shape is known.

## Scope
1. Read eqemu's `Handle_OP_MemorizeSpell` (zone/client_packet.cpp) for the
   scribe branch: which slot the scroll must be in, what `scribing` value,
   what it validates (class/level/already-known), and what it sends back.
2. Command first (T156 rule): `/scribe <slot|name>` — resolve the scroll
   via `InventoryManager` (T230's `findItemSlotByItemId` pattern), refuse
   locally with the official string when the item is not a scroll or the
   class/level is wrong (only where the client can know), send otherwise.
3. Mouse: if the spellbook popup or inventory click can carry it, route to
   the same handler body (T230/T239 one-body rule). If no gesture exists,
   say so — the intent stays for the UI leg.
4. Tests: scribe request bytes for a scroll in a slot; refusals send
   nothing; the reply adds the spell to the book model. Red/green.
5. Live: `#summonitem` a scroll the character can use (Testcaster is the
   wizard — see review/T272/ for its staging), `/scribe`, `character_spells`
   gains the row, the spellbook popup lists it, then `/mem` + `/cast` it.
   Restore rows.

## Verification
Full ctest; visual gate if the popup changed; Review; evidence review/T276/.
Commit: `T276: spell scrolls can be scribed`.
