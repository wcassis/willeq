#include "client/combat.h"
#include "client/eq.h"
#include "common/logging.h"
#include "common/net/daybreak_connection.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <cctype>
#include <fmt/format.h>
#include <limits>

// Packet structures based on Titanium client
#pragma pack(1)
struct ClientTarget_Struct {
	uint32_t new_target;
};

struct Consider_Struct {
	uint32_t playerid;
	uint32_t targetid;
	uint32_t faction;
	uint32_t level;
	int32_t  cur_hp;
	int32_t  max_hp;
	uint8_t  pvpcon;
	uint8_t  unknown3[3];
};

struct CastSpell_Struct {
	uint32_t slot;
	uint32_t spell_id;
	uint32_t inventoryslot;
	uint32_t target_id;
	uint8_t  cs_unknown[4];
};

struct LootingItem_Struct {
	uint32_t lootee;
	uint32_t looter;
	uint16_t slot_id;
	uint8_t  unknown3[2];
	uint32_t auto_loot;
};

struct Action_Struct {
	uint16_t target;           // id of target
	uint16_t source;           // id of caster/attacker
	uint16_t level;            // level of caster for spells, attack rating for melee
	uint32_t instrument_mod;   // base damage for melee, instrument mod for bard songs
	float    force;
	float    hit_heading;
	float    hit_pitch;
	uint8_t  type;             // 231 (0xE7) for spells, skill type for melee
	uint16_t unknown23;        // min_damage
	uint16_t unknown25;        // tohit
	uint16_t spell;            // spell id being cast
	uint8_t  spell_level;
	uint8_t  effect_flag;      // if this is 4, a buff icon is made
};
#pragma pack()

CombatManager::CombatManager(EverQuest* eq)
	: m_eq(eq)
	, m_enabled(false)  // Disabled by default, use /combat to enable
	, m_combat_state(COMBAT_STATE_IDLE)
	, m_current_target_id(0)
	, m_auto_attack_enabled(false)
	, m_auto_fire_enabled(false)
	, m_is_casting(false)
	, m_current_spell_id(0)
	, m_spell_target_id(0)
	, m_cast_time_ms(0)
	, m_flee_hp_threshold(20.0f)
	, m_current_corpse_id(0)
	, m_auto_loot_enabled(true)
	, m_auto_movement_enabled(false)  // Default to false - only enabled by "attack" command
	, m_auto_hunting_enabled(false)
	, m_is_resting(false)
	, m_hunt_radius(300.0f)
	, m_rest_hp_threshold(50.0f)
	, m_rest_mana_threshold(30.0f)
	, m_waiting_for_considers(false)
	, m_assist_range(50.0f)
	, m_aggro_radius(30.0f)
	, m_combat_range(5.0f)  // Standard melee range
	, m_spell_range(200.0f)
	, m_attack_delay_ms(2000) // Default 2 second attack delay
{
	// Initialize spell gems as empty
	for (int i = 0; i < MAX_SPELL_GEMS; ++i) {
		m_spell_gems[static_cast<SpellSlot>(i)] = 0;
	}

	// Initialize memorized spells vector
	m_memorized_spells.resize(MAX_SPELL_GEMS);

	// Initialize stats to avoid immediate resting - will be updated by server
	m_stats.hp_percent = 100.0f;
	m_stats.mana_percent = 100.0f;
	m_stats.endurance_percent = 100.0f;

	// Initialize time points
	m_last_target_scan = std::chrono::steady_clock::now();
	m_last_attack_time = std::chrono::steady_clock::now();
	m_cast_start_time = std::chrono::steady_clock::now();
	m_flee_start_time = std::chrono::steady_clock::now();
	m_last_hunt_update = std::chrono::steady_clock::now();
	m_last_rest_check = std::chrono::steady_clock::now();
	m_combat_end_time = std::chrono::steady_clock::now();
}

CombatManager::~CombatManager() {
}

void CombatManager::Enable() {
	if (!m_enabled) {
		m_enabled = true;
		LOG_INFO(MOD_COMBAT, "Combat manager enabled");
	}
}

void CombatManager::Disable() {
	if (m_enabled) {
		m_enabled = false;
		// Clean up any active combat state
		DisableAutoAttack();
		DisableAutoMovement();
		SetAutoHunting(false);
		SetCombatState(COMBAT_STATE_IDLE);
		LOG_INFO(MOD_COMBAT, "Combat manager disabled");
	}
}

void CombatManager::Update() {
	// Don't update if we're not initialized or not enabled
	if (!m_eq || !m_enabled) {
		return;
	}

	auto now = std::chrono::steady_clock::now();

	// Update our situational awareness
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_target_scan).count() >= 1000) {
		ScanForTargets();
		m_last_target_scan = now;
	}

	// Update auto-hunting if enabled
	if (m_auto_hunting_enabled) {
		UpdateAutoHunting();
	}

	// Handle combat state
	switch (m_combat_state) {
		case COMBAT_STATE_IDLE:
			// Check for aggro
			CheckForAggro();
			break;

		case COMBAT_STATE_ENGAGED:
			// Process combat rounds
			ProcessCombatRound();

			// Check if we should flee
			if (ShouldFlee()) {
				InitiateFlee();
			}
			break;

		case COMBAT_STATE_FLEEING:
			// Update flee movement - handled by EverQuest movement system
			// Check if we're safe to stop fleeing
			if (m_stats.hp_percent > m_flee_hp_threshold + 10.0f ||
				std::chrono::duration_cast<std::chrono::seconds>(now - m_flee_start_time).count() > 30) {
				SetCombatState(COMBAT_STATE_IDLE);
			}
			break;

		case COMBAT_STATE_LOOTING:
			// Looting is handled via packets, just wait
			break;

		case COMBAT_STATE_HUNTING:
			// Hunting state is managed by UpdateAutoHunting
			break;

		case COMBAT_STATE_RESTING:
			// Check if we're ready to resume
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_rest_check).count() >= 2000) {
				if (m_stats.hp_percent >= 90.0f && m_stats.mana_percent >= 80.0f) {
					StopResting();
					SetCombatState(COMBAT_STATE_HUNTING);
				}
				m_last_rest_check = now;
			}
			break;

		case COMBAT_STATE_SEEKING_GUARD:
			// Handled by movement system
			break;
	}
}

void CombatManager::SetCombatState(CombatState state) {
	if (m_combat_state != state) {
		m_combat_state = state;

		LOG_DEBUG(MOD_COMBAT, "Combat state changed to: {}", static_cast<int>(state));

		// Handle state transitions
		if (state == COMBAT_STATE_IDLE) {
			// Record when combat ended
			m_combat_end_time = std::chrono::steady_clock::now();
		}
	}
}

bool CombatManager::SetTarget(uint16_t entity_id) {
	// Find the entity
	const auto& entities = m_eq->GetEntities();
	for (const auto& [id, entity] : entities) {
		if (id == entity_id) {
			m_current_target_id = entity_id;

			// Create target info
			m_current_target_info = std::make_unique<CombatTarget>();
			m_current_target_info->entity_id = entity_id;
			m_current_target_info->name = entity.name;
			m_current_target_info->hp_percent = entity.hp_percent;

			// Send target packet
			EQ::Net::DynamicPacket packet;
			packet.Resize(sizeof(ClientTarget_Struct));
			ClientTarget_Struct* ct = (ClientTarget_Struct*)packet.Data();
			ct->new_target = entity_id;

			m_eq->QueuePacket(HC_OP_TargetMouse, &packet);

			LOG_DEBUG(MOD_COMBAT, "Target set to {} (ID: {})", entity.name, entity_id);

			return true;
		}
	}

	return false;
}

