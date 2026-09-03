// EverQuest entity teardown/health/appearance cluster: delete spawn, mob
// health, HP updates, entity lookup helpers, wear change, appearance
// application and illusion.
//
// Clusters of eq_entities.cpp (Batch F unit F11), split off by Batch J unit
// J08. Moved verbatim, no behavior change. See
// refactor/J08_eq_entities_split.md and refactor/F11_entities.md.
//
// K06 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_entities_appearance.cpp to here, and the bodies rebased from
// EverQuest onto eqt::entities::EntitiesComponent. Still verbatim -- only the
// substitutions §Mechanism 4 allows (m_game_state/m_bridge/m_entities/
// m_combat_manager/m_trade_manager/m_inventory_manager/m_buff_manager ->
// ctx_.*; m_loot_complete_corpse_id/m_loot_items/m_pending_loot_slots ->
// owner_.m_* (loot cluster, K11 owns the eventual move); AddChatSystemMessage()/
// OnPetRemoved()/OnSpawnRemovedGraphics()/RemoveEntityFromGameState()/
// UpdateInventoryStats()/IsFullyZonedIn()/CalculateDistance()/PosX()/PosY()/
// PosZ()/MySpawnId() -> owner_.*, they stay on EverQuest; s_debug_level ->
// EverQuest::s_debug_level. IsTrackedTarget() is a free function
// (common/logging.h), not an EverQuest member -- left bare. See the K06
// Review.

#include "client/eq.h"
#include "client/entities/entities_component.h"
#include "client/world_object.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/raid_update.h"
#include "client/spell/buff_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/position_frames.h"
#include "client/bridge/irrlicht_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"

// P28: entity_pose_state.h is game-state-owned (S2 decoupling comment in the
// header), and inventory_manager.h/item_instance.h are the same
// renderer-free data model used unconditionally elsewhere — un-guarded.
#include "client/state/entity_pose_state.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/door_sounds.h"
#endif

#include "client/animation_constants.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cmath>

