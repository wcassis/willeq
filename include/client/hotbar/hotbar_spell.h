#pragma once

#include "client/state/spell_state.h"
#include "common/packets/common.h"

namespace eqt {

// One spell-id rule for both hotbar input routes. Return a 0-based visible
// gem, or -1 when the spell is not memorized in a visible gem.
inline int FindHotbarSpellGem(const state::SpellState& spells, uint32_t spellId) {
    if (spellId == 0 || spellId == state::SPELL_ID_UNKNOWN) return -1;
    for (int gem = 0; gem < EQT::VISIBLE_SPELL_GEM_COUNT; ++gem) {
        const auto index = static_cast<uint8_t>(gem);
        if (spells.hasSpellMemorized(index) &&
            spells.getGemState(index) != state::SpellGemState::Empty &&
            spells.getGemSpellId(index) == spellId) return gem;
    }
    return -1;
}

} // namespace eqt
