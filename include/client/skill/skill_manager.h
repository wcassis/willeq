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

// Forward declarations
class EverQuest;
namespace eqt { namespace bridge { class GameStateBridge; } }

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

    // Load skill caps for a specific class from config/skill_caps.json
    // (SkillCapTable). Sets each skill's class_can_have possession flag and
    // its class lifetime max_value. Falls back to permissive (every skill
    // allowed) if the data file is missing or unparseable. T159.
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

    // Check if player has a specific skill -- POSSESSION, i.e. "can my class
    // ever have this skill" (T159). NOT the display test; the skills window
    // wants SkillData::isDisplayableSkill(), which getAllSkills() uses.
    bool hasSkill(uint8_t skill_id) const;

    // Check if a skill can currently be used
    bool canUseSkill(uint8_t skill_id) const;

    // Get error message explaining why skill can't be used
    std::string getSkillUseError(uint8_t skill_id) const;

    // Check if skill is on cooldown
    bool isSkillOnCooldown(uint8_t skill_id) const;

    // Get remaining cooldown in milliseconds
    uint32_t getSkillCooldownRemaining(uint8_t skill_id) const;

    // Get a specific skill's data
    const SkillData* getSkill(uint8_t skill_id) const;
    SkillData* getSkill(uint8_t skill_id);

    // ========================================================================
    // Skill Lists for UI
    // ========================================================================

    // B502 (GitLab #177) DELETED isSkillReady() (redundant with
    // getSkillCooldownRemaining() == 0), getSkillsByCategory() and
    // getActivatableSkills(): the Skills popup does not go through them -- it
    // is filled from getAllSkills() by EverQuest's SkillsSnapshot publish
    // (core/everquest.cpp), and the trainer panel reads SkillCapTable
    // directly. /useskill routes through activateSkill().

    // Get all skills the player has
    std::vector<const SkillData*> getAllSkills() const;

    // Get total number of skills
    size_t getSkillCount() const;

    // ========================================================================
    // Skill Activation
    // ========================================================================

    // Activate a skill (sends packet to server)
    bool activateSkill(uint8_t skill_id, uint16_t target_id = 0);

    // ========================================================================
    // Callbacks
    // ========================================================================

    using SkillUpdateCallback = std::function<void(uint8_t skill_id, uint32_t old_value, uint32_t new_value)>;
    using SkillActivatedCallback = std::function<void(uint8_t skill_id, bool success, const std::string& message)>;

    // Called when a skill value changes
    void setOnSkillUpdate(SkillUpdateCallback callback);

    // Called when a skill is activated
    void setOnSkillActivated(SkillActivatedCallback callback);

    // D20e1: Bridge for visual events (set by EverQuest::InitGraphics)
    void setBridge(eqt::bridge::GameStateBridge* bridge) { m_bridge = bridge; }

private:
    EverQuest* m_eq;
    eqt::bridge::GameStateBridge* m_bridge = nullptr;  // D20e1

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

    // T152: handle the five dedicated skill opcodes that eqemu rejects (or
    // mis-reads) with an empty body. Returns true if the skill was handled
    // here; false means the zero-length send is the correct wire form.
    bool sendBodiedSkillPacket(uint8_t skill_id, uint16_t target_id);

    // Locate a poison item for OP_ApplyPoison. Returns -1 if none is carried.
    int16_t findPoisonInventorySlot() const;

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

    // T353: checkResourceRequirement() and the SkillData::endurance_cost /
    // mana_cost it read are DELETED. Skill activation has no resource cost in
    // this era on either side of the wire: no handler in
    // /mnt/projects/akk-stack/code/zone/client_packet.cpp for any opcode fable
    // sends for a skill reads or spends endurance or mana, and there is no
    // client-side skill-cost table to load one from. See the enforcement table
    // in refactor/T353_skill_costs.md before adding one back.
};

} // namespace EQ