bool CombatManager::SetTarget(const std::string& name) {
	// Find entity by name
	const auto& entities = m_eq->GetEntities();
	for (const auto& [id, entity] : entities) {
		if (entity.name == name) {
			return SetTarget(id);
		}
	}

	// Try partial match
	for (const auto& [id, entity] : entities) {
		if (entity.name.find(name) != std::string::npos) {
			return SetTarget(id);
		}
	}

	return false;
}

void CombatManager::ClearTarget() {
	m_current_target_id = 0;
	m_current_target_info.reset();

	// Send clear target packet
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(ClientTarget_Struct));
	ClientTarget_Struct* ct = (ClientTarget_Struct*)packet.Data();
	ct->new_target = 0;

	m_eq->QueuePacket(HC_OP_TargetMouse, &packet);
}

void CombatManager::ConsiderTarget() {
	if (!HasTarget()) {
		return;
	}

	// Send consider packet
	EQ::Net::DynamicPacket packet;
	packet.Resize(28);
	packet.PutUInt32(0, m_eq->GetEntityID());  // playerid
	packet.PutUInt32(4, m_current_target_id);  // targetid
	packet.PutUInt32(8, 0);  // faction (will be filled by server)
	packet.PutUInt32(12, 0); // level (will be filled by server)
	packet.PutInt32(16, 0);  // cur_hp (will be filled by server)
	packet.PutInt32(20, 0);  // max_hp (will be filled by server)
	packet.PutUInt8(24, 0);  // pvpcon
	packet.PutUInt8(25, 0);  // unknown
	packet.PutUInt8(26, 0);  // unknown
	packet.PutUInt8(27, 0);  // unknown

	m_eq->QueuePacket(HC_OP_Consider, &packet);

	LOG_DEBUG(MOD_COMBAT, "Sent consider request for target ID: {}", m_current_target_id);
}

void CombatManager::EnableAutoAttack() {
	LOG_DEBUG(MOD_COMBAT, "EnableAutoAttack called (has_target={}, target_id={})",
		HasTarget(), m_current_target_id);

	if (!HasTarget()) {
		LOG_DEBUG(MOD_COMBAT, "EnableAutoAttack: No target, returning");
		return;
	}

	m_auto_attack_enabled = true;
	SetCombatState(COMBAT_STATE_ENGAGED);

	LOG_DEBUG(MOD_COMBAT, "EnableAutoAttack: Sending auto attack packet");

	// Send auto attack on packet (4 bytes expected by server)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt8(0, 1);  // 1 = on, 0 = off
	packet.PutUInt8(1, 0);  // padding
	packet.PutUInt8(2, 0);  // padding
	packet.PutUInt8(3, 0);  // padding
	m_eq->QueuePacket(HC_OP_AutoAttack, &packet);

	// Send AutoAttack2 immediately after AutoAttack
	EQ::Net::DynamicPacket packet2;
	packet2.Resize(4);
	packet2.PutUInt32(0, 0);  // Unknown content, server doesn't process it
	m_eq->QueuePacket(HC_OP_AutoAttack2, &packet2);

	LOG_DEBUG(MOD_COMBAT, "EnableAutoAttack: Auto attack ENABLED (sent both AutoAttack and AutoAttack2)");
}

void CombatManager::DisableAutoAttack() {
	m_auto_attack_enabled = false;

	// Only change state if we're engaged
	if (m_combat_state == COMBAT_STATE_ENGAGED) {
		SetCombatState(COMBAT_STATE_IDLE);
	}

	// Send auto attack off packet (4 bytes expected by server)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt8(0, 0);  // 1 = on, 0 = off
	packet.PutUInt8(1, 0);  // padding
	packet.PutUInt8(2, 0);  // padding
	packet.PutUInt8(3, 0);  // padding
	m_eq->QueuePacket(HC_OP_AutoAttack, &packet);

	// Send AutoAttack2 immediately after AutoAttack
	EQ::Net::DynamicPacket packet2;
	packet2.Resize(4);
	packet2.PutUInt32(0, 0);  // Unknown content, server doesn't process it
	m_eq->QueuePacket(HC_OP_AutoAttack2, &packet2);
}

void CombatManager::EnableAutoFire() {
	LOG_DEBUG(MOD_COMBAT, "EnableAutoFire called (has_target={}, target_id={})",
		HasTarget(), m_current_target_id);

	if (!HasTarget()) {
		LOG_DEBUG(MOD_COMBAT, "EnableAutoFire: No target, returning");
		return;
	}

	// Disable melee auto-attack when enabling ranged
	if (m_auto_attack_enabled) {
		DisableAutoAttack();
	}

	m_auto_fire_enabled = true;
	SetCombatState(COMBAT_STATE_ENGAGED);

	// Send auto fire on packet (1 byte bool)
	EQ::Net::DynamicPacket packet;
	packet.Resize(1);
	packet.PutUInt8(0, 1);  // true = on
	m_eq->QueuePacket(HC_OP_AutoFire, &packet);

	LOG_DEBUG(MOD_COMBAT, "EnableAutoFire: Auto fire ENABLED");
}

void CombatManager::DisableAutoFire() {
	m_auto_fire_enabled = false;

	// Only change state if we're engaged and not melee attacking
	if (m_combat_state == COMBAT_STATE_ENGAGED && !m_auto_attack_enabled) {
		SetCombatState(COMBAT_STATE_IDLE);
	}

	// Send auto fire off packet (1 byte bool)
	EQ::Net::DynamicPacket packet;
	packet.Resize(1);
	packet.PutUInt8(0, 0);  // false = off
	m_eq->QueuePacket(HC_OP_AutoFire, &packet);

	LOG_DEBUG(MOD_COMBAT, "DisableAutoFire: Auto fire DISABLED");
}

void CombatManager::ToggleAutoFire() {
	if (m_auto_fire_enabled) {
		DisableAutoFire();
	} else {
		EnableAutoFire();
	}
}

bool CombatManager::CastSpell(uint32_t spell_id, uint16_t target_id) {
	if (m_is_casting) {
		return false;
	}

	// TODO: Verify we know this spell and have it memorized

	m_is_casting = true;
	m_current_spell_id = spell_id;
	m_spell_target_id = target_id ? target_id : m_current_target_id;
	m_cast_start_time = std::chrono::steady_clock::now();
	m_cast_time_ms = 3000; // TODO: Get actual cast time from spell data

	// Send cast spell packet
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(CastSpell_Struct));
	CastSpell_Struct* cs = (CastSpell_Struct*)packet.Data();
	cs->slot = 0; // TODO: Find correct slot
	cs->spell_id = spell_id;
	cs->inventoryslot = 0xFFFFFFFF;
	cs->target_id = m_spell_target_id;

	m_eq->QueuePacket(HC_OP_CastSpell, &packet);

	return true;
}

bool CombatManager::CastSpell(SpellSlot slot, uint16_t target_id) {
	auto it = m_spell_gems.find(slot);
	if (it == m_spell_gems.end() || it->second == 0) {
		return false;
	}

	return CastSpell(it->second, target_id);
}

void CombatManager::InterruptCast() {
	if (!m_is_casting) {
		return;
	}

	m_is_casting = false;
	m_current_spell_id = 0;
	m_spell_target_id = 0;

	// Send interrupt packet
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt16(0, m_eq->GetEntityID());
	packet.PutUInt16(2, 0x01); // Interrupt type
	m_eq->QueuePacket(HC_OP_InterruptCast, &packet);
}

