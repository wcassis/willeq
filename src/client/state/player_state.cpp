#include "client/state/player_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

PlayerState::PlayerState() = default;

// ========== Position and Movement ==========

// T438 (board B424). A DISCRETE RELOCATION CANCELS AN IN-FLIGHT JUMP, and
// that one line is the whole of B424.
//
// B424 said "fable resolves ground ~71 units too high at the gfaydark Kelethin
// lift": `#goto 340 137 6` then `/loc` reported z 77.0 in two independent
// sessions, where the official client on the same server reports -0.09. T435
// showed the stated mechanism was wrong (nothing in the ground query can
// produce +71) and stopped rather than invent another. T438 instrumented every
// writer of the game-thread player z with its call site and ran ONE `#goto`.
// The whole chain, at millisecond resolution
// (review/T438/logs/fable_session_legc.log):
//
//   10:35:27.487  SetPosLocal (-320.0, -404.0, 14.4944)  movement_state.cpp:514
//   10:35:27.487  SetPosLocal (-320.0, -404.0, 14.4944)  movement_state.cpp:569
//   10:35:27.504  SetPosLocal (-320.0, -404.0, 16.1655)  movement_state.cpp:569
//   10:35:27.519  SetPosition (137.0,  340.0,   6.0000)  zone_component.cpp:138
//   10:35:27.522  SetPosLocal (137.0,  340.0,  17.7788)  movement_state.cpp:569
//   ...
//   10:35:28.488  SetPosLocal (137.0,  340.0,  14.4944)  movement_state.cpp:553
//
// A jump was in flight when the relocation landed. `MovementComponent::
// UpdateJump()` writes `jumpStartZ + height` every tick and, when the arc ends,
// writes `jumpStartZ` FLAT -- a z captured before the relocation, at the old
// place. The commanded z survived three milliseconds. T433's 77.0 is the same
// arithmetic with a jumpStartZ captured on the Kelethin platform.
//
// It also explains T435's other observation, that the renderer stayed at ~6
// while the game thread said 77.0: SetPosLocal writes only the game-thread
// store, so the two really did diverge and neither had "resolved ground" at
// all.
//
// The cancel belongs HERE and not in the jump code because this is the one
// store (T288) and setPosition() is the discrete-relocation path by
// definition -- zone-in, bind, `#goto`, the T332 echo. The per-tick integrator
// uses setPositionNoEvent() and is deliberately untouched, so a jump still
// arcs normally.
void PlayerState::setPosition(float x, float y, float z) {
    m_x = x;
    m_y = y;
    m_z = z;
    if (m_isJumping) {
        m_isJumping = false;
        m_jumpStartZ = z;
    }
    firePlayerMovedEvent();
}

void PlayerState::setPosition(const glm::vec3& pos) {
    setPosition(pos.x, pos.y, pos.z);
}

void PlayerState::setHeading(float heading) {
    m_heading = heading;
    firePlayerMovedEvent();
}

void PlayerState::setPositionAndHeading(float x, float y, float z, float heading) {
    m_x = x;
    m_y = y;
    m_z = z;
    m_heading = heading;
    firePlayerMovedEvent();
}

void PlayerState::setVelocity(float dx, float dy, float dz) {
    m_dx = dx;
    m_dy = dy;
    m_dz = dz;
}

void PlayerState::setMovementMode(MovementMode mode) {
    if (m_movementMode != mode) {
        m_movementMode = mode;
        // Could fire a PlayerMovementModeChanged event here if needed
    }
}

void PlayerState::setPositionState(PositionState state) {
    if (m_positionState != state) {
        m_positionState = state;
        // Could fire a PlayerPositionStateChanged event here if needed
    }
}

void PlayerState::setMovementTarget(float x, float y, float z) {
    m_targetX = x;
    m_targetY = y;
    m_targetZ = z;
    m_hasMovementTarget = true;
}

void PlayerState::clearMovementTarget() {
    m_hasMovementTarget = false;
    m_targetX = 0.0f;
    m_targetY = 0.0f;
    m_targetZ = 0.0f;
}

// ========== Character Stats ==========

void PlayerState::setLevel(uint8_t level) {
    if (m_level != level) {
        m_level = level;
        firePlayerStatsChangedEvent();
    }
}

