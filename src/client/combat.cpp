// combat.cpp -- part 1 of 3: targeting and auto-attack.
//
// J24 (Batch J, file-size reclamation) lifted the other two thirds out of this
// file verbatim: the per-round combat pump now lives in combat_rounds.cpp and
// the auto-hunt / consider / looting cluster in combat_hunting.cpp. The packet
// structure block below stays here, together with its matched global structure
// packing directive pair, because this is the only translation unit that reads
// any of it.
#include "client/combat.h"
#include "client/eq_body_types.h"
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"
#include "common/net/daybreak_connection.h"
#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#endif
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fmt/format.h>
#include <limits>

// Packet structures based on Titanium client
#pragma pack(1)
struct ClientTarget_Struct {
	uint32_t new_target;
};

// T326: two further packet structs that stood here had no reader in this TU or
// its two J24 siblings, and were deleted. The namespaced equivalents in
// include/common/packet_structs.h are the live ones.
// Recover with: git show bcbf2ddc:src/client/combat.cpp
#pragma pack()

CombatManager::CombatManager(EverQuest* eq)
	: m_eq(eq)
	, m_enabled(false)  // Disabled by default, use /combat to enable
	, m_combat_state(COMBAT_STATE_IDLE)
	, m_current_target_id(0)
	, m_auto_attack_enabled(false)
	, m_auto_fire_enabled(false)
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
	// Initialize memorized spells vector
	m_memorized_spells.resize(MAX_SPELL_GEMS);

	// Initialize stats to avoid immediate resting - will be updated by server
	m_stats.hp_percent = 100.0f;
	m_stats.mana_percent = 100.0f;
	m_stats.endurance_percent = 100.0f;

	// Initialize time points
	m_last_target_scan = std::chrono::steady_clock::now();
	m_last_attack_time = std::chrono::steady_clock::now();
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

	// T245: called every game tick (~60 Hz) from EverQuest::UpdateMovement();
	// keep the 500 ms cadence the deleted m_update_thread's sleep imposed.
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_update).count() < kUpdateIntervalMs) {
		return;
	}
	m_last_update = now;

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
			// T286: EverQuest kept a third copy of the target id; deleted.
			// PublishTargetChanged below is the single write, into CombatState.

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

			// T104: publish so Application::m_gameState->combat() picks up
			// the new target regardless of which caller reached this choke
			// point (mouse-click TargetIntent already publishes its own
			// TargetChanged in eq_bridge.cpp -- this is a harmless redundant
			// publish for that one path, see this unit's Review; every other
			// caller -- /target, targetNearest*, cycleTargets, assist/duel/
			// taunt current-target reads, auto-hunt -- had NO publish before
			// this).
			PublishTargetChanged(entity);

			return true;
		}
	}

	return false;
}

// T230: CombatManager::SetTarget(const std::string&) -- the SECOND,
// case-sensitive, corpse-unaware name resolver T173 flagged -- was DELETED
// here together with its only caller chain (IActionHandler /
// ActionDispatcher / EqActionHandler::targetEntityByName), none of which
// had a production caller. The ONE name resolver is
// EntityManager::findEntityByName (state layer, case-insensitive,
// living-over-corpse ranking), reached via CommandProcessor::cmdTarget;
// this class only ever targets by resolved spawn id now.

void CombatManager::ClearTarget() {
	m_current_target_id = 0;
	m_current_target_info.reset();
	// T286: PublishTargetCleared below is the single write, into CombatState.

	// Send clear target packet
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(ClientTarget_Struct));
	ClientTarget_Struct* ct = (ClientTarget_Struct*)packet.Data();
	ct->new_target = 0;

	m_eq->QueuePacket(HC_OP_TargetMouse, &packet);

	// T104: publish so the projection clears regardless of caller (despawn
	// of the current target, ProcessCombatRound's post-death clear,
	// EqActionHandler::clearTarget(), etc.) -- none of these published
	// before this unit except the OnSpawnRemovedGraphics despawn path
	// (eq_bridge.cpp), which already pushes its own clear -- harmless
	// redundant publish there, see Review.
	PublishTargetCleared();
}

