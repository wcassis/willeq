# F13 — Split combat-resolution cluster into eq_combat_actions.cpp

Move the combat resolution/loot-corpse packet cluster out of
`src/client/eq.cpp` (10,819 lines). Pure move — no behavior change, no
interface change, no reformatting. (Folding this domain INTO CombatManager is
a future non-mechanical refactor, explicitly not this unit.)

## Scope (orig 17217–17710 @ a0c02cd, ~5 large methods. Enumerate by NAME.)

Move to new `src/client/eq_combat_actions.cpp` the contiguous cluster
containing: the Consider handler, the Action handler, the Damage handler
(~237 lines by itself), the MoneyOnCorpse handler, and the loot-item
handler(s). F07's review notes it deliberately left the loot-corpse family
(`ZoneProcessMoneyOnCorpse`, `RequestLootCorpse`, `LootItemFromCorpse`,
`LootAllFromCorpse`, `DestroyAllCorpseLoot`) for a later unit — if they are
in or contiguous with this cluster, they belong to this unit; enumerate and
document.

## Steps

1. Locate the cluster; list every `EverQuest::` definition found; note
   interleaved out-of-scope methods (leave + document).
2. Create `src/client/eq_combat_actions.cpp`: header comment + pruned
   includes (CombatManager full type likely needed — see F06's combat.h
   precedent).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local static helpers: usual rule.
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_combat_actions.cpp` — ok.
- eq.cpp shrinks by ~450+ lines.

## Review

**Cluster located:** contiguous block at eq.cpp lines 8359–8853 (as of
`fable` @ 06e6f5f, before this unit's edits), immediately preceded by
`GetMovementSpeed()` and immediately followed by `QueuePacket()`. Line
numbers had shifted substantially from the plan's orig-a0c02cd estimate
(17217–17710) due to prior Batch F units already landed on `fable`; located
by name via `grep -n` rather than by the stale line range.

**Moved methods (5, in original order), all `EverQuest::`:**
- `ZoneProcessConsider` — Consider handler
- `ZoneProcessAction` — Action/spell-effect handler
- `ZoneProcessDamage` — Damage handler (~237 lines), including its
  immediately-preceding file-local static helper `getDamageVerb`
  (damage-type → verb pair)
- `ZoneProcessMoneyOnCorpse` — MoneyOnCorpse handler
- `ZoneProcessLootItem` — loot-item handler

**Loot-corpse family disposition:** `RequestLootCorpse`, `LootItemFromCorpse`,
`LootAllFromCorpse`, `DestroyAllCorpseLoot` (and `CloseLootWindow`, not
enumerated by the plan) live at eq.cpp lines ~10449–10604 — **not** contiguous
with the moved cluster. Roughly 1,600 lines separate them, including
unrelated methods (`ProcessBridgeEvents`, `UpdateInventoryStats`,
`SaveHotbarConfig`, `LoadHotbarConfig`, etc). Per the plan's contiguity rule,
these are left in `eq.cpp` for a later unit.

**Helper analysis:** `getDamageVerb` (file-local `static`, damage-type →
{second_person, third_person} verb pair) is used only by `ZoneProcessDamage`
and was moved with it, kept `static` (no de-static needed; still file-local
to the new TU).

**Includes:** pruned to what the moved code needs — `client/eq.h`,
`client/combat.h`, `client/spell/spell_manager.h`, `client/trade_manager.h`,
`client/animation_constants.h`, `common/logging.h`, `common/packet_structs.h`,
`common/name_utils.h`, `client/bridge/game_state_bridge.h`, plus
`client/chat/chat_message_buffer.h` under `#ifdef EQT_HAS_GRAPHICS` (needed
for `eqt::ui::ChatChannel::System`, matching F06's precedent) and
`client/audio/audio_manager.h` + `client/audio/sound_assets.h` under
`#ifdef WITH_AUDIO` (needed for `m_audio_manager` and `EQT::Audio::SoundId`).
Added `<sstream>` explicitly (not transitively available from `eq.h`; used
by `ZoneProcessLootItem`'s `std::stringstream` field parser) — the one
necessary-include deviation from pure verbatim move, per the plan's
"usual rule" for file-local needs.

**Line counts:**
- `eq.cpp`: 10,819 → 10,324 lines (−495)
- `src/client/eq_combat_actions.cpp`: 529 lines (new)

**CMakeLists.txt:** added `src/client/eq_combat_actions.cpp` to
`CLIENT_CORE_SOURCES` immediately after `eq_chat.cpp`.

**Verifier summary:**
- `cmake --build build -j24` — clean (only pre-existing unrelated
  `-Wpointer-arith` warnings from untouched code).
- `ctest --test-dir build` — 1191/1192 passed; sole failure
  `AudioManagerTest.PreloadCommonSounds` is the documented
  pre-existing ALSA-environment exception in `refactor/test_baseline.txt`.
- `python3 scripts/refactor/verify_move_only.py 06e6f5f..HEAD` — **PASS**
  (move-only modulo boilerplate: +30/-0 lines, i.e. the added includes and
  header comment; `CMakeLists.txt` flagged as non-C++ and reviewed
  separately — single-line addition).
- `check_file_sizes.sh --files eq_combat_actions.cpp` — **ok** (529 lines).

**Deviations:** none beyond the documented necessary include (`<sstream>`)
and the standard header-comment/pruned-includes boilerplate common to all
Batch F units.
