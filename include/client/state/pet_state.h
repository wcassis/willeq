#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Number of pet button states (from pet_constants.h PET_BUTTON_COUNT)
static constexpr int PET_BUTTON_COUNT = 10;

/**
 * PetState - Contains pet-related state data.
 *
 * This class encapsulates state related to the player's pet including
 * spawn ID, stats, and UI button toggle states.
 */
class PetState {
public:
    PetState();
    ~PetState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Pet Existence ==========

    /**
     * Check if player has a pet.
     */
    bool hasPet() const { return m_petSpawnId != 0; }

    /**
     * Get pet spawn ID (0 = no pet).
     */
    uint16_t petSpawnId() const { return m_petSpawnId; }

    // ========== Pet Identity ==========

    /**
     * Get pet name.
     */
    const std::string& petName() const { return m_petName; }

    /**
     * Get pet level.
     */
    uint8_t petLevel() const { return m_petLevel; }

    // ========== Pet Stats ==========

    /**
     * Get pet HP as percentage (0-100).
     */
    uint8_t petHPPercent() const { return m_petHPPercent; }

    /**
     * Get pet mana as percentage (0-100).
     */
    uint8_t petManaPercent() const { return m_petManaPercent; }

    // ========== Pet Mutations ==========

    /**
     * Set pet (when pet is created/summoned).
     * Fires PetCreated event.
     */
    void setPet(uint16_t spawnId, const std::string& name, uint8_t level);

    /**
     * Clear pet (when pet dies or is dismissed).
     * Fires PetRemoved event.
     */
    void clearPet();

    /**
     * Update pet HP percentage.
     * Fires PetStatsChanged event.
     */
    void updatePetHP(uint8_t hpPercent);

    /**
     * Update pet mana percentage.
     * Fires PetStatsChanged event.
     */
    void updatePetMana(uint8_t manaPercent);

    /**
     * Update both HP and mana percentages.
     * Fires PetStatsChanged event.
     */
    void updatePetStats(uint8_t hpPercent, uint8_t manaPercent);

    // ========== Button States ==========

    /**
     * Get button state (pressed/unpressed).
     * @param button Button index (0-9, see pet_constants.h PetButton enum)
     * @return true if button is pressed/active
     */
    bool getButtonState(uint8_t button) const;

    /**
     * Set button state.
     * Fires PetButtonStateChanged event.
     * @param button Button index (0-9)
     * @param state New button state
     */
    void setButtonState(uint8_t button, bool state);

    /**
     * Get all button states as array.
     */
    const std::array<bool, PET_BUTTON_COUNT>& buttonStates() const { return m_buttonStates; }

    // ========== Reset ==========

    /**
     * Reset all pet state (for zone changes, logout, etc.).
     */
    void reset();

private:
    void firePetCreatedEvent();
    void firePetRemovedEvent();
    void firePetStatsChangedEvent();
    void firePetButtonStateChangedEvent(uint8_t button);

    EventBus* m_eventBus = nullptr;

    // Pet identity
    uint16_t m_petSpawnId = 0;
    std::string m_petName;
    uint8_t m_petLevel = 0;

    // Pet stats
    uint8_t m_petHPPercent = 100;
    uint8_t m_petManaPercent = 100;

    // Button toggle states
    std::array<bool, PET_BUTTON_COUNT> m_buttonStates = {};
};

} // namespace state
} // namespace eqt
