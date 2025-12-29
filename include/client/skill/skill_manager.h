/*
 * WillEQ Skill Manager
 *
 * Manages player skills, including values, availability, activation, and cooldowns.
 */

#pragma once

#include <array>
#include <vector>
#include <functional>
#include <cstdint>
#include "skill_data.h"
#include "skill_constants.h"
#include "common/packet_structs.h"  // For MAX_PP_SKILL

// Forward declaration
class EverQuest;

namespace EQ {

// ============================================================================
// Skill Manager Class
// ============================================================================

class SkillManager {
public:
    explicit SkillManager(EverQuest* eq);
    ~SkillManager();

    // ========================================================================
    // Initialization
    // ========================================================================

    // Initialize skills for a character
    void initialize(uint8_t player_class, uint8_t player_race, uint8_t player_level);

    // Load skill caps for a specific class
    void loadClassSkillCaps(uint8_t player_class, uint8_t player_level);

    // Update player level (recalculates skill caps)
    void setPlayerLevel(uint8_t level);

    // ========================================================================
    // Skill Value Management
    // ========================================================================

    // Update a single skill value (from server packet)
    void updateSkill(uint8_t skill_id, uint32_t value);

    // Update all skills from player profile data
    void updateAllSkills(const uint32_t* skills, size_t count);

    // Get a skill's current value
    uint32_t getSkillValue(uint8_t skill_id) const;

    // Get a skill's maximum value at current level
    uint32_t getSkillMax(uint8_t skill_id) const;

    // ========================================================================
    // Skill Queries
    // ========================================================================

    // Check if player has a specific skill
    bool hasSkill(uint8_t skill_id) const;

    // Check if a skill can currently be used
    bool canUseSkill(uint8_t skill_id) const;

    // Get error message explaining why skill can't be used
    std::string getSkillUseError(uint8_t skill_id) const;

    // Check if skill is on cooldown
    bool isSkillOnCooldown(uint8_t skill_id) const;

    // Get remaining cooldown in milliseconds
    uint32_t getSkillCooldownRemaining(uint8_t skill_id) const;

    // Check if skill is ready to use (not on cooldown)
    bool isSkillReady(uint8_t skill_id) const;

    // Get a specific skill's data
    const SkillData* getSkill(uint8_t skill_id) const;
    SkillData* getSkill(uint8_t skill_id);

    // ========================================================================
    // Skill Lists for UI
    // ========================================================================

    // Get all skills the player has
    std::vector<const SkillData*> getAllSkills() const;

    // Get skills filtered by category
    std::vector<const SkillData*> getSkillsByCategory(SkillCategory category) const;

    // Get only skills that can be manually activated
    std::vector<const SkillData*> getActivatableSkills() const;

    // Get total number of skills
    size_t getSkillCount() const;

    // ========================================================================
    // Skill Activation
    // ========================================================================

    // Activate a skill (sends packet to server)
    bool activateSkill(uint8_t skill_id, uint16_t target_id = 0);

    // Toggle a toggle skill (Hide, Sneak, etc.)
    bool toggleSkill(uint8_t skill_id);

    // Check if a toggle skill is currently active
    bool isSkillActive(uint8_t skill_id) const;

    // ========================================================================
    // Callbacks
    // ========================================================================

    using SkillUpdateCallback = std::function<void(uint8_t skill_id, uint32_t old_value, uint32_t new_value)>;
    using SkillActivatedCallback = std::function<void(uint8_t skill_id, bool success, const std::string& message)>;

    // Called when a skill value changes
    void setOnSkillUpdate(SkillUpdateCallback callback);

    // Called when a skill is activated
    void setOnSkillActivated(SkillActivatedCallback callback);

private:
    EverQuest* m_eq;

    // All skills indexed by skill_id
    std::array<SkillData, EQT::MAX_PP_SKILL> m_skills;

    // Player info for skill availability
    uint8_t m_player_class = 0;
    uint8_t m_player_race = 0;
    uint8_t m_player_level = 1;

    // Callbacks
    SkillUpdateCallback m_on_skill_update;
    SkillActivatedCallback m_on_skill_activated;

    // ========================================================================
    // Private Helpers
    // ========================================================================

    // Send skill use packet to server
    void sendSkillUsePacket(uint8_t skill_id, uint16_t target_id);

    // Get the opcode for a specific skill
    uint16_t getSkillOpcode(uint8_t skill_id) const;

    // Play the appropriate animation for a skill
    void playSkillAnimation(uint8_t skill_id);

    // Check combat state requirements
    bool checkCombatRequirement(uint8_t skill_id) const;

    // Check target requirements
    bool checkTargetRequirement(uint8_t skill_id, uint16_t target_id) const;

    // Check position requirements (behind target, etc.)
    bool checkPositionRequirement(uint8_t skill_id, uint16_t target_id) const;

    // Check resource costs (endurance, mana)
    bool checkResourceRequirement(uint8_t skill_id) const;
};

} // namespace EQ