void PlayerState::setHP(uint32_t current, uint32_t max) {
    bool changed = (m_curHP != current || m_maxHP != max);
    m_curHP = current;
    m_maxHP = max;
    if (changed) {
        firePlayerStatsChangedEvent();
    }
}

void PlayerState::setCurHP(uint32_t hp) {
    if (m_curHP != hp) {
        m_curHP = hp;
        firePlayerStatsChangedEvent();
    }
}

void PlayerState::setMana(uint32_t current, uint32_t max) {
    bool changed = (m_mana != current || m_maxMana != max);
    m_mana = current;
    m_maxMana = max;
    if (changed) {
        firePlayerStatsChangedEvent();
    }
}

void PlayerState::setCurMana(uint32_t mana) {
    if (m_mana != mana) {
        m_mana = mana;
        firePlayerStatsChangedEvent();
    }
}

void PlayerState::setEndurance(uint32_t current, uint32_t max) {
    bool changed = (m_endurance != current || m_maxEndurance != max);
    m_endurance = current;
    m_maxEndurance = max;
    if (changed) {
        firePlayerStatsChangedEvent();
    }
}

void PlayerState::setAttributes(uint32_t str, uint32_t sta, uint32_t cha, uint32_t dex,
                                uint32_t intel, uint32_t agi, uint32_t wis) {
    m_STR = str;
    m_STA = sta;
    m_CHA = cha;
    m_DEX = dex;
    m_INT = intel;
    m_AGI = agi;
    m_WIS = wis;
}

// ========== Currency ==========

void PlayerState::setCurrency(uint32_t plat, uint32_t gold, uint32_t silver, uint32_t copper) {
    m_platinum = plat;
    m_gold = gold;
    m_silver = silver;
    m_copper = copper;
}

void PlayerState::addCurrency(uint32_t plat, uint32_t gold, uint32_t silver, uint32_t copper) {
    setCurrency(m_platinum + plat, m_gold + gold, m_silver + silver, m_copper + copper);
}

uint64_t PlayerState::totalCopperValue() const {
    return static_cast<uint64_t>(m_platinum) * 1000 +
           static_cast<uint64_t>(m_gold) * 100 +
           static_cast<uint64_t>(m_silver) * 10 +
           static_cast<uint64_t>(m_copper);
}

// ========== Bank Currency ==========

void PlayerState::setBankCurrency(uint32_t plat, uint32_t gold, uint32_t silver, uint32_t copper) {
    m_bankPlatinum = plat;
    m_bankGold = gold;
    m_bankSilver = silver;
    m_bankCopper = copper;
}

uint64_t PlayerState::totalBankCopperValue() const {
    return static_cast<uint64_t>(m_bankPlatinum) * 1000 +
           static_cast<uint64_t>(m_bankGold) * 100 +
           static_cast<uint64_t>(m_bankSilver) * 10 +
           static_cast<uint64_t>(m_bankCopper);
}

// ========== Practice Points ==========

void PlayerState::decrementPracticePoints() {
    if (m_practicePoints > 0) {
        --m_practicePoints;
    }
}

// ========== Vendor Interaction ==========

void PlayerState::setVendor(uint16_t npcId, float sellRate, const std::string& name) {
    m_vendorNpcId = npcId;
    m_vendorSellRate = sellRate;
    m_vendorName = name;

    if (m_eventBus && npcId != 0) {
        WindowOpenedData data;
        data.npcId = npcId;
        data.npcName = name;
        data.sellRate = sellRate;
        m_eventBus->publish(GameEventType::VendorWindowOpened, data);
    }
}

void PlayerState::clearVendor() {
    uint16_t oldNpcId = m_vendorNpcId;
    m_vendorNpcId = 0;
    m_vendorSellRate = 1.0f;
    m_vendorName.clear();

    if (m_eventBus && oldNpcId != 0) {
        WindowClosedData data;
        data.npcId = oldNpcId;
        m_eventBus->publish(GameEventType::VendorWindowClosed, data);
    }
}

// ========== Bank Interaction ==========

