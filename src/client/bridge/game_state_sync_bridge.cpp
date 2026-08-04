#include "client/bridge/game_state_sync_bridge.h"

#include "client/state/game_state.h"

namespace eqt {
namespace bridge {

void GameStateSyncBridge::applyEvent(const state::GameEvent& event) {
    // T40: narrow switch, pet events only. Every other event delegates
    // untouched -- do not pre-build for later syncs here.
    switch (event.type) {
        case state::GameEventType::PetCreated: {
            const auto& d = std::get<state::PetCreatedData>(event.data);
            m_state.pet().setPet(d.spawnId, d.name, d.level);
            break;
        }
        case state::GameEventType::PetRemoved: {
            m_state.pet().clearPet();
            break;
        }
        case state::GameEventType::PetStatsChanged: {
            const auto& d = std::get<state::PetStatsChangedData>(event.data);
            m_state.pet().updatePetStats(d.hpPercent, d.manaPercent);
            break;
        }
        case state::GameEventType::PetButtonStateChanged: {
            const auto& d = std::get<state::PetButtonStateChangedData>(event.data);
            m_state.pet().setButtonState(d.button, d.state);
            break;
        }
        default:
            break;
    }

    m_wrapped.applyEvent(event);
}

} // namespace bridge
} // namespace eqt
