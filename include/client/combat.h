#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <map>
#include <glm/glm.hpp>

// Forward declarations
class EverQuest;
struct Entity;

// Combat-related enums
enum CombatState {
	COMBAT_STATE_IDLE,
	COMBAT_STATE_ENGAGED,
	COMBAT_STATE_FLEEING,
	COMBAT_STATE_LOOTING,
	COMBAT_STATE_HUNTING,
	COMBAT_STATE_RESTING,
	COMBAT_STATE_SEEKING_GUARD
};

enum TargetPriority {
	TARGET_PRIORITY_LOWEST = 0,
	TARGET_PRIORITY_LOW,
	TARGET_PRIORITY_MEDIUM,
	TARGET_PRIORITY_HIGH,
	TARGET_PRIORITY_HIGHEST
};

// Consider colors (faction + level based)
enum HCConsiderColor {
	CON_GREEN = 2,
	CON_LIGHTBLUE = 18,
	CON_BLUE = 4,
	CON_WHITE = 20,
	CON_YELLOW = 15,
	CON_RED = 13,
	CON_GRAY = 6
};

// Spell gem slots
enum SpellSlot {
	SPELL_GEM_1 = 0,
	SPELL_GEM_2,
	SPELL_GEM_3,
	SPELL_GEM_4,
	SPELL_GEM_5,
	SPELL_GEM_6,
	SPELL_GEM_7,
	SPELL_GEM_8,
	SPELL_GEM_9,
	SPELL_GEM_10,
	SPELL_GEM_11,
	SPELL_GEM_12,
	MAX_SPELL_GEMS = 12
};

// Combat actions
enum CombatAction {
	COMBAT_ACTION_ATTACK = 0,
	COMBAT_ACTION_CAST,
	COMBAT_ACTION_HEAL,
	COMBAT_ACTION_BUFF,
	COMBAT_ACTION_FLEE
};

struct CombatTarget {
	uint16_t entity_id;
	std::string name;
	float distance;
	uint8_t hp_percent;
	HCConsiderColor con_color;
	TargetPriority priority;
	bool is_aggro;
	std::chrono::steady_clock::time_point last_considered;
	bool has_consider_data;
	uint32_t faction;
	uint32_t con_level;
	int32_t cur_hp;
	int32_t max_hp;
};

struct SpellInfo {
	uint32_t spell_id;
	std::string name;
	uint32_t mana_cost;
	uint32_t cast_time_ms;
	uint32_t recast_time_ms;
	uint32_t range;
	bool is_beneficial;
	bool is_detrimental;
	SpellSlot gem_slot;
	std::chrono::steady_clock::time_point last_cast_time;
};

struct CombatStats {
	uint32_t current_hp;
	uint32_t max_hp;
	uint32_t current_mana;
	uint32_t max_mana;
	uint32_t current_endurance;
	uint32_t max_endurance;
	float hp_percent;
	float mana_percent;
	float endurance_percent;
};

class CombatManager {
public:
	CombatManager(EverQuest* eq);
	~CombatManager();

	// Enable/disable combat manager
	void Enable();
	void Disable();
	bool IsEnabled() const { return m_enabled; }

	// Core combat functions
	void Update();
	void SetCombatState(CombatState state);
	CombatState GetCombatState() const { return m_combat_state; }

	// Targeting
	bool SetTarget(uint16_t entity_id);
	bool SetTarget(const std::string& name);
	void ClearTarget();
	uint16_t GetTargetId() const { return m_current_target_id; }
	bool HasTarget() const { return m_current_target_id != 0; }
	void ConsiderTarget();

	// Auto attack
	void EnableAutoAttack();
	void DisableAutoAttack();
	bool IsAutoAttackEnabled() const { return m_auto_attack_enabled; }

	// Auto fire (ranged)
	void EnableAutoFire();
	void DisableAutoFire();
	void ToggleAutoFire();
	bool IsAutoFireEnabled() const { return m_auto_fire_enabled; }

	// Auto movement during combat
	void EnableAutoMovement() { m_auto_movement_enabled = true; }
	void DisableAutoMovement() { m_auto_movement_enabled = false; }
	bool IsAutoMovementEnabled() const { return m_auto_movement_enabled; }

	// Spell casting
	bool CastSpell(uint32_t spell_id, uint16_t target_id = 0);
	bool CastSpell(SpellSlot slot, uint16_t target_id = 0);
	void InterruptCast();
	bool IsCasting() const { return m_is_casting; }
	void MemorizeSpell(uint32_t spell_id, SpellSlot slot);

	// Combat abilities
	void UseAbility(uint32_t ability_id, uint16_t target_id = 0);
	void Taunt(uint16_t target_id = 0);

	// Situational awareness
	void UpdateCombatStats(const CombatStats& stats);
	bool ShouldFlee() const;
	std::vector<CombatTarget> GetNearbyHostiles(float range = 50.0f);
	std::vector<CombatTarget> GetNearbyAllies(float range = 50.0f);
	CombatTarget* GetHighestPriorityTarget();

