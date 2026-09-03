// combat_rounds.cpp -- part 2 of 3: the per-round combat pump.
//
// J24 (Batch J, file-size reclamation) moved this cluster verbatim out of
// combat.cpp: ally scanning, the auto-hunt update, the combat round itself, and
// the range and suitability helpers it calls. Move-only -- no interface,
// behaviour or decomposition change.
//
// This TU deliberately carries no copy of combat.cpp's packet structure block
// or of its matched global structure packing directive pair: nothing here reads
// them, and a half-copied pair silently changes the layout of every struct
// declared after it. See refactor/J24_combat_split.md.
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
	// The caller (shift+click via EqActionHandler::lootCorpse, or the T23
	// test fixtures) already has a corpse id in hand -- skip AwaitingCorpse
	// and go straight to requesting loot.
	BeginLootRequest(corpse_id);
}

void CombatManager::BeginLootRequest(uint16_t corpse_id) {
	m_current_corpse_id = corpse_id;
	m_loot_items.clear();
	SetCombatState(COMBAT_STATE_LOOTING);

	LOG_DEBUG(MOD_COMBAT, "BeginLootRequest: requesting loot for corpse ID {}", corpse_id);

	// T268: EverQuest::RequestLootCorpse is the ONE OP_LootRequest sender --
	// it also sets the game-side "window open" state (m_player_looting_corpse_id,
	// PlayerState::setLootingCorpse) and publishes LootWindowOpened, so the
	// item packets that follow populate EverQuest::m_loot_items (names
	// included) for both this path and the shift+click intent.
	m_eq->RequestLootCorpse(corpse_id);

	EnterLootSubState(LootSubState::AwaitingItems);
}

void CombatManager::LootItem(uint32_t item_slot) {
	if (m_current_corpse_id == 0) {
		return;
	}
	// T268: the ONE OP_LootItem sender. auto_loot=1: straight to inventory
	// (eqemu Corpse::LootCorpseItem -> AutoPutLootInInventory); a cursor
	// item would make the server refuse the next take
	// (Character:CheckCursorEmptyWhenLooting).
	m_eq->LootItemFromCorpse(m_current_corpse_id, static_cast<int16_t>(item_slot), true);
}

bool CombatManager::LootAll() {
	if (m_current_corpse_id == 0) {
		return false;
	}
	if (m_loot_items.empty()) {
		// Nothing left to take: just close, the way the drained machine does.
		LOG_DEBUG(MOD_COMBAT, "LootAll: no items on corpse {}, closing", m_current_corpse_id);
		EnterLootSubState(LootSubState::Closing);
		return true;
	}

	LOG_DEBUG(MOD_COMBAT, "Looting all {} items from corpse {} (event-driven, T26)",
		m_loot_items.size(), m_current_corpse_id);

	// T26: no longer blocks. Prime LootingItems and return immediately --
	// UpdateLootState(), ticked every game frame, drains m_loot_items one
	// slot per kInterItemDelayMs and closes the window kClosingDelayMs
	// after the last one (see TickLootingItems/TickClosing). Safe to call
	// from AwaitingItems (CheckAutoLoot's path) or ManualOpen (/lootall,
	// LootAllIntent -- T268).
	EnterLootSubState(LootSubState::LootingItems);
	return true;
}

void CombatManager::CloseLootWindow() {
	uint16_t corpseId = m_current_corpse_id;
	if (corpseId == 0) {
		OnLootWindowClosed();
		return;
	}
	// T268: EverQuest::CloseLootWindow is the ONE OP_EndLootRequest sender;
	// it calls back OnLootWindowClosed() for the state transition below.
	m_eq->CloseLootWindow(corpseId);
}