void PlayerState::setBanker(uint16_t npcId) {
    m_bankerNpcId = npcId;

    if (m_eventBus && npcId != 0) {
        WindowOpenedData data;
        data.npcId = npcId;
        data.npcName = "Banker";
        data.sellRate = 1.0f;
        m_eventBus->publish(GameEventType::BankWindowOpened, data);
    }
}

void PlayerState::clearBanker() {
    uint16_t oldNpcId = m_bankerNpcId;
    m_bankerNpcId = 0;

    if (m_eventBus && oldNpcId != 0) {
        WindowClosedData data;
        data.npcId = oldNpcId;
        m_eventBus->publish(GameEventType::BankWindowClosed, data);
    }
}

// ========== Trainer Interaction ==========

void PlayerState::setTrainer(uint16_t npcId, const std::string& name) {
    m_trainerNpcId = npcId;
    m_trainerName = name;

    // Note: TrainerWindowOpened event is published from eq_merchant.cpp with full skill data.
    // This method only tracks state for polling.
}

void PlayerState::clearTrainer() {
    uint16_t oldNpcId = m_trainerNpcId;
    m_trainerNpcId = 0;
    m_trainerName.clear();

    if (m_eventBus && oldNpcId != 0) {
        WindowClosedData data;
        data.npcId = oldNpcId;
        m_eventBus->publish(GameEventType::TrainerWindowClosed, data);
    }
}

// ========== Loot State ==========

void PlayerState::setLootingCorpse(uint16_t corpseId) {
    m_lootingCorpseId = corpseId;
}

void PlayerState::clearLootingCorpse() {
    m_lootingCorpseId = 0;
}

// ========== Weight ==========

void PlayerState::setWeight(float current, float max) {
    m_weight = current;
    m_maxWeight = max;
}

// ========== Bind Point ==========

void PlayerState::setBindPoint(uint32_t zoneId, float x, float y, float z, float heading) {
    m_bindZoneId = zoneId;
    m_bindX = x;
    m_bindY = y;
    m_bindZ = z;
    m_bindHeading = heading;
}

// ========== Bulk State Setting ==========

void PlayerState::loadFromProfile(const ProfileData& profile) {
    m_name = profile.name;
    m_lastName = profile.lastName;
    m_level = profile.level;
    m_class = profile.classId;
    m_race = profile.race;
    m_gender = profile.gender;
    m_deity = profile.deity;
    m_curHP = profile.curHP;
    m_maxHP = profile.maxHP;
    m_mana = profile.mana;
    m_maxMana = profile.maxMana;
    m_endurance = profile.endurance;
    m_maxEndurance = profile.maxEndurance;
    m_STR = profile.STR;
    m_STA = profile.STA;
    m_CHA = profile.CHA;
    m_DEX = profile.DEX;
    m_INT = profile.INT;
    m_AGI = profile.AGI;
    m_WIS = profile.WIS;
    m_platinum = profile.platinum;
    m_gold = profile.gold;
    m_silver = profile.silver;
    m_copper = profile.copper;
    m_x = profile.x;
    m_y = profile.y;
    m_z = profile.z;
    m_heading = profile.heading;

    // Fire events after bulk load
    firePlayerStatsChangedEvent();
    firePlayerMovedEvent();
}

// ========== Event Firing ==========

void PlayerState::firePlayerMovedEvent() {
    if (m_eventBus) {
        PlayerMovedData data;
        data.x = m_x;
        data.y = m_y;
        data.z = m_z;
        data.heading = m_heading;
        data.dx = m_dx;
        data.dy = m_dy;
        data.dz = m_dz;
        data.isMoving = m_isMoving;
        m_eventBus->publish(GameEventType::PlayerMoved, data);
    }
}

void PlayerState::firePlayerStatsChangedEvent() {
    if (m_eventBus) {
        PlayerStatsChangedData data;
        data.curHP = m_curHP;
        data.maxHP = m_maxHP;
        data.curMana = m_mana;
        data.maxMana = m_maxMana;
        data.curEndurance = m_endurance;
        data.maxEndurance = m_maxEndurance;
        data.level = m_level;
        m_eventBus->publish(GameEventType::PlayerStatsChanged, data);
    }
}

} // namespace state
} // namespace eqt
