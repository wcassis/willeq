#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <glm/glm.hpp>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Movement modes (matches EverQuest enum)
enum class MovementMode {
    Run = 0,
    Walk = 1,
    Sneak = 2
};

// Position states (matches EverQuest enum)
enum class PositionState {
    Standing = 0,
    Sitting = 1,
    Crouching = 2,
    FeignDeath = 3,
    Dead = 4
};

/**
 * PlayerState - Contains all player character state data.
 *
 * This class encapsulates all state related to the player character including
 * position, stats, attributes, currency, movement state, and flags. It provides
 * getters for read access and setters that optionally fire events through the
 * EventBus when state changes.
 *
 * The EventBus pointer is optional - if null, no events are fired. This allows
 * the state to be used in contexts where event notification is not needed.
 */
class PlayerState {
public:
    PlayerState();
    ~PlayerState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Position and Movement ==========

    // Position getters
    float x() const { return m_x; }
    float y() const { return m_y; }
    float z() const { return m_z; }
    float heading() const { return m_heading; }
    glm::vec3 position() const { return glm::vec3(m_x, m_y, m_z); }
    float size() const { return m_size; }

    // Position setters (fires PlayerMoved event)
    void setPosition(float x, float y, float z);
    void setPosition(const glm::vec3& pos);
    void setHeading(float heading);
    void setPositionAndHeading(float x, float y, float z, float heading);
    void setSize(float size) { m_size = size; }

    // Velocity
    float dx() const { return m_dx; }
    float dy() const { return m_dy; }
    float dz() const { return m_dz; }
    void setVelocity(float dx, float dy, float dz);

    // Animation
    int16_t animation() const { return m_animation; }
    void setAnimation(int16_t anim) { m_animation = anim; }

    // Movement state
    bool isMoving() const { return m_isMoving; }
    void setMoving(bool moving) { m_isMoving = moving; }

    float moveSpeed() const { return m_moveSpeed; }
    void setMoveSpeed(float speed) { m_moveSpeed = speed; }

    uint32_t movementSequence() const { return m_movementSequence; }
    void setMovementSequence(uint32_t seq) { m_movementSequence = seq; }
    void incrementMovementSequence() { ++m_movementSequence; }

    // Movement mode and position state
    MovementMode movementMode() const { return m_movementMode; }
    void setMovementMode(MovementMode mode);

    PositionState positionState() const { return m_positionState; }
    void setPositionState(PositionState state);

    // Movement target (destination)
    float targetX() const { return m_targetX; }
    float targetY() const { return m_targetY; }
    float targetZ() const { return m_targetZ; }
    void setMovementTarget(float x, float y, float z);
    void clearMovementTarget();
    bool hasMovementTarget() const { return m_hasMovementTarget; }

    // Follow target
    const std::string& followTarget() const { return m_followTarget; }
    void setFollowTarget(const std::string& name) { m_followTarget = name; }
    void clearFollowTarget() { m_followTarget.clear(); }
    bool isFollowing() const { return !m_followTarget.empty(); }
    float followDistance() const { return m_followDistance; }
    void setFollowDistance(float dist) { m_followDistance = dist; }

    // Keyboard movement state
    bool moveForward() const { return m_moveForward; }
    bool moveBackward() const { return m_moveBackward; }
    bool turnLeft() const { return m_turnLeft; }
    bool turnRight() const { return m_turnRight; }
    void setMoveForward(bool val) { m_moveForward = val; }
    void setMoveBackward(bool val) { m_moveBackward = val; }
    void setTurnLeft(bool val) { m_turnLeft = val; }
    void setTurnRight(bool val) { m_turnRight = val; }

    // Jump state
    bool isJumping() const { return m_isJumping; }
    void setJumping(bool jumping) { m_isJumping = jumping; }
    float jumpStartZ() const { return m_jumpStartZ; }
    void setJumpStartZ(float z) { m_jumpStartZ = z; }
    std::chrono::steady_clock::time_point jumpStartTime() const { return m_jumpStartTime; }
    void setJumpStartTime(std::chrono::steady_clock::time_point time) { m_jumpStartTime = time; }