void eqt::entities::EntitiesComponent::ZoneProcessDeleteSpawn(const EQ::Net::Packet &p)
{
	// DeleteSpawn is sent when an entity despawns
	// Format: spawn_id (2 bytes)
	
	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "DeleteSpawn packet too small: {} bytes", p.Length());
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);

	// Special case: spawn_id 0 might mean "delete current target"
	if (spawn_id == 0 && ctx_.combat_manager && ctx_.combat_manager->HasTarget()) {
		spawn_id = ctx_.combat_manager->GetTargetId();
		if (EverQuest::s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "DeleteSpawn with ID 0 interpreted as current target: {}", spawn_id);
		}
	}
	
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end()) {
		// Handle corpse deletion - only delete corpses we've finished looting
		if (it->second.is_corpse) {
			// If we're actively looting this corpse, close the loot window first
			if (ctx_.game_state.player().lootingCorpseId() == spawn_id) {
				LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) being deleted while looting, closing loot window", spawn_id, it->second.name);
				// Publish LootWindowClosed (D18a: bridge is sole path to renderer)
				if (ctx_.bridge) {
					eqt::state::LootWindowClosedData lwdata;
					lwdata.corpseId = spawn_id;
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::LootWindowClosed, std::move(lwdata)));
				}
				ctx_.Bridge().LootItems().clear();
				ctx_.Bridge().PendingLootSlots().clear();
				ctx_.game_state.player().clearLootingCorpse();  // Phase 7.7 / T286
				// Mark as ready for deletion
				ctx_.Bridge().LootCompleteCorpseId() = spawn_id;
				// T268: the loot policy machine leaves LOOTING with the window.
				if (ctx_.combat_manager) {
					ctx_.combat_manager->OnLootWindowClosed();
				}
			}

			// Only delete corpses we've completed looting
			// This prevents corpses from being deleted when NPCs die (server sends DeleteSpawn(0))
			if (ctx_.Bridge().LootCompleteCorpseId() != spawn_id) {
				LOG_TRACE(MOD_ENTITY, "Ignoring DeleteSpawn for corpse {} ({}) - not finished looting", spawn_id, it->second.name);
				return;
			}

			LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) removed by server after looting", spawn_id, it->second.name);
			ctx_.Bridge().LootCompleteCorpseId() = 0;  // Clear the flag
		}

		LOG_DEBUG(MOD_ENTITY, "Entity {} ({}) despawned", spawn_id, it->second.name);

		// Cancel trade if partner despawned
		if (ctx_.trade_manager && ctx_.trade_manager->isTrading() &&
			ctx_.trade_manager->getPartnerSpawnId() == spawn_id) {
			LOG_DEBUG(MOD_MAIN, "Trade partner despawned, canceling trade");
			ctx_.trade_manager->cancelTrade();
			ctx_.out.System("Trade cancelled - partner is no longer available");
		}

		// Check if our pet despawned
		if (spawn_id == ctx_.game_state.pet().petSpawnId()) {
			LOG_INFO(MOD_MAIN, "Pet {} ({}) despawned", spawn_id, it->second.name);
			// T286: PetState is the only store; OnPetRemoved's clearPet()
			// zeroes the id and the button states.
			// Clear pet buffs
			if (ctx_.buff_manager) {
				ctx_.buff_manager->clearPetBuffs();
			}
			// T221: was #ifdef EQT_HAS_GRAPHICS -- same T43 reasoning as
			// OnPetCreated() above: a pure bridge publish (now also the
			// source-side pet().clearPet()); gating it left the headless
			// build's pet state set forever after a despawn.
			ctx_.Bridge().OnPetRemoved();
		}

		// Check if our combat target despawned - disable auto-attack
		if (ctx_.combat_manager && ctx_.combat_manager->HasTarget() &&
			ctx_.combat_manager->GetTargetId() == spawn_id) {
			LOG_DEBUG(MOD_COMBAT, "Combat target {} ({}) despawned, disabling auto-attack", spawn_id, it->second.name);
			ctx_.combat_manager->DisableAutoAttack();
			ctx_.combat_manager->ClearTarget();
		}

#ifdef EQT_HAS_GRAPHICS
		ctx_.Bridge().OnSpawnRemovedGraphics(spawn_id);