void CombatManager::MemorizeSpell(uint32_t spell_id, SpellSlot slot) {
	m_spell_gems[slot] = spell_id;

	// TODO: Send memorize spell packet
}

void CombatManager::UseAbility(uint32_t ability_id, uint16_t target_id) {
	if (!m_eq) {
		LOG_ERROR(MOD_COMBAT, "UseAbility: No EQ reference");
		return;
	}

	// Use current target if none specified
	uint16_t actual_target = target_id;
	if (actual_target == 0) {
		actual_target = m_current_target_id;
	}

	LOG_DEBUG(MOD_COMBAT, "UseAbility: skill={}, target={}", ability_id, actual_target);

	// Build CombatAbility packet
	// Structure: target (4 bytes), attack_value (4 bytes), skill_id (4 bytes)
	// The attack_value is typically 100 based on packet captures from real client
	EQ::Net::DynamicPacket packet;
	packet.Resize(12);  // CombatAbility_Struct is 12 bytes
	packet.PutUInt32(0, actual_target);  // target entity ID
	packet.PutUInt32(4, 100);            // attack value (real client sends 100)
	packet.PutUInt32(8, ability_id);     // skill ID

	m_eq->QueuePacket(HC_OP_CombatAbility, &packet);

	LOG_DEBUG(MOD_COMBAT, "Sent CombatAbility packet: skill={}, target={}", ability_id, actual_target);
}

void CombatManager::Taunt(uint16_t target_id) {
	if (!m_eq) {
		LOG_ERROR(MOD_COMBAT, "Taunt: No EQ reference");
		return;
	}

	// Use current target if none specified
	uint16_t actual_target = target_id;
	if (actual_target == 0) {
		actual_target = m_current_target_id;
	}

	if (actual_target == 0) {
		LOG_DEBUG(MOD_COMBAT, "Taunt: No target specified");
		return;
	}

	LOG_DEBUG(MOD_COMBAT, "Taunt: target={}", actual_target);

	// Build Taunt packet using Target_Struct (4 bytes)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt32(0, actual_target);

	m_eq->QueuePacket(HC_OP_Taunt, &packet);

	LOG_DEBUG(MOD_COMBAT, "Sent Taunt packet: target={}", actual_target);
}

void CombatManager::UpdateCombatStats(const CombatStats& stats) {
	m_stats = stats;

	LOG_TRACE(MOD_COMBAT, "Combat stats updated: HP={}%, Mana={}%, End={}%",
		m_stats.hp_percent, m_stats.mana_percent, m_stats.endurance_percent);
}

bool CombatManager::ShouldFlee() const {
	return m_stats.hp_percent < m_flee_hp_threshold;
}

std::vector<CombatTarget> CombatManager::GetNearbyHostiles(float range) {
	std::vector<CombatTarget> hostiles;
	const auto& entities = m_eq->GetEntities();
	glm::vec3 my_pos = m_eq->GetPosition();
	uint16_t my_id = m_eq->GetEntityID();

	LOG_TRACE(MOD_COMBAT, "GetNearbyHostiles: my_pos=({:.1f},{:.1f},{:.1f}), my_id={}, range={}, entities={}",
		my_pos.x, my_pos.y, my_pos.z, my_id, range, entities.size());

	int total_mobs = 0;
	int mobs_in_range = 0;

	for (const auto& [id, entity] : entities) {
		// Skip self
		if (id == my_id) {
			continue;
		}

		// Look for mobs (class_id 1 or 0 for NPCs)
		if (entity.class_id == 1 || entity.class_id == 0) {
			// Skip players (they have race 1-12 and class 1-16)
			bool is_player = false;
			if (entity.race_id >= 1 && entity.race_id <= 12 && entity.class_id >= 1 && entity.class_id <= 16) {
				// Additional check: NPCs usually have generic names, players have unique names
				// This is a heuristic - might need refinement
				if (entity.name.find("a_") != 0 && entity.name.find("an_") != 0) {
					is_player = true;
				}
			}

			// Skip corpses (check for both apostrophe and backtick)
			if (entity.name.find("'s corpse") != std::string::npos ||
			    entity.name.find("`s corpse") != std::string::npos ||
			    entity.name.find("corpse") != std::string::npos) {
				continue;
			}

			// Skip special NPCs like Guard, Merchant, etc.
			if (entity.name.find("Guard") != std::string::npos ||
				entity.name.find("Merchant") != std::string::npos ||
				entity.name.find("Banker") != std::string::npos) {
				continue;
			}

			if (!is_player) {
				total_mobs++;

				glm::vec3 target_pos(entity.x, entity.y, entity.z);
				float distance = glm::distance(my_pos, target_pos);

				if (distance <= range) {
					mobs_in_range++;

					CombatTarget target;
					target.entity_id = id;
					target.name = entity.name;
					target.distance = distance;
					target.hp_percent = entity.hp_percent;
					target.con_color = CON_WHITE; // Default until we consider
					target.priority = TARGET_PRIORITY_MEDIUM;
					target.is_aggro = false; // TODO: Determine from faction
					target.last_considered = std::chrono::steady_clock::now();
					target.has_consider_data = false;

					hostiles.push_back(target);

					LOG_TRACE(MOD_COMBAT, "  Found hostile: {} (ID:{}) at distance {:.1f}",
						entity.name, id, distance);
				}
			}
		}
	}

	LOG_TRACE(MOD_COMBAT, "GetNearbyHostiles: Found {} potential mobs, {} within range {}",
		total_mobs, mobs_in_range, range);

	return hostiles;
}

std::vector<CombatTarget> CombatManager::GetNearbyAllies(float range) {
	std::vector<CombatTarget> allies;
	const auto& entities = m_eq->GetEntities();
	glm::vec3 my_pos = m_eq->GetPosition();

	for (const auto& [id, entity] : entities) {
		if (IsAlly(entity)) {
			glm::vec3 target_pos(entity.x, entity.y, entity.z);
			float distance = glm::distance(my_pos, target_pos);

			if (distance <= range) {
				CombatTarget target;
				target.entity_id = id;
				target.name = entity.name;
				target.distance = distance;
				target.hp_percent = entity.hp_percent;
				target.con_color = CON_GREEN; // Allies are always green
				target.priority = TARGET_PRIORITY_LOWEST;
				target.is_aggro = false;

				allies.push_back(target);
			}
		}
	}

	return allies;
}

CombatTarget* CombatManager::GetHighestPriorityTarget() {
	if (m_potential_targets.empty()) {
		return nullptr;
	}

	// Sort by priority and distance
	std::sort(m_potential_targets.begin(), m_potential_targets.end(),
		[](const CombatTarget& a, const CombatTarget& b) {
			if (a.priority != b.priority) {
				return a.priority > b.priority;
			}
			return a.distance < b.distance;
		});

	return &m_potential_targets[0];
}

void CombatManager::InitiateFlee() {
	SetCombatState(COMBAT_STATE_FLEEING);
	m_flee_start_time = std::chrono::steady_clock::now();

	// Disable auto attack
	DisableAutoAttack();

	// Find best flee path
	SelectBestFleePath();
}

void CombatManager::LootCorpse(uint16_t corpse_id) {
	m_current_corpse_id = corpse_id;
	SetCombatState(COMBAT_STATE_LOOTING);

	LOG_DEBUG(MOD_COMBAT, "LootCorpse: Sending loot request for corpse ID {}", corpse_id);

	// Send loot request packet - server expects only the corpse ID (4 bytes)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt32(0, corpse_id); // Only the corpse entity ID
	m_eq->QueuePacket(HC_OP_LootRequest, &packet);
}