void CombatManager::OnLootWindowClosed() {
	m_current_corpse_id = 0;
	m_loot_items.clear();
	if (m_loot_sub_state != LootSubState::Idle) {
		EnterLootSubState(LootSubState::Idle);
	}

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
	if (enable && !m_eq->Movement().IsPathfindingEnabled()) {
		LOG_ERROR(MOD_COMBAT, "Auto-hunting requires pathfinding to be enabled. Use 'pathfinding on' first.");
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
				existing.is_aggro = hostile.is_aggro;   // T247
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
			m_eq->Movement().Move(m_flee_destination.x, m_flee_destination.y, m_flee_destination.z);
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
					m_eq->Movement().StartCombatMovement(m_current_target_id);
				} else {
					LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: Out of range but auto movement disabled");
				}
			} else {
				// In range - first ensure we're facing the target
				LOG_DEBUG(MOD_COMBAT, "ProcessCombatRound: In range, facing target");
				m_eq->Movement().FaceEntity(entity.name);

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

				// Target died. T33: this branch is no longer the primary
				// door into looting -- EverQuest::ZoneProcessDeath()
				// (the real server's authoritative death packet) now
				// calls CombatManager::NotifyTargetDied() directly, and
				// that call happens synchronously on the game thread
				// before this poll (T245: also on the game thread now,
				// paced to kUpdateIntervalMs from UpdateMovement()) can
				// next observe hp_percent<=0 -- DisableAutoAttack()
				// there flips m_combat_state out of ENGAGED, and Update()
				// only calls ProcessCombatRound() while ENGAGED, so this
				// branch cannot fire for the same kill afterward. It is
				// kept, not removed, because it is not *provably*
				// unreachable: a server that sent an explicit 0%
				// OP_MobHealth packet ahead of OP_Death (T32's transcript
				// shows this particular fight never did) could still let
				// this poll observe the death first (same thread now, so
				// a packet-ordering window, not a cross-thread one).
				// NotifyTargetDied() is idempotent against a
				// stale re-entry (same corpse id, same target state), so
				// this is a harmless safety net now, not a second
				// diverging entry point.
				uint16_t dead_target_id = m_current_target_id;

				ClearTarget();
				DisableAutoAttack();
				NotifyTargetDied(dead_target_id);

				if (m_combat_state != COMBAT_STATE_LOOTING) {
					SetCombatState(COMBAT_STATE_IDLE);
				}
				return;
			}

			break;
		}
	}
}

void CombatManager::CheckForAggro() {
	// Check if any hostile mobs are aggroing us. is_aggro comes from
	// NoteAttackedBy (T247); before that it was a hard-coded false and this
	// never fired. T254: with several attackers engage the one that hit us
	// LAST (ties -> nearest), not whichever the entity-map scan listed first.
	const CombatTarget* best = nullptr;
	std::chrono::steady_clock::time_point best_at{};
	for (const auto& target : m_potential_targets) {
		if (!target.is_aggro || target.distance > m_aggro_radius) {
			continue;
		}
		auto it = m_recent_attackers.find(target.entity_id);
		auto at = it != m_recent_attackers.end() ? it->second : std::chrono::steady_clock::time_point{};
		if (!best || at > best_at || (at == best_at && target.distance < best->distance)) {
			best = &target;
			best_at = at;
		}
	}
	if (best) {
		// We're being attacked!
		LOG_INFO(MOD_COMBAT, "CheckForAggro: {} (ID:{}) attacked us at {:.1f} (latest of {} attacker(s)) -- engaging",
			best->name, best->entity_id, best->distance, m_recent_attackers.size());
		if (SetTarget(best->entity_id)) {
			EnableAutoAttack();
		}
	}
}

void CombatManager::NoteAttackedBy(uint16_t attacker_id) {
	if (attacker_id == 0) {
		return;
	}
	auto now = std::chrono::steady_clock::now();
	m_recent_attackers[attacker_id] = now;
	// Forget entries past the memory window so the map cannot grow with a
	// long session's worth of dead ids.
	for (auto it = m_recent_attackers.begin(); it != m_recent_attackers.end();) {
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() > kAggroMemoryMs) {
			it = m_recent_attackers.erase(it);
		} else {
			++it;
		}
	}
}

bool CombatManager::IsRecentAttacker(uint16_t entity_id) const {
	auto it = m_recent_attackers.find(entity_id);
	if (it == m_recent_attackers.end()) {
		return false;
	}
	auto age = std::chrono::steady_clock::now() - it->second;
	return std::chrono::duration_cast<std::chrono::milliseconds>(age).count() <= kAggroMemoryMs;
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

