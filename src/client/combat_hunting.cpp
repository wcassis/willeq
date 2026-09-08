// combat_hunting.cpp -- part 3 of 3: auto-hunting, consider handling, looting.
//
// J24 (Batch J, file-size reclamation) moved this cluster verbatim out of
// combat.cpp: hunt-target discovery, the consider response path, the hunt list,
// and the loot state machine that follows a kill. Move-only.
//
// The hunt-target scan is 192 lines and was moved WHOLE. Decomposing it is a
// recorded Batch-G candidate that this move-only commit deliberately does not
// touch.
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
			LOG_INFO(MOD_COMBAT, "Hunting: Engaging {} (con={}, faction={}, dist={:.1f})",
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
	// COMBAT_STATE_RESTING is the resting flag; T414 Leg B (board B299)
	// deleted the duplicate m_is_resting bool, which nothing read.
	SetCombatState(COMBAT_STATE_RESTING);

	// Sit down to rest
	m_eq->Movement().SetPositionState(POS_SITTING);

	LOG_INFO(MOD_COMBAT, "Resting: HP={}%, Mana={}%",
		m_stats.hp_percent, m_stats.mana_percent);
}

void CombatManager::StopResting() {
	// Stand up
	m_eq->Movement().SetPositionState(POS_STANDING);

	LOG_INFO(MOD_COMBAT, "Resuming hunt");
}

bool CombatManager::IsTargetSuitableForHunt(const Entity& entity) const {
	LOG_TRACE(MOD_COMBAT, "IsTargetSuitableForHunt: Checking {}", entity.name);

	// Skip if it doesn't look like a regular mob. T456 (board B478): this
	// was `name.find("'s corpse")`, a SIXTH copy the board's census missed --
	// and doubly wrong, because that form is only the PLAYER-corpse spelling,
	// so every npc corpse ("a rat`s_corpse17") passed straight through as a
	// live hunt target, while a live npc named "a corpse beetle" would not
	// have. IsCorpseEntity() is the one predicate, and it is in this file.
	if (IsCorpseEntity(entity)) {
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
	bool is_humanoid = IsHumanoid(entity);
	LOG_TRACE(MOD_COMBAT, "  {} race {} bodytype {} humanoid={} - {}",
		entity.name, entity.race_id, static_cast<int>(entity.bodytype), is_humanoid,
		is_humanoid ? "NOT suitable" : "suitable");
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
		m_eq->Movement().Move(guard->x, guard->y, guard->z);
	} else {
		// No guard found, just flee in opposite direction
		SelectBestFleePath();
	}
}

// ---------------------------------------------------------------------------
// T160: shared corpse recognition (declared in combat.h).
// ---------------------------------------------------------------------------

// T173: ParseCorpseName and the predicate itself moved to
// corpse_predicate.{h,cpp} -- the state layer's EntityManager needs the same
// question answered about a different Entity type. This overload is the only
// thing that stayed, because it is the only part that knows this struct.
bool IsCorpseEntity(const Entity& entity) {
	return IsCorpseByFields(entity.is_corpse, entity.npc_type, entity.name);
}

bool CombatManager::IsGuard(const Entity& entity) const {
	return entity.name.find("Guard") != std::string::npos ||
		   entity.name.find("Sentinel") != std::string::npos;
}