	// Fleeing
	void InitiateFlee();
	bool IsFleeing() const { return m_combat_state == COMBAT_STATE_FLEEING; }
	void SetFleeThreshold(float hp_percent) { m_flee_hp_threshold = hp_percent; }

	// Looting
	void LootCorpse(uint16_t corpse_id);
	void LootItem(uint32_t item_slot);
	void LootAll();
	void CloseLootWindow();
	bool IsLooting() const { return m_combat_state == COMBAT_STATE_LOOTING; }

	// Configuration
	void SetAssistRange(float range) { m_assist_range = range; }
	void SetAggroRadius(float radius) { m_aggro_radius = radius; }
	void EnableAutoLoot(bool enable) { m_auto_loot_enabled = enable; }
	void SetCombatRange(float range) { m_combat_range = range; }

	// Auto-hunting
	void SetAutoHunting(bool enable);
	bool IsAutoHunting() const { return m_auto_hunting_enabled; }
	void UpdateAutoHunting();

	// Spell management
	void LoadSpellSet(const std::string& spell_set_name);
	void SaveSpellSet(const std::string& spell_set_name);
	std::vector<SpellInfo> GetMemorizedSpells() const;

	// Consider result processing
	void ProcessConsiderResponse(uint32_t target_id, uint32_t faction, uint32_t level,
		int32_t cur_hp, int32_t max_hp);

	// Hunt target listing
	void ListHuntTargets();

	// Loot window handling
	void ProcessLootWindow(const std::vector<uint32_t>& item_ids);
	void AddLootItem(uint32_t slot_id);
	void CheckAutoLoot();
	void SetAutoLootEnabled(bool enabled) { m_auto_loot_enabled = enabled; }
	bool IsAutoLootEnabled() const { return m_auto_loot_enabled; }

private:
	EverQuest* m_eq;
	bool m_enabled;
	CombatState m_combat_state;

	// Targeting
	uint16_t m_current_target_id;
	std::unique_ptr<CombatTarget> m_current_target_info;
	std::vector<CombatTarget> m_potential_targets;
	std::chrono::steady_clock::time_point m_last_target_scan;

	// Auto attack
	bool m_auto_attack_enabled;
	bool m_auto_fire_enabled;  // Ranged auto-attack
	std::chrono::steady_clock::time_point m_last_attack_time;
	uint32_t m_attack_delay_ms;

	// Spell casting
	bool m_is_casting;
	uint32_t m_current_spell_id;
	uint16_t m_spell_target_id;
	std::chrono::steady_clock::time_point m_cast_start_time;
	uint32_t m_cast_time_ms;

	// Spells
	std::vector<SpellInfo> m_memorized_spells;
	std::map<SpellSlot, uint32_t> m_spell_gems;

	// Combat stats
	CombatStats m_stats;
	float m_flee_hp_threshold;
	std::chrono::steady_clock::time_point m_flee_start_time;
	glm::vec3 m_flee_destination;

	// Looting
	uint16_t m_current_corpse_id;
	std::vector<uint32_t> m_loot_items;
	bool m_auto_loot_enabled;

	// Auto movement during combat
	bool m_auto_movement_enabled;

	// Auto-hunting
	bool m_auto_hunting_enabled;
	std::chrono::steady_clock::time_point m_last_hunt_update;
	std::chrono::steady_clock::time_point m_last_rest_check;
	std::chrono::steady_clock::time_point m_combat_end_time;
	bool m_is_resting;
	float m_hunt_radius;
	float m_rest_hp_threshold;
	float m_rest_mana_threshold;
	bool m_waiting_for_considers;
	std::vector<uint16_t> m_pending_considers;
	std::chrono::steady_clock::time_point m_consider_start_time;

	// Configuration
	float m_assist_range;
	float m_aggro_radius;
	float m_combat_range;
	float m_spell_range;

	// Helper methods
	void ScanForTargets();
	void UpdateTargetPriorities();
	float CalculateTargetPriority(const Entity& entity);
	HCConsiderColor GetConsiderColor(int level_diff);
	bool IsHostile(const Entity& entity) const;
	bool IsAlly(const Entity& entity) const;
	void SelectBestFleePath();
	bool CanCastSpell(const SpellInfo& spell);
	void ProcessCombatRound();
	void CheckForAggro();
	void SendAttackPacket(uint16_t target_id);
	float CalculateCombatRange(float my_size, float target_size, uint16_t target_race) const;

	// Auto-hunting helpers
	void FindNextHuntTarget();
	bool ShouldRest() const;
	void StartResting();
	void StopResting();
	bool IsTargetSuitableForHunt(const Entity& entity) const;
	Entity* FindNearestGuard() const;
	void FleeToGuard();
	bool IsGuard(const Entity& entity) const;
	bool IsCorpse(const Entity& entity) const;
	Entity* FindNearbyCorpse() const;
	bool IsHumanoid(uint16_t race_id) const;
};
