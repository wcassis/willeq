# T251 — "You are missing <item>." for missing spell components (T246 leftover)

Slot: slot2. x86. NO eqemu. Class A. Territory: src/client/spell/
spell_cast_checks.cpp `checkReagents` (return WHICH component is missing),
include/client/spell/cast_refusal.h, src/client/eq_strings.cpp fallback
rows, the two refusal sites T244 unified (eq_action_handler
beginCastFromGem, eq_bridge CastSpellIntent — message rendering only),
tests. Do NOT touch `checkPlayerState` / the stun flag (T247 slot1),
inventory_manager.* (T250 slot3).

## Gap
T246 refuses with eqstr 272 ("You are missing some required components.")
but the official client uses **433 "You are missing %1."** with the item
name. Also `eq_strings.cpp`'s fallback for 272 says "…required spell
components." vs eqstr's "…required components." (T246 note).

## Scope
1. `checkReagents` reports the first missing component's item id;
   resolve the name via the item database the client already has (grep
   how item names are looked up for tooltips / `/equip` messages — the
   items table in willeq.db or the ItemPacket cache). If no name source is
   available offline for an item never seen, fall back to 272.
2. Both refusal sites render 433 with the name via `GetStringMessage`'s
   %1 substitution (verify it supports substitution; T178 did `%1` for
   WhoAll count).
3. Fix the 272 fallback text to match eqstr exactly.
4. Tests: missing reagent with a known name → "You are missing Bone Chips."
   (or whatever the test spell needs); unknown name → 272 text. Red/green.

## Verification
Full ctest; Review; evidence review/T251/. Commit:
`T251: missing spell components are named`.

## Review (executed 2026-08-26, slot2, branch task/T251-missing-component from fable 8182ee94)

### What changed
- `SpellManager::checkReagents(spell, uint32_t* missing_item_id = nullptr)`
  reports the first short component; `validateCast()` stores it in a new
  `m_last_missing_reagent_id` (reset to 0 on every validation), read via
  `getLastMissingReagentId()`.
- New `EQ::castRefusalMessage(EverQuest&, CastResult)` in
  `src/client/spell/cast_refusal.cpp` (declared in `cast_refusal.h`, id
  `STR_MISSING_NAMED_COMPONENT = 433` added). Both T244 sites --
  `EqActionHandler::beginCastFromGem()` and eq_bridge's `CastSpellIntent`
  arm -- now call it instead of `GetStringMessage(castRefusalStringId())`.
  `castRefusalStringId()` is unchanged (ComponentMissing still maps to 272;
  T244's table test stays green).
- **Name source used: `InventoryManager::getItemById()`** -- the by-id
  `itemCacheById_` that `setItem()` fills from every ItemPacket the client
  has received (inventory, bank, cursor, merchant, loot, trade, item
  links), plus a slot scan. There is NO offline item table: `willeq.db`
  has `spells` (with reagent ids) but no `items` table, and the Titanium
  client ships none either. So an item the client has never been sent
  (e.g. a reagent never carried or seen) has no name locally and the
  refusal stays 272, exactly as Scope 1 specifies.
- `GetFormattedStringMessage()` made public on EverQuest (same reason
  T244 made `GetStringMessage` public); its no-string-DB fallback already
  substitutes `%1` (verified: the harness has no string DB and the 433
  test passes through that path). The string-DB path uses
  `StringDatabase::formatString()`, which also handles `%1`.
- Fallback rows: 272 now reads "You are missing some required
  components." (eqstr_us.txt line 176, exact); 433 "You are missing %1."
  added (eqstr line 337).

### Red / green (review/T251/)
- `red_cast.txt`: on the pre-change tree with the new/updated tests, 6
  FAIL / 1 pass (`--gtest_filter='*Reagent*:*Missing*:*ResolveWithoutStringDb*'`).
- `green_cast.txt`: test_eq_action_handler_cast 23/23 after the change.
- Tests: `CastRefusalStrings_MissingNamedComponent_ResolvesWithoutStringDb`
  (433 row + %1 substitution, no DB), `CastSpell_MissingReagent_...`
  (never-seen item -> 272 exact eqstr text),
  `CastSpell_ReagentCountShort_...` (1 of 2 Bone Chips carried -> "You are
  missing Bone Chips."), `CastSpellIntent_ReagentCountShort_SaysMissingNamedItem`
  (the UI/bridge site, same text), `CastSpell_ReagentOnlyInBank_StillRefused`
  (banked stack does not count but DOES teach the name -> named 433).
- `ctest_full.txt`: **2925/2926**, the one failure the documented
  `AudioManagerTest.PreloadCommonSounds` ALSA quirk.
- `check_file_sizes.txt`: 0 FAIL (the 4 WARNs are pre-existing >1500-line
  TUs; `inventory_manager.*`/`test_eq_commands_equip.cpp` appear only
  because `fable` advanced to T250 after this branch was cut -- this branch
  does not touch them).
- `build-gles2-x86.sh`: exit 0. Not run: visual gate / ARM (no rendering
  change; plan narrows verification to full ctest).

### Deviations / notes
- A THIRD refusal site exists outside the plan's territory: the hotbar
  `HotbarSlotType::Spell` arm in eq_bridge.cpp (~line 1309) still carries
  its own literals ("Insufficient mana", "Spell not ready", "Already
  casting") and says nothing at all for ComponentMissing/OutOfRange/etc.
  It is the T244 drift pattern surviving in one more place; left untouched
  per the plan ("the two refusal sites T244 unified"). Candidate follow-up.
- `InventoryManager::getItemById()` returns `false` (not `nullptr`) on its
  miss path -- harmless (converts to null) but a T250-territory wart, not
  touched.
- eqstr 433 is what eqemu itself sends (`MISSING_SPELL_COMP_ITEM`) when the
  server refuses; the local pre-check now matches that wording one round
  trip earlier.