bool CombatManager::IsCorpse(const Entity& entity) const {
	// T160: one shared predicate (combat.h) -- flag, Spawn_Struct NPC field,
	// or any of eqemu's three corpse name forms.
	return IsCorpseEntity(entity);
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

// T308: server-authoritative, not a race-id table.
//
// This was the FIFTH live copy of the pre-JSON race numbering (T267, T270,
// T271 and T304 killed the other four, every one of them live and wrong).
// Checked against the audited race_models rows, its case comments were wrong
// for all but 1-12, 85 and 128: 44 "Dark Elf (NPC)" is FPM Freeport Guard,
// 55 "Freeport Guard" is BGM Human Beggar, 71 "Human (NPC)" is QCM Qeynos
// Citizen, 77 "Skeleton" is NGM Neriak Citizen, 78 "Ghoul" is EGM Erudite
// Citizen, 117 "Iksar Citizen" is GDM Ghost Dwarf, 140 "Velious Dragon" is
// FGI Forest Giant, 145 "Zombie" is GOO Goo, 146 "Mummy" is SSN Spectral
// Sarnak, and about twenty more. Hunt-target filtering
// (IsTargetSuitableForHunt) reasoned about the wrong creature type.
//
// The race data CANNOT answer this question: race_models has code / name /
// scale / model / colour and no creature-class field of any kind, and
// inventing one would be a sixth hand-written table (T271's standing
// objection, overruled only for the placeholder tints).
//
// The server already answers it. Spawn_Struct carries bodytype at offset 335
// and eq_entities.cpp parses it into Entity::bodytype for every spawn; eqemu
// stamps it from common/bodytypes.h, where Humanoid == 1. So the honest
// predicate is the body type the server sent, keyed on the shared constants
// in client/eq_body_types.h -- no table, no race ids, nothing to drift.
//
// bodytype 0 is not a valid eqemu value; it means no spawn data was recorded
// for this entity. Answering false there preserves the deleted switch's
// default for an unknown race.
bool CombatManager::IsHumanoid(const Entity& entity) const {
	return entity.bodytype == EQT::BodyType::Humanoid;
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
			// Skip players, corpses, and special NPCs. T456 (board B478):
			// the SEVENTH copy -- same "'s corpse" substring, same two ways
			// of being wrong. Ask the one predicate.
			if (entity.class_id > 16 || IsCorpseEntity(entity)) {
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
							if (!IsHumanoid(entity) || faction >= 8) {
								is_suitable = true;
								reason = fmt::format("con={}, faction={}, humanoid={}",
									static_cast<int>(con_color), faction, IsHumanoid(entity));
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
		LOG_INFO(MOD_COMBAT, "No suitable hunt targets found within {} units", m_hunt_radius);
	} else {
		LOG_INFO(MOD_COMBAT, "Found {} potential hunt targets:", hunt_targets.size());
		for (size_t i = 0; i < hunt_targets.size() && i < 10; ++i) {
			const auto& target = hunt_targets[i];
			std::string status = target.has_consider_data ?
				fmt::format("con={}, faction={}", static_cast<int>(target.con_color), target.faction) :
				"needs consider";
			LOG_INFO(MOD_COMBAT, "  {}: {} (ID:{}) - dist={:.1f}, hp={}%, {}",
				i + 1, target.name, target.entity_id, target.distance,
				static_cast<int>(target.hp_percent), status);
		}
	}
}

void CombatManager::AddLootItem(uint32_t slot_id) {
	// Add to our loot items list if not already there
	if (std::find(m_loot_items.begin(), m_loot_items.end(), slot_id) == m_loot_items.end()) {
		m_loot_items.push_back(slot_id);
		LOG_DEBUG(MOD_COMBAT, "Added loot item in slot {}, total items: {}",
			slot_id, m_loot_items.size());
	}
	// Refresh the AwaitingItems quiescence clock -- each arriving item
	// pushes the "loot everything now" decision out until items stop
	// arriving (or the hard deadline fires). See TickAwaitingItems.
	m_last_loot_item_time = std::chrono::steady_clock::now();
}

void CombatManager::RemoveLootItem(uint32_t slot_id) {
	// T268: the player took this slot by hand (/loot <item>, LootItemIntent)
	// and the server confirmed it -- drop it from the auto-loot queue so a
	// later LootAll() does not re-request an already-taken slot.
	auto it = std::find(m_loot_items.begin(), m_loot_items.end(), slot_id);
	if (it != m_loot_items.end()) {
		m_loot_items.erase(it);
		LOG_DEBUG(MOD_COMBAT, "Removed loot item in slot {}, remaining: {}", slot_id, m_loot_items.size());
	}
}

void CombatManager::CheckAutoLoot() {
	// T26: no longer blocks the calling thread (was: sleep_for(500ms) then
	// the also-blocking LootAll()). ZoneProcessLootItem() calls this once
	// per item packet on the game thread; it must return promptly since
	// nothing else advances (network reads, game tick, renderer intents)
	// while it runs. AddLootItem() already refreshed the quiescence timer;
	// UpdateLootState(), ticked every game frame, does the actual
	// AwaitingItems -> LootingItems advance once quiescence or the hard
	// deadline is reached. This is now just a defensive nudge in case items
	// arrived without AwaitingItems having been entered yet (e.g. direct
	// AddLootItem() calls in tests).
	if (!m_auto_loot_enabled || m_combat_state != COMBAT_STATE_LOOTING || m_loot_items.empty()) {
		return;
	}

	if (m_loot_sub_state == LootSubState::Idle) {
		EnterLootSubState(LootSubState::AwaitingItems);
	}
}

void CombatManager::EnterLootSubState(LootSubState state) {
	m_loot_sub_state = state;
	m_loot_state_entered = std::chrono::steady_clock::now();

	if (state == LootSubState::AwaitingItems) {
		m_last_loot_item_time = m_loot_state_entered;
	} else if (state == LootSubState::LootingItems) {
		// Pace the first item immediately on the next tick, matching the
		// legacy LootAll(): it sent slot 1 as soon as it started, then
		// waited kInterItemDelayMs before the next.
		m_last_item_send_time = m_loot_state_entered - std::chrono::milliseconds(kInterItemDelayMs);
	}
}

bool CombatManager::CorpseStillPresent(uint16_t corpse_id) const {
	if (corpse_id == 0) {
		return false;
	}
	const auto& entities = m_eq->GetEntities();
	return entities.find(corpse_id) != entities.end();
}

void CombatManager::AbortLoot() {
	// Reuses CloseLootWindow()'s cleanup (clears corpse id/items, sends
	// HC_OP_EndLootRequest, transitions CombatState back to HUNTING/IDLE).
	// Sending EndLootRequest for an already-vanished corpse is harmless --
	// it just tells the server we're done with a session it likely already
	// cleaned up on its own.
	CloseLootWindow();
	EnterLootSubState(LootSubState::Idle);
}

void CombatManager::UpdateLootState() {
	if (!m_eq || m_loot_sub_state == LootSubState::Idle) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	switch (m_loot_sub_state) {
		case LootSubState::AwaitingCorpse:
			TickAwaitingCorpse(now);
			break;
		case LootSubState::AwaitingItems:
			TickAwaitingItems(now);
			break;
		case LootSubState::LootingItems:
			TickLootingItems(now);
			break;
		case LootSubState::Closing:
			TickClosing(now);
			break;
		case LootSubState::ManualOpen:
			TickManualOpen();
			break;
		case LootSubState::Idle:
			break;
	}
}

void CombatManager::TickAwaitingCorpse(std::chrono::steady_clock::time_point now) {
	// T33: match the SPECIFIC victim by spawn_id, not "the first
	// is_corpse entity in the zone" -- EQ corpses persist for minutes, so
	// with more than one corpse present the old any-is_corpse rule would
	// grab whichever one the map happened to iterate to first. m_eq's
	// entity map is keyed by spawn_id, so this is an O(1) exact lookup,
	// not a scan.
	const auto& entities = m_eq->GetEntities();
	auto it = entities.find(m_pending_corpse_id);
	// T160: IsCorpse(), not the raw flag -- the id match is what T33 requires
	// and is unchanged; this only stops us timing out when the corpse arrives
	// as a fresh spawn rather than via the death path we watched.
	if (it != entities.end() && IsCorpse(it->second)) {
		LOG_DEBUG(MOD_COMBAT, "Found corpse: {} (ID: {}), initiating loot",
			it->second.name, m_pending_corpse_id);
		m_eq->Movement().MoveToEntityWithinRange(m_pending_corpse_id, 10.0f);
		BeginLootRequest(m_pending_corpse_id);
		return;
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_loot_state_entered).count();
	if (elapsed >= kCorpseSearchDeadlineMs) {
		LOG_DEBUG(MOD_COMBAT, "Corpse (id {}) never appeared within {}ms, abandoning loot",
			m_pending_corpse_id, kCorpseSearchDeadlineMs);
		m_pending_corpse_id = 0;
		EnterLootSubState(LootSubState::Idle);
		if (m_combat_state == COMBAT_STATE_LOOTING) {
			SetCombatState(m_auto_hunting_enabled ? COMBAT_STATE_HUNTING : COMBAT_STATE_IDLE);
		}
	}
}

void CombatManager::TickAwaitingItems(std::chrono::steady_clock::time_point now) {
	if (!CorpseStillPresent(m_current_corpse_id)) {
		LOG_DEBUG(MOD_COMBAT, "Corpse {} disappeared while awaiting items", m_current_corpse_id);
		AbortLoot();
		return;
	}

	auto since_last_item = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_loot_item_time).count();
	if (!m_loot_items.empty() && since_last_item >= kItemQuiescenceMs) {
		if (m_auto_loot_enabled) {
			LOG_DEBUG(MOD_COMBAT, "Item quiescence reached ({} items), starting to loot", m_loot_items.size());
			EnterLootSubState(LootSubState::LootingItems);
		} else {
			// T268: manual looting -- the window is the player's now.
			LOG_DEBUG(MOD_COMBAT, "Item quiescence reached ({} items), auto-loot off: holding the window open",
				m_loot_items.size());
			EnterLootSubState(LootSubState::ManualOpen);
		}
		return;
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_loot_state_entered).count();
	if (elapsed >= kAwaitingItemsHardDeadlineMs) {
		if (m_auto_loot_enabled && !m_loot_items.empty()) {
			LOG_DEBUG(MOD_COMBAT, "Hard deadline reached with {} items pending, looting anyway",
				m_loot_items.size());
			EnterLootSubState(LootSubState::LootingItems);
		} else if (!m_auto_loot_enabled && !m_loot_items.empty()) {
			// T268: auto-loot is off and there is something to take -- this
			// is the player's window now. Hold it open (no deadline) until
			// /loot <item>, /lootall, /lootclose, or the corpse goes away.
			LOG_DEBUG(MOD_COMBAT, "Auto-loot off, {} items on corpse {}: holding the window open",
				m_loot_items.size(), m_current_corpse_id);
			EnterLootSubState(LootSubState::ManualOpen);
		} else {
			LOG_DEBUG(MOD_COMBAT, "Hard deadline reached with no items, closing");
			AbortLoot();
		}
	}
}

void CombatManager::TickManualOpen() {
	if (!CorpseStillPresent(m_current_corpse_id)) {
		LOG_DEBUG(MOD_COMBAT, "Corpse {} disappeared while its loot window was open", m_current_corpse_id);
		AbortLoot();
	}
}

void CombatManager::TickLootingItems(std::chrono::steady_clock::time_point now) {
	if (!CorpseStillPresent(m_current_corpse_id)) {
		LOG_DEBUG(MOD_COMBAT, "Corpse {} disappeared while looting ({} items remaining)",
			m_current_corpse_id, m_loot_items.size());
		AbortLoot();
		return;
	}

	if (m_loot_items.empty()) {
		EnterLootSubState(LootSubState::Closing);
		return;
	}

	auto since_last_send = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_item_send_time).count();
	if (since_last_send >= kInterItemDelayMs) {
		uint32_t slot = m_loot_items.front();
		m_loot_items.erase(m_loot_items.begin());
		LootItem(slot);
		m_last_item_send_time = now;
	}
}

void CombatManager::TickClosing(std::chrono::steady_clock::time_point now) {
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_loot_state_entered).count();
	bool corpse_gone = !CorpseStillPresent(m_current_corpse_id);
	if (elapsed >= kClosingDelayMs || corpse_gone) {
		CloseLootWindow();
		EnterLootSubState(LootSubState::Idle);
	}
}