void CombatManager::LootItem(uint32_t item_slot) {
	if (m_current_corpse_id == 0) {
		return;
	}

	// Send loot item packet
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(LootingItem_Struct));
	LootingItem_Struct* li = (LootingItem_Struct*)packet.Data();
	li->lootee = m_current_corpse_id;
	li->looter = m_eq->GetEntityID();
	li->slot_id = item_slot;
	li->auto_loot = m_auto_loot_enabled ? 1 : 0;

	m_eq->QueuePacket(HC_OP_LootItem, &packet);
}

void CombatManager::LootAll() {
	if (m_current_corpse_id == 0 || m_loot_items.empty()) {
		return;
	}

	LOG_DEBUG(MOD_COMBAT, "Looting all {} items from corpse {}",
		m_loot_items.size(), m_current_corpse_id);

	// Loot each item
	for (uint32_t slot : m_loot_items) {
		LootItem(slot);
		// Small delay between looting items
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	// Clear the loot list
	m_loot_items.clear();

	// Close loot window after a short delay
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	CloseLootWindow();
}

void CombatManager::CloseLootWindow() {
	// Save corpse ID before clearing
	uint16_t corpseId = m_current_corpse_id;
	m_current_corpse_id = 0;
	m_loot_items.clear();

	// Mark this corpse as ready for deletion (server will send DeleteSpawn after EndLootRequest)
	m_eq->m_loot_complete_corpse_id = corpseId;

	// Send end loot request to server (OP_EndLootRequest, not OP_LootComplete)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt16(0, corpseId);
	packet.PutUInt16(2, 0);  // padding
	m_eq->QueuePacket(HC_OP_EndLootRequest, &packet);
	LOG_DEBUG(MOD_COMBAT, "CloseLootWindow: Sent HC_OP_EndLootRequest with corpseId={}", corpseId);

	// If we were looting, transition back to appropriate state
	if (m_combat_state == COMBAT_STATE_LOOTING) {
		if (m_auto_hunting_enabled) {
			// Go back to hunting after looting
			SetCombatState(COMBAT_STATE_HUNTING);
			LOG_DEBUG(MOD_COMBAT, "Finished looting, resuming hunt");
		} else {
			SetCombatState(COMBAT_STATE_IDLE);
		}
	}
}

void CombatManager::SetAutoHunting(bool enable) {
	// Check pathfinding requirement
	if (enable && !m_eq->IsPathfindingEnabled()) {
		std::cout << "[ERROR] Auto-hunting requires pathfinding to be enabled. Use 'pathfinding on' first.\n";
		return;
	}

	m_auto_hunting_enabled = enable;

	if (enable) {
		// Start hunting
		SetCombatState(COMBAT_STATE_HUNTING);
		m_last_hunt_update = std::chrono::steady_clock::now();

		// Also enable auto-movement for hunting
		EnableAutoMovement();

		LOG_DEBUG(MOD_COMBAT, "Auto-hunting enabled with auto-movement");
	} else {
		// Stop hunting
		if (m_combat_state == COMBAT_STATE_HUNTING || m_combat_state == COMBAT_STATE_RESTING) {
			SetCombatState(COMBAT_STATE_IDLE);
		}

		// Also disable auto-movement
		DisableAutoMovement();

		// Clear any pending consider state
		m_waiting_for_considers = false;
		m_pending_considers.clear();

		LOG_DEBUG(MOD_COMBAT, "Auto-hunting disabled");
	}
}

void CombatManager::UpdateAutoHunting() {
	auto now = std::chrono::steady_clock::now();

	// Don't update too frequently
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_hunt_update).count() < 500) {
		return;
	}
	m_last_hunt_update = now;

	// Check our state
	switch (m_combat_state) {
		case COMBAT_STATE_HUNTING:
			// Check if we should rest
			if (ShouldRest()) {
				StartResting();
				return;
			}

			// If we don't have a target, find one
			if (!HasTarget()) {
				FindNextHuntTarget();
			}
			break;

		case COMBAT_STATE_ENGAGED:
			// Combat is handled by normal combat update
			break;

		case COMBAT_STATE_RESTING:
			// Resting is handled in main Update()
			break;

		case COMBAT_STATE_IDLE:
			// If we're idle during hunting, go back to hunting
			if (m_auto_hunting_enabled) {
				// Wait a bit after combat before hunting again
				if (std::chrono::duration_cast<std::chrono::seconds>(now - m_combat_end_time).count() >= 3) {
					SetCombatState(COMBAT_STATE_HUNTING);
				}
			}
			break;

		case COMBAT_STATE_FLEEING:
		case COMBAT_STATE_SEEKING_GUARD:
			// Handled elsewhere or by main Update()
			break;

		case COMBAT_STATE_LOOTING:
			// After looting, go back to hunting
			// The actual transition happens when we receive loot window close
			// or when looting times out
			break;
	}
}

void CombatManager::LoadSpellSet(const std::string& spell_set_name) {
	// TODO: Load spell set from configuration
}

void CombatManager::SaveSpellSet(const std::string& spell_set_name) {
	// TODO: Save current spell set to configuration
}

std::vector<SpellInfo> CombatManager::GetMemorizedSpells() const {
	return m_memorized_spells;
}

void CombatManager::ScanForTargets() {
	LOG_TRACE(MOD_COMBAT, "ScanForTargets: using radius {}, state={}, hunting={}, {} existing targets",
		m_aggro_radius, static_cast<int>(m_combat_state), m_auto_hunting_enabled, m_potential_targets.size());

	// Don't clear potential targets if we're actively hunting - we need the consider data
	if (!m_auto_hunting_enabled || m_combat_state == COMBAT_STATE_IDLE) {
		m_potential_targets.clear();
	}

	// Get nearby hostiles
	auto hostiles = GetNearbyHostiles(m_aggro_radius);

	LOG_TRACE(MOD_COMBAT, "ScanForTargets: found {} targets after scan", hostiles.size());

	// Add to potential targets
	for (auto& hostile : hostiles) {
		// Check if already exists
		bool already_exists = false;
		for (auto& existing : m_potential_targets) {
			if (existing.entity_id == hostile.entity_id) {
				already_exists = true;
				// Update distance and other non-consider data
				existing.distance = hostile.distance;
				existing.hp_percent = hostile.hp_percent;
				break;
			}
		}
		if (!already_exists) {
			UpdateTargetPriorities();
			m_potential_targets.push_back(hostile);
		}
	}
}

void CombatManager::UpdateTargetPriorities() {
	// TODO: Update target priorities based on various factors
	// - Current HP of target
	// - Consider color
	// - Aggro status
	// - Distance
	// - Special mob types (named, casters, etc.)
}

float CombatManager::CalculateTargetPriority(const Entity& entity) {
	float priority = 0.0f;

	// Base priority on consider color
	// TODO: Get actual consider color
	priority += 50.0f;

	// Adjust for distance (closer = higher priority)
	glm::vec3 my_pos = m_eq->GetPosition();
	glm::vec3 target_pos(entity.x, entity.y, entity.z);
	float distance = glm::distance(my_pos, target_pos);
	priority += (100.0f - distance);

	// Adjust for HP (lower HP = higher priority)
	priority += (100.0f - entity.hp_percent);

	return priority;
}

HCConsiderColor CombatManager::GetConsiderColor(int level_diff) {
	// EverQuest consider colors based on level difference
	if (level_diff >= 2) return CON_RED;
	if (level_diff >= 1) return CON_YELLOW;
	if (level_diff >= 0) return CON_WHITE;
	if (level_diff >= -1) return CON_BLUE;
	if (level_diff >= -2) return CON_LIGHTBLUE;
	return CON_GREEN;
}