    // ========== Character Identity ==========

    uint16_t spawnId() const { return m_spawnId; }
    void setSpawnId(uint16_t id) { m_spawnId = id; }

    uint16_t characterId() const { return m_characterId; }
    void setCharacterId(uint16_t id) { m_characterId = id; }

    const std::string& name() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    const std::string& lastName() const { return m_lastName; }
    void setLastName(const std::string& name) { m_lastName = name; }

    // ========== Character Stats ==========

    uint8_t level() const { return m_level; }
    void setLevel(uint8_t level);

    uint32_t classId() const { return m_class; }
    void setClass(uint32_t classId) { m_class = classId; }

    uint32_t race() const { return m_race; }
    void setRace(uint32_t race) { m_race = race; }

    uint32_t gender() const { return m_gender; }
    void setGender(uint32_t gender) { m_gender = gender; }

    uint32_t deity() const { return m_deity; }
    void setDeity(uint32_t deity) { m_deity = deity; }

    // Health, Mana, Endurance (fires PlayerStatsChanged event)
    uint32_t curHP() const { return m_curHP; }
    uint32_t maxHP() const { return m_maxHP; }
    void setHP(uint32_t current, uint32_t max);
    void setCurHP(uint32_t hp);

    uint32_t curMana() const { return m_mana; }
    uint32_t maxMana() const { return m_maxMana; }
    void setMana(uint32_t current, uint32_t max);
    void setCurMana(uint32_t mana);

    uint32_t curEndurance() const { return m_endurance; }
    uint32_t maxEndurance() const { return m_maxEndurance; }
    void setEndurance(uint32_t current, uint32_t max);

    // Base attributes
    uint32_t STR() const { return m_STR; }
    uint32_t STA() const { return m_STA; }
    uint32_t CHA() const { return m_CHA; }
    uint32_t DEX() const { return m_DEX; }
    uint32_t INT() const { return m_INT; }
    uint32_t AGI() const { return m_AGI; }
    uint32_t WIS() const { return m_WIS; }

    void setSTR(uint32_t val) { m_STR = val; }
    void setSTA(uint32_t val) { m_STA = val; }
    void setCHA(uint32_t val) { m_CHA = val; }
    void setDEX(uint32_t val) { m_DEX = val; }
    void setINT(uint32_t val) { m_INT = val; }
    void setAGI(uint32_t val) { m_AGI = val; }
    void setWIS(uint32_t val) { m_WIS = val; }

    void setAttributes(uint32_t str, uint32_t sta, uint32_t cha, uint32_t dex,
                       uint32_t intel, uint32_t agi, uint32_t wis);

    // ========== Currency ==========

    uint32_t platinum() const { return m_platinum; }
    uint32_t gold() const { return m_gold; }
    uint32_t silver() const { return m_silver; }
    uint32_t copper() const { return m_copper; }

    void setPlatinum(uint32_t val) { m_platinum = val; }
    void setGold(uint32_t val) { m_gold = val; }
    void setSilver(uint32_t val) { m_silver = val; }
    void setCopper(uint32_t val) { m_copper = val; }

    void setCurrency(uint32_t plat, uint32_t gold, uint32_t silver, uint32_t copper);

    // Total currency in copper
    uint64_t totalCopperValue() const;

    // ========== Weight ==========

    float weight() const { return m_weight; }
    float maxWeight() const { return m_maxWeight; }
    void setWeight(float current, float max);

    // ========== Bind Point ==========

    uint32_t bindZoneId() const { return m_bindZoneId; }
    float bindX() const { return m_bindX; }
    float bindY() const { return m_bindY; }
    float bindZ() const { return m_bindZ; }
    float bindHeading() const { return m_bindHeading; }

    void setBindPoint(uint32_t zoneId, float x, float y, float z, float heading);

    // ========== Flags ==========

    bool isSneaking() const { return m_isSneaking; }
    void setSneaking(bool val) { m_isSneaking = val; }

    bool isAFK() const { return m_isAFK; }
    void setAFK(bool val) { m_isAFK = val; }