// T104: the three publish helpers below are the sole place this class
// pushes cross-thread bridge events. Called only from SetTarget(uint16_t)/
// ClearTarget()/EnableAutoAttack()/DisableAutoAttack() -- the choke points
// that own m_current_target_id/m_auto_attack_enabled -- so every caller
// (CycleTargets, auto-hunt, EqActionHandler's direct
// calls, the TargetIntent path) is covered without touching any of them.
// No-op if there is no bridge (headless-without-graphics runs, or tests
// that never call EverQuest::setBridge()) -- mirrors every other
// m_bridge-gated publish in this codebase (eq_bridge.cpp).
void CombatManager::PublishTargetChanged(const Entity& entity) {
	// T221: source-side write (friend access to m_game_state, same grant the
	// m_bridge read below already uses); TargetChanged projection redundant.
	m_eq->m_game_state.combat().setTarget(entity.spawn_id, entity.name,
	                                      entity.hp_percent, entity.level);
	if (!m_eq->m_bridge) {
		return;
	}
	eqt::state::TargetChangedData data;
	data.spawnId = entity.spawn_id;
	data.name = entity.name;
	data.level = entity.level;
	data.hpPercent = entity.hp_percent;
	data.raceId = entity.race_id;
	data.gender = entity.gender;
	data.classId = entity.class_id;
	data.bodyType = entity.bodytype;
	data.npcType = entity.npc_type;
	data.helm = entity.helm;
	data.showHelm = entity.showhelm;
	data.texture = entity.equip_chest2;
	for (int i = 0; i < 9; ++i) {
		data.equipment[i] = entity.equipment[i];
		data.equipmentTint[i] = entity.equipment_tint[i];
	}
	m_eq->m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::TargetChanged, std::move(data)));
}

void CombatManager::PublishTargetCleared() {
	m_eq->m_game_state.combat().clearTarget();  // T221
	if (!m_eq->m_bridge) {
		return;
	}
	eqt::state::TargetChangedData data;  // spawnId=0 = cleared
	m_eq->m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::TargetChanged, std::move(data)));
}

void CombatManager::WriteAutoAttackState(bool enabled) {
	// T221: the one GameState is written at the source. T264: the
	// AutoAttackChanged bridge event that used to follow had no consumer on
	// either bridge (the GameStateSyncBridge projection it fed died with
	// T227) and was deleted.
	m_eq->m_game_state.combat().setAutoAttacking(enabled);
}

uint16_t CombatManager::GetConsiderOpcodeForTarget() const {
	if (!HasTarget()) {
		return 0;
	}

	// T153: corpses take a different opcode. eqemu resolves
	// OP_ConsiderCorpse's target through entity_list.GetCorpseByID()
	// (zone/client_packet.cpp:5253) and OP_Consider's through the mob list,
	// so considering a corpse on OP_Consider never resolves a target and the
	// handler returns without a reply.
	const auto& entities = m_eq->GetEntities();
	auto it = entities.find(m_current_target_id);
	if (it != entities.end() && IsCorpse(it->second)) {
		return HC_OP_ConsiderCorpse;
	}
	return HC_OP_Consider;
}