bool CombatManager::IsHostile(const Entity& entity) const {
	// TODO: Check faction standings
	// For now, assume all NPCs are potentially hostile
	return entity.class_id == 0; // NPC class
}

bool CombatManager::IsAlly(const Entity& entity) const {
	// TODO: Check faction standings, group membership, pets
	// For now, only consider other players as allies
	return entity.class_id >= 1 && entity.class_id <= 16; // Player classes
}

void CombatManager::SelectBestFleePath() {
	// Find safest direction to flee
	// For now, just run away from current target
	if (!HasTarget()) {
		return;
	}

	const auto& entities = m_eq->GetEntities();
	for (const auto& [id, entity] : entities) {
		if (id == m_current_target_id) {
			glm::vec3 my_pos = m_eq->GetPosition();
			glm::vec3 target_pos(entity.x, entity.y, entity.z);

			// Calculate opposite direction
			glm::vec3 flee_direction = glm::normalize(my_pos - target_pos);
			m_flee_destination = my_pos + (flee_direction * 100.0f);

			// Start moving
			m_eq->Move(m_flee_destination.x, m_flee_destination.y, m_flee_destination.z);
			break;
		}
	}
}

bool CombatManager::CanCastSpell(const SpellInfo& spell) {
	// Check mana
	if (m_stats.current_mana < spell.mana_cost) {
		return false;
	}

	// Check recast timer
	auto now = std::chrono::steady_clock::now();
	auto time_since_cast = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - spell.last_cast_time).count();

	if (time_since_cast < spell.recast_time_ms) {
		return false;
	}

	// Check range if we have a target
	if (HasTarget() && m_current_target_info) {
		if (m_current_target_info->distance > spell.range) {
			return false;
		}
	}

	return true;
}

void CombatManager::ProcessCombatRound() {
	LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: has_target={}, auto_attack={}, target_id={}",
		HasTarget(), m_auto_attack_enabled, m_current_target_id);

	if (!HasTarget() || !m_auto_attack_enabled) {
		LOG_TRACE(MOD_COMBAT, "ProcessCombatRound: Exiting early - no target or auto attack disabled");
		return;
	}

	auto now = std::chrono::steady_clock::now();

	// Check if we're in range for melee
	const auto& entities = m_eq->GetEntities();
	for (const auto& [id, entity] : entities) {
		if (id == m_current_target_id) {
			glm::vec3 my_pos = m_eq->GetPosition();
			glm::vec3 target_pos(entity.x, entity.y, entity.z);
			float distance = glm::distance(my_pos, target_pos);

			// Calculate actual combat range based on sizes
			float actual_combat_range = CalculateCombatRange(6.0f, entity.size, entity.race_id);

			LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: target={} ({}), distance={:.1f}, combat_range={:.1f}, in_range={}",
				entity.name, id, distance, actual_combat_range, (distance <= actual_combat_range));

			if (distance > actual_combat_range) {
				// Need to move closer if auto movement is enabled
				if (m_auto_movement_enabled) {
					LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: Out of range, starting combat movement (stop_distance={:.1f})",
						actual_combat_range * 0.8f);
					// Pass the calculated combat range to movement system
					m_eq->SetCombatStopDistance(actual_combat_range * 0.8f); // 80% to ensure we're well within range
					m_eq->StartCombatMovement(m_current_target_id);
				} else {
					LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: Out of range but auto movement disabled");
				}
			} else {
				// In range - first ensure we're facing the target
				LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: In range, facing target");
				m_eq->FaceEntity(entity.name);

				// Check attack timer
				auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_attack_time).count();
				LOG_TRACE(MOD_COMBAT, "ProcessCombatRound: In range! Time since last attack: {}ms (delay: {}ms)",
					time_since_last, m_attack_delay_ms);

				if (time_since_last >= m_attack_delay_ms) {
					// Time for next attack - server handles the actual attack
					m_last_attack_time = now;
					LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: Attack timer ready - server should process melee attack");
				}
			}

			// Check if target is dead
			if (entity.hp_percent <= 0) {
				LOG_DEBUG(MOD_COMBAT, "Target {} (ID: {}) died! hp_percent={}",
					entity.name, m_current_target_id, entity.hp_percent);

				// Target died
				uint16_t dead_target_id = m_current_target_id;
				std::string dead_target_name = entity.name;

				ClearTarget();
				DisableAutoAttack();

				// If auto-loot is enabled and we're hunting, look for the corpse
				if (m_auto_loot_enabled && m_auto_hunting_enabled) {
					LOG_DEBUG(MOD_COMBAT, "Auto-loot enabled, looking for corpse (auto_loot={}, auto_hunt={})",
						m_auto_loot_enabled, m_auto_hunting_enabled);

					// Mark when combat ended for auto-hunting cooldown
					m_combat_end_time = std::chrono::steady_clock::now();

					// Give the server a moment to spawn the corpse
					LOG_DEBUG(MOD_COMBAT, "Waiting 500ms for corpse to spawn...");
					std::this_thread::sleep_for(std::chrono::milliseconds(500));

					// The entity name is already the corpse name after OP_MobRename
					// It already contains "'s_corpse" with the entity ID appended
					std::string corpse_name = dead_target_name;  // Already in corpse format
					const auto& entities_check = m_eq->GetEntities();

					LOG_DEBUG(MOD_COMBAT, "Looking for corpse named '{}' among {} entities",
						corpse_name, entities_check.size());

					bool corpse_found = false;
					for (const auto& [corpse_id, corpse_entity] : entities_check) {
						LOG_TRACE(MOD_COMBAT, "  Checking entity: {} (ID: {}, is_corpse={})",
							corpse_entity.name, corpse_id, corpse_entity.is_corpse);

						// The corpse should have the exact same name as the dead entity
						// or be marked with is_corpse flag
						if (corpse_entity.is_corpse || corpse_entity.name == corpse_name) {
							// Found the corpse, initiate looting
							LOG_DEBUG(MOD_COMBAT, "Found corpse: {} (ID: {}), initiating loot",
								corpse_entity.name, corpse_id);
							// Move to corpse for looting
							m_eq->MoveToEntityWithinRange(corpse_entity.name, 10.0f);
							corpse_found = true;
							LootCorpse(corpse_id);
							return;
						}
					}

					if (!corpse_found) {
						LOG_DEBUG(MOD_COMBAT, "Corpse not found! Expected name: '{}'", corpse_name);
						// List nearby entities for debugging
						LOG_DEBUG(MOD_COMBAT, "Nearby entities:");
						for (const auto& [eid, ent] : entities_check) {
							glm::vec3 my_pos_check = m_eq->GetPosition();
							glm::vec3 ent_pos(ent.x, ent.y, ent.z);
							float dist = glm::distance(my_pos_check, ent_pos);
							if (dist <= 50.0f) {
								LOG_DEBUG(MOD_COMBAT, "  {} (ID: {}, dist: {:.1f}, is_corpse: {})",
									ent.name, eid, dist, ent.is_corpse);
							}
						}
					}
				} else {
					LOG_DEBUG(MOD_COMBAT, "Not looking for corpse: auto_loot={}, auto_hunt={}",
						m_auto_loot_enabled, m_auto_hunting_enabled);
				}

				SetCombatState(COMBAT_STATE_IDLE);
			}

			break;
		}
	}
}

void CombatManager::CheckForAggro() {
	// Check if any hostile mobs are aggroing us
	for (const auto& target : m_potential_targets) {
		if (target.is_aggro && target.distance <= m_aggro_radius) {
			// We're being attacked!
			SetTarget(target.entity_id);
			EnableAutoAttack();
			break;
		}
	}
}