#endif

		// Remove from both local map and EntityManager
		ctx_.entities.erase(it);
		ctx_.Player().RemoveEntityFromGameState(spawn_id);
	} else {
		LOG_DEBUG(MOD_ENTITY, "DeleteSpawn for unknown spawn_id: {}", spawn_id);
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessMobHealth(const EQ::Net::Packet &p)
{
	// MobHealth updates an entity's health percentage
	// Format: spawn_id (2 bytes), hp_percent (1 byte)
	
	if (p.Length() < 5) {
		LOG_WARN(MOD_ENTITY, "MobHealth packet too small: {} bytes", p.Length());
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t hp_percent = p.GetUInt8(4);
	
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end()) {
		uint8_t old_hp = it->second.hp_percent;
		it->second.hp_percent = hp_percent;
		if (EverQuest::s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
			LOG_DEBUG(MOD_ENTITY, "[HP] Entity {} ({}) health: {}% -> {}%", spawn_id, it->second.name, old_hp, hp_percent);
		}
		// T221: source-side writes, hoisted out of the bridge guard below.
		// Target HP: the TargetChanged re-publish's projection (T104) is
		// redundant with this. Pet HP: the PetStatsChanged projection (T40)
		// is redundant with this. manaPercent keeps T40's honest derivation
		// (reads 0 for every pet until something populates cur/max_mana).
		if (ctx_.game_state.combat().targetId() == spawn_id) {
			ctx_.game_state.combat().setTarget(spawn_id, it->second.name, hp_percent, it->second.level);
		}
		if (ctx_.game_state.pet().petSpawnId() == spawn_id) {
			uint8_t manaPercent = (it->second.max_mana > 0)
				? static_cast<uint8_t>((it->second.cur_mana * 100) / it->second.max_mana)
				: 0;
			ctx_.game_state.pet().updatePetStats(hp_percent, manaPercent);
		}
		// Update target HP via bridge if this is our current target (D17b)
		// Note: EntityStatsChanged event published below covers this
		// Publish EntityStatsChanged event
		if (ctx_.bridge) {
			eqt::state::EntityStatsChangedData data;
			data.spawnId = spawn_id;
			data.hpPercent = hp_percent;
			data.curMana = 0;
			data.maxMana = 0;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityStatsChanged, std::move(data)));
			// If this entity is the current target, push TargetChanged with updated HP
			if (ctx_.game_state.combat().targetId() == spawn_id) {
				eqt::state::TargetChangedData tdata;
				tdata.spawnId = spawn_id;
				tdata.hpPercent = hp_percent;
				tdata.name = it->second.name;
				tdata.level = it->second.level;
				tdata.raceId = it->second.race_id;
				tdata.gender = it->second.gender;
				tdata.classId = it->second.class_id;
				tdata.bodyType = it->second.bodytype;
				tdata.npcType = it->second.npc_type;
				tdata.helm = it->second.helm;
				tdata.showHelm = it->second.showhelm;
				tdata.texture = it->second.equip_chest2;
				for (int i = 0; i < 9; ++i) {
					tdata.equipment[i] = it->second.equipment[i];
					tdata.equipmentTint[i] = it->second.equipment_tint[i];
				}
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TargetChanged, std::move(tdata)));
			}
			// T40: if this entity is our pet, push PetStatsChanged with the
			// updated HP. manaPercent is computed from the entity record for
			// honesty rather than fabricated -- but Entity::cur_mana/max_mana
			// are populated only for the player's own entity (ManaChange
			// packet is player-only), never for pets, so this will read 0
			// for every pet until/unless something starts populating it.
			if (ctx_.game_state.pet().petSpawnId() == spawn_id) {
				eqt::state::PetStatsChangedData pdata;
				pdata.spawnId = spawn_id;
				pdata.hpPercent = hp_percent;
				pdata.manaPercent = (it->second.max_mana > 0)
					? static_cast<uint8_t>((it->second.cur_mana * 100) / it->second.max_mana)
					: 0;
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PetStatsChanged, std::move(pdata)));
			}
		}
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessHPUpdate(const EQ::Net::Packet &p)
{
	// HPUpdate is sent for an entity's HP
	// SpawnHPUpdate_Struct format: cur_hp (4 bytes), max_hp (4 bytes), spawn_id (2 bytes)
	// Total: 10 bytes + 2 byte opcode = 12 bytes
	// Note: Mana is sent separately via HC_OP_ManaChange

	if (p.Length() < 12) {
		LOG_WARN(MOD_ENTITY, "HPUpdate packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t cur_hp = p.GetUInt32(2);
	int32_t max_hp = p.GetInt32(6);
	uint16_t spawn_id = p.GetUInt16(10);

	// Check if this is our own HP update
	bool is_self = (spawn_id == ctx_.self.MySpawnId());

	if (is_self) {
		// Update player HP tracking (T286: PlayerState is the only store)
		ctx_.game_state.player().setHP(cur_hp, static_cast<uint32_t>(max_hp > 0 ? max_hp : 0));
	}

	if (is_self) {
		LOG_DEBUG(MOD_ENTITY, "Player HP: {}/{} (spawn_id={})", cur_hp, max_hp, spawn_id);
	} else {
		LOG_TRACE(MOD_ENTITY, "Entity {} HP: {}/{}", spawn_id, cur_hp, max_hp);
	}

	// Update entity HP if we're tracking it
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end()) {
		it->second.hp_percent = (max_hp > 0) ? static_cast<uint8_t>(cur_hp * 100 / max_hp) : 100;
	}

	// Update combat stats for our player
	if (is_self && ctx_.combat_manager) {
		CombatStats stats;
		stats.current_hp = cur_hp;
		stats.max_hp = max_hp > 0 ? static_cast<uint32_t>(max_hp) : 0;
		// Don't update mana here - it comes from ManaChange packet
		stats.current_mana = ctx_.game_state.player().curMana();
		stats.max_mana = ctx_.game_state.player().maxMana();
		stats.current_endurance = 0;
		stats.max_endurance = 0;
		stats.hp_percent = (max_hp > 0) ? (static_cast<float>(cur_hp) * 100.0f / static_cast<float>(max_hp)) : 100.0f;
		stats.mana_percent = (ctx_.game_state.player().maxMana() > 0) ? (static_cast<float>(ctx_.game_state.player().curMana()) * 100.0f / static_cast<float>(ctx_.game_state.player().maxMana())) : 100.0f;
		stats.endurance_percent = 100.0f;
		ctx_.combat_manager->UpdateCombatStats(stats);
	}

	// T43: was #ifdef EQT_HAS_GRAPHICS. UpdateInventoryStats() (eq_bridge.cpp)
	// is unguarded/declassified since P28 -- pure inventory-manager
	// equipment-stat calculation + bridge event push, no renderer dependency.
	// Left gated here, a headless build's equipment stats (weight, weapon
	// skill types) never refreshed when player HP changed.
	if (is_self) {
		ctx_.Bridge().UpdateInventoryStats();
	}

	// Publish PlayerStatsChanged when our own HP changes
	if (is_self && ctx_.bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = ctx_.game_state.player().curHP();
		data.maxHP = ctx_.game_state.player().maxHP();
		data.curMana = ctx_.game_state.player().curMana();
		data.maxMana = ctx_.game_state.player().maxMana();
		data.curEndurance = ctx_.game_state.player().curEndurance();
		data.maxEndurance = ctx_.game_state.player().maxEndurance();
		data.level = ctx_.game_state.player().level();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}
}

Entity* eqt::entities::EntitiesComponent::FindEntityByName(const std::string& name)
{
	// Find entity by name (case-insensitive prefix match)
	// Replace spaces with underscores in search term
	std::string name_lower = name;
	std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
	std::replace(name_lower.begin(), name_lower.end(), ' ', '_');
	
	for (auto& pair : ctx_.entities) {
		Entity& entity = pair.second;
		std::string entity_name_lower = entity.name;
		std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
		
		if (entity_name_lower.find(name_lower) == 0) {  // Starts with
			return &entity;
		}
	}
	
	return nullptr;
}

void eqt::entities::EntitiesComponent::ListEntities(const std::string& search) const
{
	if (!ctx_.self.IsFullyZonedIn()) {
		std::cout << "Not in zone yet" << std::endl;
		return;
	}
	
	if (ctx_.entities.empty()) {
		std::cout << "No entities in zone" << std::endl;
		return;
	}
	
	// Prepare search string
	std::string search_lower = search;
	if (!search.empty()) {
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
		std::replace(search_lower.begin(), search_lower.end(), ' ', '_');
	}
	
	if (search.empty()) {
		std::cout << fmt::format("Entities in zone ({} total):", ctx_.entities.size()) << std::endl;
	} else {
		std::cout << fmt::format("Entities matching '{}' in zone:", search) << std::endl;
	}
	
	// Sort entities by distance from player
	std::vector<std::pair<float, const Entity*>> sorted_entities;
	for (const auto& pair : ctx_.entities) {
		const Entity& entity = pair.second;
		
		// Filter by search term if provided
		if (!search.empty()) {
			std::string entity_name_lower = entity.name;
			std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
			if (entity_name_lower.find(search_lower) == std::string::npos) {
				continue;  // Skip entities that don't match search
			}
		}
		
		float dist = ctx_.Movement().CalculateDistance(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity.x, entity.y, entity.z);
		sorted_entities.push_back({dist, &entity});
	}
	
	std::sort(sorted_entities.begin(), sorted_entities.end(), 
		[](const auto& a, const auto& b) { return a.first < b.first; });
	
	// Check if any entities matched
	if (sorted_entities.empty() && !search.empty()) {
		std::cout << fmt::format("  No entities found matching '{}'", search) << std::endl;
		return;
	}
	
	// Display up to 20 nearest entities
	int count = 0;
	for (const auto& [dist, entity] : sorted_entities) {
		if (count++ >= 20) {
			std::cout << "  ... and more" << std::endl;
			break;
		}
		
		// T311: two decimals, not zero. This listing is the instrument a
		// second (observing) client is read with, and the whole T311
		// question is a 0.75-unit difference in z -- {:.0f} rounded 3.00
		// and 3.75 to "3" and "4" and could not show it.
		std::cout << fmt::format("  {} (ID: {}) - Level {} {} - {:.1f} units away at ({:.2f}, {:.2f}, {:.2f})",
			entity->name, entity->spawn_id, entity->level,
			entity->class_id == 0 ? "NPC" : fmt::format("Class {}", entity->class_id),
			dist, entity->x, entity->y, entity->z) << std::endl;
			
		if (entity->hp_percent < 100) {
			std::cout << fmt::format("    HP: {}%", entity->hp_percent) << std::endl;
		}
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessWearChange(const EQ::Net::Packet &p)
{
	// WearChange indicates equipment changes on entities
	// Titanium Format: spawn_id (2 bytes), material (2 bytes), color (4 bytes), wear_slot_id (1 byte)
	// Total: 9 bytes + 2 byte opcode = 11 bytes

	if (p.Length() != 11) {
		LOG_WARN(MOD_ENTITY, "WearChange packet wrong size: {} bytes (expected 11)", p.Length());
		return;
	}

	// Skip opcode (2 bytes), then parse struct
	uint16_t spawn_id = p.GetUInt16(2);      // offset 0 in struct, 2 in packet
	uint16_t material = p.GetUInt16(4);      // offset 2 in struct, 4 in packet
	uint32_t color = p.GetUInt32(6);         // offset 4 in struct, 6 in packet
	uint8_t wear_slot = p.GetUInt8(10);      // offset 8 in struct, 10 in packet

	LOG_DEBUG(MOD_ENTITY, "Equipment change for spawn_id={}: slot {} material {} color {:08X}",
		spawn_id, wear_slot, material, color);

	// Update the entity's equipment data
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end()) {
		Entity& entity = it->second;

		// Update equipment slot if valid (slots 0-8 map to equipment array)
		if (wear_slot < 9) {
			entity.equipment[wear_slot] = material;
			entity.equipment_tint[wear_slot] = color;
		}

		// Build EntityAppearanceChanged; written to the source GameState
		// unconditionally (T221), published to the bridge when present
		// (D17b: bridge is sole path to renderer).
		eqt::state::EntityAppearanceChangedData data;
		data.spawnId = spawn_id;
		data.raceId = entity.race_id;
		data.gender = entity.gender;
		data.isPlayer = (spawn_id == ctx_.self.MySpawnId());
		data.face = entity.face;
		data.haircolor = entity.haircolor;
		data.hairstyle = entity.hairstyle;
		data.beardcolor = entity.beardcolor;
		data.beard = entity.beard;
		data.texture = entity.equip_chest2;
		data.helm = entity.helm;
		for (int j = 0; j < 9; j++) {
			data.equipment[j] = entity.equipment[j];
			data.equipmentTint[j] = entity.equipment_tint[j];
		}
		ApplyAppearanceToGameState(data);
		if (ctx_.bridge) {
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
		}
	}
}

// T221: the source-side twin of GameStateSyncBridge's EntityAppearanceChanged
// projection (T81) -- the same partial merge (NOT size; that field is not on
// the event). Applied to the built event payload rather than to m_entities,
// because UpdatePlayerAppearanceFromInventory() derives equipment from
// INVENTORY items and never writes them back to m_entities -- merging the
// payload is the only way the source matches what the projection held.
// No-op for an unknown spawn, as the projection was.
void eqt::entities::EntitiesComponent::ApplyAppearanceToGameState(const eqt::state::EntityAppearanceChangedData& d)
{
	auto* existing = ctx_.game_state.entities().getEntityMutable(d.spawnId);
	if (!existing) {
		return;
	}
	existing->raceId = d.raceId;
	existing->gender = d.gender;
	existing->face = d.face;
	existing->haircolor = d.haircolor;
	existing->hairstyle = d.hairstyle;
	existing->beardcolor = d.beardcolor;
	existing->beard = d.beard;
	existing->equipChest2 = d.texture;
	existing->helm = d.helm;
	for (int i = 0; i < 9; ++i) {
		existing->equipment[i] = d.equipment[i];
		existing->equipmentTint[i] = d.equipmentTint[i];
	}
}

void eqt::entities::EntitiesComponent::UpdatePlayerAppearanceFromInventory()
{
	// T221: was `!m_bridge || !m_inventory_manager` -- the source write below
	// must not depend on a bridge being attached; only the publish does.
	if (!ctx_.inventory_manager) {
		return;
	}

	// Find the player entity
	auto it = ctx_.entities.find(ctx_.self.MySpawnId());
	if (it == ctx_.entities.end()) {
		LOG_WARN(MOD_ENTITY, "UpdatePlayerAppearanceFromInventory: player entity {} not found", ctx_.self.MySpawnId());
		return;
	}

	Entity& entity = it->second;

	// Build appearance data from inventory items (D17b: bridge is sole path to renderer)
	eqt::state::EntityAppearanceChangedData data;
	data.spawnId = ctx_.self.MySpawnId();
	data.raceId = entity.race_id;
	data.gender = entity.gender;
	data.isPlayer = true;
	data.face = entity.face;
	data.haircolor = entity.haircolor;
	data.hairstyle = entity.hairstyle;
	data.beardcolor = entity.beardcolor;
	data.beard = entity.beard;
	data.texture = entity.equip_chest2;
	data.helm = entity.helm;

	// Initialize equipment arrays from entity (fallback)
	for (int i = 0; i < 9; i++) {
		data.equipment[i] = entity.equipment[i];
		data.equipmentTint[i] = entity.equipment_tint[i];
	}

	// Mapping from inventory slots to equipment appearance indices
	// Note: EQ equipment indices are: 0=Head, 1=Chest, 2=Arms, 3=Wrist, 4=Hands, 5=Legs, 6=Feet, 7=Primary, 8=Secondary
	struct SlotMapping {
		int16_t inventorySlot;
		int equipSlotIndex;
	};
	const SlotMapping mappings[] = {
		{ eqt::inventory::SLOT_HEAD, 0 },      // Head
		{ eqt::inventory::SLOT_CHEST, 1 },     // Chest
		{ eqt::inventory::SLOT_ARMS, 2 },      // Arms
		{ eqt::inventory::SLOT_WRIST1, 3 },    // Wrist (use first wrist slot)
		{ eqt::inventory::SLOT_HANDS, 4 },     // Hands
		{ eqt::inventory::SLOT_LEGS, 5 },      // Legs
		{ eqt::inventory::SLOT_FEET, 6 },      // Feet
		{ eqt::inventory::SLOT_PRIMARY, 7 },   // Primary
		{ eqt::inventory::SLOT_SECONDARY, 8 }, // Secondary
	};

	LOG_DEBUG(MOD_INVENTORY, "UpdatePlayerAppearanceFromInventory: Building appearance from inventory");
	for (const auto& mapping : mappings) {
		const auto* item = ctx_.inventory_manager->getItem(mapping.inventorySlot);
		if (item) {
			// Body armor: use material for texture variant and color for tint
			if (mapping.equipSlotIndex < 7) {
				data.equipment[mapping.equipSlotIndex] = item->material;
				data.equipmentTint[mapping.equipSlotIndex] = item->color;
			} else {
				// Weapons: use item ID for model lookup
				data.equipment[mapping.equipSlotIndex] = item->itemId;
			}
		} else {
			// No item in slot - clear the appearance slot
			data.equipment[mapping.equipSlotIndex] = 0;
			data.equipmentTint[mapping.equipSlotIndex] = 0;
		}
	}

	ApplyAppearanceToGameState(data);  // T221
	if (ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
	}

	LOG_DEBUG(MOD_INVENTORY, "UpdatePlayerAppearanceFromInventory: Updated player appearance");
}

void eqt::entities::EntitiesComponent::ZoneProcessIllusion(const EQ::Net::Packet &p)
{
	// Illusion packet indicates race/appearance changes from illusion spells
	// Packet format: opcode(2) + Illusion_Struct(168)

	if (p.Length() < 2 + sizeof(EQT::Illusion_Struct)) {
		LOG_WARN(MOD_ENTITY, "Illusion packet too small: {} bytes (expected {})",
		         p.Length(), 2 + sizeof(EQT::Illusion_Struct));
		return;
	}

	const EQT::Illusion_Struct* illusion = reinterpret_cast<const EQT::Illusion_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	uint32_t spawn_id = illusion->spawnid;
	int32_t new_race = illusion->race;
	uint8_t new_gender = illusion->gender;
	uint8_t new_texture = illusion->texture;
	uint8_t new_helm = illusion->helmtexture;
	float new_size = illusion->size;

	auto it = ctx_.entities.find(static_cast<uint16_t>(spawn_id));
	if (it == ctx_.entities.end()) {
		LOG_DEBUG(MOD_ENTITY, "Illusion for unknown entity {}", spawn_id);
		return;
	}

	Entity& entity = it->second;
	std::string entity_name = entity.name;

	LOG_INFO(MOD_ENTITY, "{} (ID: {}) illusioned to race {} gender {} texture {} helm {} size {:.2f}",
	         entity_name, spawn_id, new_race, new_gender, new_texture, new_helm, new_size);

	// Update entity data
	entity.race_id = static_cast<uint16_t>(new_race);
	entity.gender = new_gender;
	entity.equip_chest2 = new_texture;
	entity.helm = new_helm;
	entity.size = new_size;

	// Copy additional appearance fields if present
	entity.face = illusion->face;
	entity.hairstyle = illusion->hairstyle;
	entity.haircolor = illusion->haircolor;
	entity.beard = illusion->beard;
	entity.beardcolor = illusion->beardcolor;

	// Build EntityAppearanceChanged; source write unconditional (T221),
	// bridge publish when present (D17b: bridge is sole path to renderer).
	eqt::state::EntityAppearanceChangedData data;
	data.spawnId = static_cast<uint16_t>(spawn_id);
	data.raceId = entity.race_id;
	data.gender = entity.gender;
	data.isPlayer = (spawn_id == ctx_.self.MySpawnId());
	data.face = entity.face;
	data.haircolor = entity.haircolor;
	data.hairstyle = entity.hairstyle;
	data.beardcolor = entity.beardcolor;
	data.beard = entity.beard;
	data.texture = entity.equip_chest2;
	data.helm = entity.helm;
	for (int j = 0; j < 9; j++) {
		data.equipment[j] = entity.equipment[j];
		data.equipmentTint[j] = entity.equipment_tint[j];
	}
	ApplyAppearanceToGameState(data);
	if (ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
		// T318: EntityAppearanceChanged swaps the MODEL (race/gender/textures);
		// the illusion's own size rides in the same packet and is a separate
		// concern for the renderer, so it goes out as its own event -- the same
		// one AT_SIZE publishes.
		if (new_size > 0.0f) {
			eqt::state::EntitySizeChangedData sizeData;
			sizeData.spawnId = static_cast<uint16_t>(spawn_id);
			sizeData.size = new_size;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntitySizeChanged, std::move(sizeData)));
		}
	}
}

