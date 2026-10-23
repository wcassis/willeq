/*
 * WillEQ Spell Visual Vocabulary (T381, board B160)
 *
 * The three per-spell facts the RENDER-side spell effect layer needs in
 * order to pick a colour, a cast style and a spells.eff record:
 * resist type, casting school, and spell_affect_index.
 *
 * Why this type exists at all: SpellVisualFX used to read them straight off
 * a `SpellDatabase*` handed to it by setSpellDatabase(). Nothing ever called
 * that setter in the client, so the pointer was permanently null and every
 * consumer took its null branch -- six resist colours, five school colours,
 * the spray cast style and the whole spells.eff resolution layer were
 * unreachable and every spell in the game drew one identical lavender-blue
 * orbital glow (B160, measured by T380).
 *
 * The pointer must NOT simply be handed across the bridge: the only
 * SpellDatabase instance is SpellManager::m_spell_db, owned on the game
 * thread, and its LRU cache (spell_database.h) has no mutex. So the GAME
 * thread copies these three plain values into the SpellCastVisual* events
 * while it still has the SpellData* in hand -- the same "enriched event"
 * shape CLAUDE.md names for InventorySlotChanged -- and the renderer keys
 * off the payload. This struct is trivially copyable by construction; keep
 * it that way.
 *
 * T384 (board B163, closed) added the fourth fact: travelType, spells_us.txt
 * field 122 (eqemu spells_new.TravelType). T381 deliberately left it out
 * because the `spells` table had no travel_type column and an always-zero
 * field is exactly the dead vocabulary T381 was removing; the column exists
 * now (willeq.db version 7), so the value is real -- 3 for the 85 spells
 * with a projectile travel stage, 0 for the other 7,981. It is packed into
 * the padding byte after `school`, so the struct is still 8 bytes
 * (static_assert'd below) and the bridge event payloads did not grow.
 * Carrying it does NOT wire projectiles: nothing consumes it yet (B165).
 */

#ifndef EQT_SPELL_VISUAL_VOCABULARY_H
#define EQT_SPELL_VISUAL_VOCABULARY_H

#include <cstdint>

#include "spell_constants.h"

namespace EQ {

// spell_affect_index sentinel for "this spell names no spells.eff record".
// Mirrors kSpellsEffNoEffectIndex (spells_eff_reader.h) without dragging the
// reader into the event layer; static_assert'd equal in spell_visual_fx.cpp.
constexpr int32_t kNoSpellAffectIndex = -1;

struct SpellVisualVocabulary {
    // false = the game thread had no SpellData for this id (an unknown spell
    // id on the wire). Consumers must then behave exactly as they did before
    // T381: default colour, non-spray style, NoAffectIndex fallback.
    bool known = false;

    ResistType  resistType = ResistType::None;
    SpellSchool school     = SpellSchool::Alteration;

    // spells_us.txt field 122, verbatim. Only two values occur in the
    // shipped data: 0 (no travel stage) and 3 (projectile).
    uint8_t     travelType = 0;

    // B1127: int16 (spells.eff has 256 records) so the struct stays 8 bytes
    // with spellAnim beside it.
    int16_t     spellAffectIndex = kNoSpellAffectIndex;

    // B1127 (#447): spells_us.txt field 145, the spellsnew.eff record the
    // official client draws (0 = none; the field's values reach 3411).
    int16_t     spellAnim = 0;
};

// The struct is carried by value inside two bridge event payloads; T381
// measured that it costs nothing there because it fits the variant's slack.
// Keep it 8 bytes -- travelType went into the padding byte after `school`,
// and B1127 split the int32 index into two int16s (spellAffectIndex, spellAnim).
static_assert(sizeof(SpellVisualVocabulary) == 8,
              "SpellVisualVocabulary must stay 8 bytes (T381/T384/B1127)");

}  // namespace EQ

#endif  // EQT_SPELL_VISUAL_VOCABULARY_H