float CombatManager::CalculateCombatRange(float my_size, float target_size, uint16_t target_race) const {
	// EverQuest melee range formula approximation
	// Base range is 14 units, modified by size
	float base_range = 14.0f;

	// Size modifiers - EQ uses a complex formula but this approximates it
	// Player size is typically 6.0 for medium races
	// Mob sizes vary greatly (1.0 for tiny to 250+ for raid bosses)
	float my_size_factor = my_size * 0.25f;  // Player size contribution
	float target_size_factor = target_size * 0.13f;  // Target size contribution

	// Minimum effective size to prevent too small ranges
	if (my_size_factor < 1.5f) my_size_factor = 1.5f;
	if (target_size_factor < 1.5f && target_size < 10.0f) target_size_factor = 1.5f;

	// Special race adjustments
	if (target_race >= 49 && target_race <= 52) { // Dragons
		target_size_factor *= 1.5f;
	} else if (target_race >= 17 && target_race <= 19) { // Giants
		target_size_factor *= 1.3f;
	}

	// Calculate final range
	float final_range = base_range + my_size_factor + target_size_factor;

	// Cap the range to reasonable values
	if (final_range < 5.0f) final_range = 5.0f;  // Minimum melee range
	if (final_range > 75.0f) final_range = 75.0f;  // Maximum melee range (for huge mobs)

	// Reduce the calculated range by 50% to ensure we're definitely in range
	float adjusted_range = final_range * 0.5f;

	LOG_TRACE(MOD_COMBAT, "Combat range: base={}, my_size={} (factor={}), target_size={} (factor={}), calculated={}, final={}",
		base_range, my_size, my_size_factor, target_size, target_size_factor, final_range, adjusted_range);

	return adjusted_range;
}

void CombatManager::FindNextHuntTarget() {
	LOG_DEBUG(MOD_COMBAT, "FindNextHuntTarget: Starting scan (state={}, auto_hunt={}, hunt_radius={}, waiting_for_considers={})",
		static_cast<int>(m_combat_state), m_auto_hunting_enabled, m_hunt_radius, m_waiting_for_considers);

	// If we're waiting for considers, check if they're done
	if (m_waiting_for_considers) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_consider_start_time).count();

		// Check if all pending considers have data or if we've waited too long
		bool all_done = true;
		for (uint16_t id : m_pending_considers) {
			bool found = false;
			for (const auto& target : m_potential_targets) {
				if (target.entity_id == id && target.has_consider_data) {
					found = true;
					break;
				}
			}
			if (!found) {
				all_done = false;
				break;
			}
		}

		// If all considers are done or we've waited too long (3 seconds), proceed
		if (!all_done && elapsed < 3) {
			LOG_DEBUG(MOD_COMBAT, "Still waiting for {} considers to complete ({}s elapsed)",
				m_pending_considers.size(), elapsed);
			return;
		}

		// Clear waiting state
		m_waiting_for_considers = false;
		m_pending_considers.clear();

		LOG_DEBUG(MOD_COMBAT, "All considers complete or timed out, selecting best target");
	}

	// If we don't have any potential targets, scan for them
	if (m_potential_targets.empty()) {
		ScanForTargets();
	}

	// Update distances for existing targets and get fresh hostile list
	auto current_hostiles = GetNearbyHostiles(m_hunt_radius);
	const auto& entities = m_eq->GetEntities();
	glm::vec3 my_pos = m_eq->GetPosition();

	// Update distances for potential targets
	for (auto& target : m_potential_targets) {
		glm::vec3 target_pos(0, 0, 0);
		bool found = false;
		for (const auto& [eid, e] : entities) {
			if (eid == target.entity_id) {
				target_pos = glm::vec3(e.x, e.y, e.z);
				found = true;
				break;
			}
		}
		if (found) {
			target.distance = glm::distance(my_pos, target_pos);
		}
	}

	// Add any new hostiles to potential targets
	for (const auto& hostile : current_hostiles) {
		bool already_exists = false;
		for (auto& existing : m_potential_targets) {
			if (existing.entity_id == hostile.entity_id) {
				already_exists = true;
				// Update distance for existing target
				existing.distance = hostile.distance;
				break;
			}
		}
		if (!already_exists) {
			m_potential_targets.push_back(hostile);
		}
	}

	LOG_DEBUG(MOD_COMBAT, "Have {} potential targets in list", m_potential_targets.size());

	// Filter targets based on hunting criteria
	std::vector<CombatTarget> needs_consider;
	std::vector<CombatTarget> ready_targets;

	for (const auto& target : m_potential_targets) {
		// Skip if out of range
		if (target.distance > m_hunt_radius) {
			continue;
		}

		// Find the entity
		const Entity* entity = nullptr;
		for (const auto& [eid, e] : entities) {
			if (eid == target.entity_id) {
				entity = &e;
				break;
			}
		}

		if (!entity) {
			continue;
		}

		LOG_DEBUG(MOD_COMBAT, "Checking {} (ID:{}, dist:{:.1f}, has_consider:{})",
			target.name, target.entity_id, target.distance, target.has_consider_data);

		// Categorize targets
		if (target.has_consider_data) {
			LOG_DEBUG(MOD_COMBAT, "{} has consider data: con_level={}, con_color={}, faction={}, hp={}/{}",
				entity->name, target.con_level, static_cast<int>(target.con_color),
				target.faction, target.cur_hp, target.max_hp);
			if (IsTargetSuitableForHunt(*entity)) {
				ready_targets.push_back(target);
				LOG_DEBUG(MOD_COMBAT, "{} is SUITABLE for hunting", entity->name);
			} else {
				LOG_DEBUG(MOD_COMBAT, "{} is NOT suitable for hunting", entity->name);
			}
		} else {
			// No consider data yet
			needs_consider.push_back(target);
			LOG_DEBUG(MOD_COMBAT, "{} needs consider check (no data yet)", entity->name);
		}
	}

	// Sort both lists by distance
	std::sort(needs_consider.begin(), needs_consider.end(),
		[](const CombatTarget& a, const CombatTarget& b) {
			return a.distance < b.distance;
		});

	std::sort(ready_targets.begin(), ready_targets.end(),
		[](const CombatTarget& a, const CombatTarget& b) {
			return a.distance < b.distance;
		});

	// If we have targets that need considering, do up to 3 considers
	if (!needs_consider.empty()) {
		int considers_sent = 0;
		m_pending_considers.clear();

		for (const auto& target : needs_consider) {
			if (considers_sent >= 3) break;

			LOG_DEBUG(MOD_COMBAT, "Sending consider request for {} (ID:{})",
				target.name, target.entity_id);

			if (SetTarget(target.entity_id)) {
				ConsiderTarget();
				m_pending_considers.push_back(target.entity_id);
				considers_sent++;
				// Don't clear target immediately, let next one override
			}
		}

		if (considers_sent > 0) {
			m_waiting_for_considers = true;
			m_consider_start_time = std::chrono::steady_clock::now();
			ClearTarget(); // Clear target after all considers sent

			LOG_DEBUG(MOD_COMBAT, "Sent {} consider requests, waiting for responses", considers_sent);
			return;
		}
	}

	// If we have ready targets, engage the best one
	if (!ready_targets.empty()) {
		const auto& best_target = ready_targets[0]; // Already sorted by distance

		LOG_DEBUG(MOD_COMBAT, "Attempting to engage best target {} (ID:{})",
			best_target.name, best_target.entity_id);

		if (SetTarget(best_target.entity_id)) {
			std::cout << fmt::format("Hunting: Engaging {} (con={}, faction={}, dist={:.1f})\n",
				best_target.name, static_cast<int>(best_target.con_color),
				best_target.faction, best_target.distance);

			LOG_DEBUG(MOD_COMBAT, "Calling EnableAutoAttack()");
			EnableAutoAttack();

			LOG_DEBUG(MOD_COMBAT, "Calling EnableAutoMovement()");
			EnableAutoMovement();
			return;
		} else {
			LOG_DEBUG(MOD_COMBAT, "Failed to set target {} for combat", best_target.entity_id);
		}
	}

	LOG_DEBUG(MOD_COMBAT, "No suitable targets found for hunting");
}

