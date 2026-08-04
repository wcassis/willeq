#include "client/bridge/game_state_sync_bridge.h"

#include "client/state/game_state.h"
#include "client/inventory/inventory_constants.h"

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
        case state::GameEventType::InventorySlotChanged: {
            // T42: replaces Application::syncInventoryState() (a once-per-tick
            // poll of InventoryManager). Piece 1 of this unit closed the three
            // mutation paths (vendor buy, vendor sell, bank-window population)
            // that used to mutate slots without publishing this event, so the
            // event stream is now a complete substitute for the poll.
            const auto& d = std::get<state::InventorySlotChangedData>(event.data);
            if (eqt::inventory::isEquipmentSlot(d.slotId)) {
                m_state.inventory().setEquipmentOccupied(d.slotId, d.hasItem);
            } else if (eqt::inventory::isGeneralSlot(d.slotId)) {
                m_state.inventory().setGeneralOccupied(d.slotId - eqt::inventory::GENERAL_BEGIN, d.hasItem);
            } else if (eqt::inventory::isBankSlot(d.slotId)) {
                m_state.inventory().setBankOccupied(d.slotId - eqt::inventory::BANK_BEGIN, d.hasItem);
            }
            break;
        }
        default:
            break;
    }

    m_wrapped.applyEvent(event);
}

} // namespace bridge
} // namespace eqt
