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
 * NOT carried here: travel_type. tools/willeq_db_builder.cpp does not bind
 * spells_us.txt field 122, so the `spells` table has no travel_type column
 * and there is no value to copy (board B163). A field that is always zero
 * is exactly the kind of dead vocabulary this unit is removing; it is added
 * when B163 adds the column, not before.
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
    int32_t     spellAffectIndex = kNoSpellAffectIndex;
};

}  // namespace EQ

#endif  // EQT_SPELL_VISUAL_VOCABULARY_H