bool CombatManager::ShouldRest() const {
	// Rest if HP or Mana is below threshold
	return m_stats.hp_percent < m_rest_hp_threshold ||
		   m_stats.mana_percent < m_rest_mana_threshold;
}

void CombatManager::StartResting() {
	SetCombatState(COMBAT_STATE_RESTING);
	m_is_resting = true;

	// Sit down to rest
	m_eq->SetPositionState(POS_SITTING);

	std::cout << fmt::format("Resting: HP={}%, Mana={}%\n",
		m_stats.hp_percent, m_stats.mana_percent);
}

void CombatManager::StopResting() {
	m_is_resting = false;

	// Stand up
	m_eq->SetPositionState(POS_STANDING);

	std::cout << "Resuming hunt\n";
}

bool CombatManager::IsTargetSuitableForHunt(const Entity& entity) const {
	LOG_TRACE(MOD_COMBAT, "IsTargetSuitableForHunt: Checking {}", entity.name);

	// Skip if it doesn't look like a regular mob
	if (entity.name.find("'s corpse") != std::string::npos) {
		LOG_TRACE(MOD_COMBAT, "  {} is a corpse - NOT suitable", entity.name);
		return false;
	}

	// Find this entity in our potential targets to get consider data
	const CombatTarget* target_data = nullptr;
	for (const auto& target : m_potential_targets) {
		if (target.entity_id == entity.spawn_id && target.has_consider_data) {
			target_data = &target;
			break;
		}
	}

	if (!target_data || !target_data->has_consider_data) {
		// No consider data yet
		LOG_TRACE(MOD_COMBAT, "  {} has no consider data - suitable (needs consider)", entity.name);
		return true; // Consider it potential until we know
	}

	// Check con color (white, blue, light blue)
	if (target_data->con_color != CON_WHITE &&
		target_data->con_color != CON_BLUE &&
		target_data->con_color != CON_LIGHTBLUE) {
		LOG_TRACE(MOD_COMBAT, "  {} con color {} - NOT suitable",
			entity.name, static_cast<int>(target_data->con_color));
		return false;
	}

	// Check faction (5=indifferent, 6=dubious, 8=threatening, 9=scowls)
	if (target_data->faction < 5 || target_data->faction > 9) {
		LOG_TRACE(MOD_COMBAT, "  {} faction {} - NOT suitable",
			entity.name, target_data->faction);
		return false;
	}

	// For aggressive mobs (threatening/scowls), any con is OK
	if (target_data->faction >= 8) {
		LOG_TRACE(MOD_COMBAT, "  {} is aggressive (faction {}) - suitable",
			entity.name, target_data->faction);
		return true;
	}

	// For non-aggressive mobs, only hunt non-humanoids
	bool is_humanoid = IsHumanoid(entity.race_id);
	LOG_TRACE(MOD_COMBAT, "  {} race {} humanoid={} - {}",
		entity.name, entity.race_id, is_humanoid, is_humanoid ? "NOT suitable" : "suitable");
	return !is_humanoid;
}

Entity* CombatManager::FindNearestGuard() const {
	// TODO: Implement guard detection
	return nullptr;
}

void CombatManager::FleeToGuard() {
	auto* guard = FindNearestGuard();
	if (guard) {
		SetCombatState(COMBAT_STATE_SEEKING_GUARD);
		m_eq->Move(guard->x, guard->y, guard->z);
	} else {
		// No guard found, just flee in opposite direction
		SelectBestFleePath();
	}
}

bool CombatManager::IsGuard(const Entity& entity) const {
	return entity.name.find("Guard") != std::string::npos ||
		   entity.name.find("Sentinel") != std::string::npos;
}

bool CombatManager::IsCorpse(const Entity& entity) const {
	// Check both the is_corpse flag and name pattern
	return entity.is_corpse || entity.name.find("'s_corpse") != std::string::npos;
}

Entity* CombatManager::FindNearbyCorpse() const {
	const auto& entities = m_eq->GetEntities();
	glm::vec3 my_pos = m_eq->GetPosition();
	Entity* nearest_corpse = nullptr;
	float nearest_distance = std::numeric_limits<float>::max();

	for (const auto& [id, entity] : entities) {
		if (IsCorpse(entity)) {
			glm::vec3 corpse_pos(entity.x, entity.y, entity.z);
			float distance = glm::distance(my_pos, corpse_pos);

			if (distance < nearest_distance && distance <= 30.0f) {
				nearest_distance = distance;
				nearest_corpse = const_cast<Entity*>(&entity);
			}
		}
	}

	return nearest_corpse;
}

void CombatManager::ProcessConsiderResponse(uint32_t target_id, uint32_t faction, uint32_t level,
	int32_t cur_hp, int32_t max_hp) {

	LOG_DEBUG(MOD_COMBAT, "ProcessConsiderResponse: target_id={}, faction={}, con_level={}, hp={}/{}",
		target_id, faction, level, cur_hp, max_hp);

	// Update our potential targets list
	for (auto& target : m_potential_targets) {
		if (target.entity_id == target_id) {
			target.has_consider_data = true;
			target.faction = faction;
			target.con_level = level;  // This is the consider color ID from server
			target.cur_hp = cur_hp;
			target.max_hp = max_hp;

			// Map server con level to our enum
			// Server consider color IDs:
			// Green = 2, LightBlue = 10, DarkBlue = 4, White = 10 (titanium) or 20
			// Yellow = 15, Red = 13, Gray = 6
			switch (level) {
				case 2:  target.con_color = CON_GREEN; break;
				case 4:  target.con_color = CON_BLUE; break;  // DarkBlue
				case 6:  target.con_color = CON_GRAY; break;
				case 10: target.con_color = CON_LIGHTBLUE; break;  // Could also be White on Titanium
				case 13: target.con_color = CON_RED; break;
				case 15: target.con_color = CON_YELLOW; break;
				case 18: target.con_color = CON_LIGHTBLUE; break;  // Alternative LightBlue?
				case 20: target.con_color = CON_WHITE; break;
				default:
					// Unknown con level, default to white
					target.con_color = CON_WHITE;
					LOG_DEBUG(MOD_COMBAT, "Unknown con level {} for target {}, defaulting to WHITE",
						level, target_id);
					break;
			}

			LOG_DEBUG(MOD_COMBAT, "Updated consider data for {} (ID:{}): con_level={}, con_color={}, faction={}",
				target.name, target_id, level, static_cast<int>(target.con_color), faction);
			break;
		}
	}

	// Also update current target info if this is our target
	if (m_current_target_id == target_id && m_current_target_info) {
		m_current_target_info->has_consider_data = true;
		m_current_target_info->faction = faction;
		m_current_target_info->con_level = level;
		m_current_target_info->cur_hp = cur_hp;
		m_current_target_info->max_hp = max_hp;
	}

	// If we were waiting for this consider response during hunting, check if all are done
	if (m_waiting_for_considers) {
		// Remove this ID from pending list
		auto it = std::find(m_pending_considers.begin(), m_pending_considers.end(), target_id);
		if (it != m_pending_considers.end()) {
			m_pending_considers.erase(it);

			LOG_DEBUG(MOD_COMBAT, "Consider response received for ID {}, {} pending considers remain",
				target_id, m_pending_considers.size());

			// If all considers are done and we're still hunting, trigger immediate update
			if (m_pending_considers.empty() && m_combat_state == COMBAT_STATE_HUNTING && !HasTarget() && m_auto_hunting_enabled) {
				m_waiting_for_considers = false;
				// Force immediate hunt update by resetting the timer
				m_last_hunt_update = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);

				LOG_DEBUG(MOD_COMBAT, "All considers complete, triggering immediate hunt update");
			}
		}
	}
}