void CombatManager::ConsiderTarget() {
	if (!HasTarget()) {
		return;
	}

	// T153: /consider (and its "con" alias) is the only entry point; there is
	// deliberately no separate /considercorpse command, which would be wired
	// but practically unreachable.
	if (GetConsiderOpcodeForTarget() == HC_OP_ConsiderCorpse) {
		m_eq->Player().SendConsiderCorpse(m_current_target_id);
		LOG_DEBUG(MOD_COMBAT, "Sent corpse consider request for target ID: {}",
			m_current_target_id);
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

void CombatManager::CycleTargets(bool forward) {
	// Get entities and player position
	const auto& entities = m_eq->GetEntities();
	glm::vec3 my_pos = m_eq->GetPosition();
	uint16_t my_id = m_eq->GetEntityID();

	// Build/refresh the cycle list if needed
	// Rebuild if empty or if we've moved significantly since last build
	static glm::vec3 last_pos = my_pos;
	float dist_moved = glm::length(my_pos - last_pos);

	if (m_cycle_targets.empty() || dist_moved > 50.0f) {
		m_cycle_targets.clear();
		m_cycle_index = -1;
		last_pos = my_pos;

		// Collect all nearby targetable entities within range
		const float CYCLE_RANGE = 200.0f;

		for (const auto& [id, entity] : entities) {
			// Skip self
			if (id == my_id) continue;

			// Skip corpses. T160: the shared predicate, not the raw flag --
			// before T160 a player corpse (or any corpse that predated our
			// zone-in) was tab-targetable because nothing had ever set its
			// is_corpse flag.
			if (IsCorpse(entity)) continue;

			// Calculate distance
			glm::vec3 entity_pos(entity.x, entity.y, entity.z);
			float distance = glm::length(entity_pos - my_pos);

			if (distance <= CYCLE_RANGE) {
				m_cycle_targets.push_back(id);
			}
		}

		// Sort by distance
		std::sort(m_cycle_targets.begin(), m_cycle_targets.end(),
			[&entities, &my_pos](uint16_t a, uint16_t b) {
				auto it_a = entities.find(a);
				auto it_b = entities.find(b);
				if (it_a == entities.end()) return false;
				if (it_b == entities.end()) return true;

				glm::vec3 pos_a(it_a->second.x, it_a->second.y, it_a->second.z);
				glm::vec3 pos_b(it_b->second.x, it_b->second.y, it_b->second.z);
				return glm::length(pos_a - my_pos) < glm::length(pos_b - my_pos);
			});

		LOG_DEBUG(MOD_COMBAT, "Rebuilt cycle target list with {} entities", m_cycle_targets.size());
	}

	if (m_cycle_targets.empty()) {
		LOG_DEBUG(MOD_COMBAT, "No targets available for cycling");
		return;
	}

	// Find current target in list if we have one
	if (m_cycle_index < 0 && HasTarget()) {
		auto it = std::find(m_cycle_targets.begin(), m_cycle_targets.end(), m_current_target_id);
		if (it != m_cycle_targets.end()) {
			m_cycle_index = static_cast<int>(std::distance(m_cycle_targets.begin(), it));
		}
	}

	// Move to next/previous target
	if (forward) {
		m_cycle_index++;
		if (m_cycle_index >= static_cast<int>(m_cycle_targets.size())) {
			m_cycle_index = 0;
		}
	} else {
		m_cycle_index--;
		if (m_cycle_index < 0) {
			m_cycle_index = static_cast<int>(m_cycle_targets.size()) - 1;
		}
	}

	// Set the new target
	uint16_t new_target = m_cycle_targets[m_cycle_index];
	SetTarget(new_target);

	LOG_DEBUG(MOD_COMBAT, "Cycled to target index {} (ID: {})", m_cycle_index, new_target);
}

void CombatManager::ToggleAutoAttack() {
	if (m_auto_attack_enabled) {
		DisableAutoAttack();
	} else if (HasTarget()) {
		EnableAutoAttack();
	}
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

	// Start auto-attack music
#ifdef WITH_AUDIO
	if (auto* audio = m_eq->GetAudioManager()) {
		audio->startAutoAttackMusic();
	}
#endif

	LOG_DEBUG(MOD_COMBAT, "EnableAutoAttack: Auto attack ENABLED (sent both AutoAttack and AutoAttack2)");

	// T104: replaces the auto-attack half of Application::syncCombatState().
	WriteAutoAttackState(true);
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

	// Stop auto-attack music
#ifdef WITH_AUDIO
	if (auto* audio = m_eq->GetAudioManager()) {
		audio->stopAutoAttackMusic();
	}
#endif

	// T104: replaces the auto-attack half of Application::syncCombatState().
	WriteAutoAttackState(false);
}

void CombatManager::NotifyTargetDied(uint16_t victim_id) {
	// T33: the real entry point. Called from
	// EverQuest::ZoneProcessDeath() -- the server's authoritative death
	// packet -- for a victim that was our current combat target, right
	// after DisableAutoAttack(). Carries over exactly what the old
	// ProcessCombatRound() hp_percent<=0 block did once a kill was
	// confirmed, minus the ClearTarget()/DisableAutoAttack() calls the
	// caller is responsible for (ZoneProcessDeath does not call
	// ClearTarget() today -- see plan STOP condition on not widening
	// combat-shutdown ordering beyond loot).
	if (!m_auto_loot_enabled || !m_auto_hunting_enabled) {
		LOG_DEBUG(MOD_COMBAT, "Not looking for corpse: auto_loot={}, auto_hunt={}",
			m_auto_loot_enabled, m_auto_hunting_enabled);
		return;
	}

	LOG_DEBUG(MOD_COMBAT, "Auto-loot enabled, looking for corpse (auto_loot={}, auto_hunt={})",
		m_auto_loot_enabled, m_auto_hunting_enabled);

	// Mark when combat ended for auto-hunting cooldown.
	m_combat_end_time = std::chrono::steady_clock::now();

	// T33: match the specific victim by spawn_id, not "any is_corpse
	// entity in the zone" (the wrong-corpse bug the pre-wiring code never
	// hit only because the AwaitingCorpse door was dead). ZoneProcessDeath
	// already set is_corpse=true on this same entity synchronously before
	// calling here, and eqemu renames corpses in place rather than
	// spawning a new entity (confirmed against
	// benchmarks/audits/T32/transcript_primary.txt), so the id captured
	// at death is stable for the corpse's whole lifetime.
	m_pending_corpse_id = victim_id;
	SetCombatState(COMBAT_STATE_LOOTING);
	EnterLootSubState(LootSubState::AwaitingCorpse);
	LOG_DEBUG(MOD_COMBAT, "Awaiting corpse (id {}) (deadline {}ms)",
		m_pending_corpse_id, kCorpseSearchDeadlineMs);
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
					target.is_aggro = IsRecentAttacker(id); // T247: it has hit us lately
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