    bool isAnonymous() const { return m_isAnonymous; }
    void setAnonymous(bool val) { m_isAnonymous = val; }

    bool isRoleplay() const { return m_isRoleplay; }
    void setRoleplay(bool val) { m_isRoleplay = val; }

    // ========== Camp Timer ==========

    bool isCamping() const { return m_isCamping; }
    void setCamping(bool val) { m_isCamping = val; }
    std::chrono::steady_clock::time_point campStartTime() const { return m_campStartTime; }
    void setCampStartTime(std::chrono::steady_clock::time_point time) { m_campStartTime = time; }

    // ========== Bulk State Setting ==========

    // Used when receiving PlayerProfile packet
    struct ProfileData {
        std::string name;
        std::string lastName;
        uint8_t level;
        uint32_t classId;
        uint32_t race;
        uint32_t gender;
        uint32_t deity;
        uint32_t curHP, maxHP;
        uint32_t mana, maxMana;
        uint32_t endurance, maxEndurance;
        uint32_t STR, STA, CHA, DEX, INT, AGI, WIS;
        uint32_t platinum, gold, silver, copper;
        float x, y, z, heading;
    };

    void loadFromProfile(const ProfileData& profile);

private:
    void firePlayerMovedEvent();
    void firePlayerStatsChangedEvent();

    EventBus* m_eventBus = nullptr;

    // Position and movement
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
    float m_heading = 0.0f;
    float m_size = 6.0f;
    float m_dx = 0.0f;
    float m_dy = 0.0f;
    float m_dz = 0.0f;
    int16_t m_animation = 0;
    bool m_isMoving = false;
    float m_moveSpeed = 48.5f;
    uint32_t m_movementSequence = 0;
    MovementMode m_movementMode = MovementMode::Run;
    PositionState m_positionState = PositionState::Standing;

    // Movement target
    float m_targetX = 0.0f;
    float m_targetY = 0.0f;
    float m_targetZ = 0.0f;
    bool m_hasMovementTarget = false;

    // Follow target
    std::string m_followTarget;
    float m_followDistance = 10.0f;

    // Keyboard state
    bool m_moveForward = false;
    bool m_moveBackward = false;
    bool m_turnLeft = false;
    bool m_turnRight = false;

    // Jump state
    bool m_isJumping = false;
    float m_jumpStartZ = 0.0f;
    std::chrono::steady_clock::time_point m_jumpStartTime;

    // Character identity
    uint16_t m_spawnId = 0;
    uint16_t m_characterId = 0;
    std::string m_name;
    std::string m_lastName;

    // Character stats
    uint8_t m_level = 1;
    uint32_t m_class = 0;
    uint32_t m_race = 0;
    uint32_t m_gender = 0;
    uint32_t m_deity = 0;

    // Health, Mana, Endurance
    uint32_t m_curHP = 0;
    uint32_t m_maxHP = 0;
    uint32_t m_mana = 0;
    uint32_t m_maxMana = 0;
    uint32_t m_endurance = 0;
    uint32_t m_maxEndurance = 0;

    // Base attributes
    uint32_t m_STR = 0;
    uint32_t m_STA = 0;
    uint32_t m_CHA = 0;
    uint32_t m_DEX = 0;
    uint32_t m_INT = 0;
    uint32_t m_AGI = 0;
    uint32_t m_WIS = 0;

    // Currency
    uint32_t m_platinum = 0;
    uint32_t m_gold = 0;
    uint32_t m_silver = 0;
    uint32_t m_copper = 0;

    // Weight
    float m_weight = 0.0f;
    float m_maxWeight = 0.0f;

    // Bind point
    uint32_t m_bindZoneId = 0;
    float m_bindX = 0.0f;
    float m_bindY = 0.0f;
    float m_bindZ = 0.0f;
    float m_bindHeading = 0.0f;

    // Flags
    bool m_isSneaking = false;
    bool m_isAFK = false;
    bool m_isAnonymous = false;
    bool m_isRoleplay = false;

    // Camp timer
    bool m_isCamping = false;
    std::chrono::steady_clock::time_point m_campStartTime;
};

} // namespace state
} // namespace eqt