bool CombatManager::IsHumanoid(uint16_t race_id) const {
	// EverQuest humanoid races
	// Player races: 1-12
	// Other humanoid races: various
	switch (race_id) {
		// Player races
		case 1:   // Human
		case 2:   // Barbarian
		case 3:   // Erudite
		case 4:   // Wood Elf
		case 5:   // High Elf
		case 6:   // Dark Elf
		case 7:   // Half Elf
		case 8:   // Dwarf
		case 9:   // Troll
		case 10:  // Ogre
		case 11:  // Halfling
		case 12:  // Gnome
		// NPCs that are humanoid
		case 44:  // Dark Elf (NPC)
		case 55:  // Freeport Guard
		case 71:  // Human (NPC)
		case 77:  // Skeleton
		case 78:  // Ghoul
		case 81:  // Qeynos Citizen
		case 82:  // Erudin Citizen
		case 85:  // Spectre
		case 106: // Fiend
		case 110: // Erudite Ghost
		case 111: // Human Ghost
		case 112: // Iksar Ghost
		case 117: // Iksar Citizen
		case 118: // Forest Giant
		case 120: // Pirate
		case 122: // Kerran
		case 123: // Barbarian (NPC)
		case 124: // Erudite (NPC)
		case 125: // Troll (NPC)
		case 126: // Ogre (NPC)
		case 127: // Dwarf (NPC)
		case 128: // Iksar
		case 137: // Yeti
		case 139: // Coldain
		case 140: // Velious Dragon
		case 145: // Zombie
		case 146: // Mummy
		case 161: // Iksar Skeleton
			return true;
		default:
			return false;
	}
}

void CombatManager::ListHuntTargets() {
	// First scan for targets
	ScanForTargets();

	// Get entities from the zone
	const auto& entities = m_eq->GetEntities();
	std::vector<CombatTarget> hunt_targets;

	LOG_DEBUG(MOD_COMBAT, "ListHuntTargets: my_id={}, hunt_radius={}, total_entities={}",
		m_eq->GetEntityID(), m_hunt_radius, entities.size());

	// Check each entity
	for (const auto& [id, entity] : entities) {
		if (!entity.name.empty() && id != m_eq->GetEntityID()) {
			// Skip players, corpses, and special NPCs
			if (entity.class_id > 16 || entity.name.find("'s corpse") != std::string::npos) {
				continue;
			}

			// Check if it's a mob (class_id 1 or 0)
			if (entity.class_id != 1 && entity.class_id != 0) {
				continue;
			}

			// Calculate distance
			glm::vec3 my_pos = m_eq->GetPosition();
			glm::vec3 target_pos(entity.x, entity.y, entity.z);
			float distance = glm::distance(my_pos, target_pos);

			// Check if within hunt radius
			if (distance <= m_hunt_radius) {
				// Check if we have consider data for this target
				bool has_consider_data = false;
				HCConsiderColor con_color = CON_WHITE;
				uint32_t faction = 5; // Default to indifferent

				for (const auto& potential : m_potential_targets) {
					if (potential.entity_id == entity.spawn_id && potential.has_consider_data) {
						has_consider_data = true;
						con_color = potential.con_color;
						faction = potential.faction;
						break;
					}
				}

				// Check if it meets our hunting criteria
				bool is_suitable = false;
				std::string reason;

				if (has_consider_data) {
					// Check con color (white, blue, light blue)
					if (con_color == CON_WHITE || con_color == CON_BLUE || con_color == CON_LIGHTBLUE) {
						// Check faction (indifferent=5, dubious=6, threatening=8, scowls=9)
						if (faction >= 5 && faction <= 9) {
							// Check if it's non-humanoid or aggressive
							if (!IsHumanoid(entity.race_id) || faction >= 8) {
								is_suitable = true;
								reason = fmt::format("con={}, faction={}, humanoid={}",
									static_cast<int>(con_color), faction, IsHumanoid(entity.race_id));
							}
						}
					}
				} else {
					// No consider data yet - mark as potential
					reason = "no consider data";
				}

				CombatTarget target;
				target.entity_id = id;
				target.name = entity.name;
				target.distance = distance;
				target.hp_percent = entity.hp_percent;
				target.con_color = con_color;
				target.faction = faction;
				target.has_consider_data = has_consider_data;

				if (is_suitable || !has_consider_data) {
					hunt_targets.push_back(target);
				}

				LOG_TRACE(MOD_COMBAT, "Hunt check: {} (ID:{}) dist={:.1f} suitable={} reason={}",
					entity.name, entity.spawn_id, distance, is_suitable, reason);
			}
		}
	}

	// Sort by distance
	std::sort(hunt_targets.begin(), hunt_targets.end(),
		[](const CombatTarget& a, const CombatTarget& b) {
			return a.distance < b.distance;
		});

	// Display results
	if (hunt_targets.empty()) {
		std::cout << "No suitable hunt targets found within " << m_hunt_radius << " units\n";
	} else {
		std::cout << fmt::format("Found {} potential hunt targets:\n", hunt_targets.size());
		for (size_t i = 0; i < hunt_targets.size() && i < 10; ++i) {
			const auto& target = hunt_targets[i];
			std::string status = target.has_consider_data ?
				fmt::format("con={}, faction={}", static_cast<int>(target.con_color), target.faction) :
				"needs consider";
			std::cout << fmt::format("  {}: {} (ID:{}) - dist={:.1f}, hp={}%, {}\n",
				i + 1, target.name, target.entity_id, target.distance,
				static_cast<int>(target.hp_percent), status);
		}
	}
}

void CombatManager::ProcessLootWindow(const std::vector<uint32_t>& item_ids) {
	if (m_combat_state != COMBAT_STATE_LOOTING) {
		LOG_DEBUG(MOD_COMBAT, "Received loot window but not in looting state");
		return;
	}

	m_loot_items = item_ids;

	LOG_DEBUG(MOD_COMBAT, "Loot window opened with {} items", item_ids.size());

	// If auto-loot is enabled, loot everything
	if (m_auto_loot_enabled && !m_loot_items.empty()) {
		// Give a small delay before looting to simulate human behavior
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		LootAll();
	}
}

void CombatManager::AddLootItem(uint32_t slot_id) {
	// Add to our loot items list if not already there
	if (std::find(m_loot_items.begin(), m_loot_items.end(), slot_id) == m_loot_items.end()) {
		m_loot_items.push_back(slot_id);
		LOG_DEBUG(MOD_COMBAT, "Added loot item in slot {}, total items: {}",
			slot_id, m_loot_items.size());
	}
}

void CombatManager::CheckAutoLoot() {
	// If auto-loot is enabled and we have items, start looting after a delay
	if (m_auto_loot_enabled && m_combat_state == COMBAT_STATE_LOOTING && !m_loot_items.empty()) {
		// Give a moment for all items to arrive
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		LootAll();
	}
}
