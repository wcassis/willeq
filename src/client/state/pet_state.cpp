#include "client/state/pet_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

PetState::PetState() = default;

// ========== Pet Mutations ==========

void PetState::setPet(uint16_t spawnId, const std::string& name, uint8_t level) {
    bool hadPet = hasPet();

    m_petSpawnId = spawnId;
    m_petName = name;
    m_petLevel = level;
    m_petHPPercent = 100;
    m_petManaPercent = 100;

    // Reset button states for new pet
    m_buttonStates.fill(false);

    if (spawnId != 0) {
        firePetCreatedEvent();
    } else if (hadPet) {
        firePetRemovedEvent();
    }
}

void PetState::clearPet() {
    if (!hasPet()) {
        return;
    }

    m_petSpawnId = 0;
    m_petName.clear();
    m_petLevel = 0;
    m_petHPPercent = 100;
    m_petManaPercent = 100;
    m_buttonStates.fill(false);

    firePetRemovedEvent();
}

void PetState::updatePetHP(uint8_t hpPercent) {
    if (m_petHPPercent != hpPercent) {
        m_petHPPercent = hpPercent;
        firePetStatsChangedEvent();
    }
}

void PetState::updatePetMana(uint8_t manaPercent) {
    if (m_petManaPercent != manaPercent) {
        m_petManaPercent = manaPercent;
        firePetStatsChangedEvent();
    }
}

void PetState::updatePetStats(uint8_t hpPercent, uint8_t manaPercent) {
    bool changed = (m_petHPPercent != hpPercent) || (m_petManaPercent != manaPercent);
    m_petHPPercent = hpPercent;
    m_petManaPercent = manaPercent;
    if (changed) {
        firePetStatsChangedEvent();
    }
}

// ========== Button States ==========

bool PetState::getButtonState(uint8_t button) const {
    if (button >= PET_BUTTON_COUNT) {
        return false;
    }
    return m_buttonStates[button];
}

void PetState::setButtonState(uint8_t button, bool state) {
    if (button >= PET_BUTTON_COUNT) {
        return;
    }
    if (m_buttonStates[button] != state) {
        m_buttonStates[button] = state;
        firePetButtonStateChangedEvent(button);
    }
}

// ========== Reset ==========

void PetState::reset() {
    bool hadPet = hasPet();

    m_petSpawnId = 0;
    m_petName.clear();
    m_petLevel = 0;
    m_petHPPercent = 100;
    m_petManaPercent = 100;
    m_buttonStates.fill(false);

    if (hadPet) {
        firePetRemovedEvent();
    }
}

// ========== Event Firing ==========

void PetState::firePetCreatedEvent() {
    if (m_eventBus) {
        PetCreatedData data;
        data.spawnId = m_petSpawnId;
        data.name = m_petName;
        data.level = m_petLevel;
        m_eventBus->publish(GameEventType::PetCreated, data);
    }
}

void PetState::firePetRemovedEvent() {
    if (m_eventBus) {
        PetRemovedData data;
        data.spawnId = m_petSpawnId;
        data.name = m_petName;
        m_eventBus->publish(GameEventType::PetRemoved, data);
    }
}

void PetState::firePetStatsChangedEvent() {
    if (m_eventBus) {
        PetStatsChangedData data;
        data.spawnId = m_petSpawnId;
        data.hpPercent = m_petHPPercent;
        data.manaPercent = m_petManaPercent;
        m_eventBus->publish(GameEventType::PetStatsChanged, data);
    }
}

void PetState::firePetButtonStateChangedEvent(uint8_t button) {
    if (m_eventBus) {
        PetButtonStateChangedData data;
        data.button = button;
        data.state = m_buttonStates[button];
        m_eventBus->publish(GameEventType::PetButtonStateChanged, data);
    }
}

} // namespace state
} // namespace eqt
