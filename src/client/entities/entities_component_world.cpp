// EverQuest door and world-object cluster: the door-type helper, move door,
// the spawn-appearance/animation/click-door senders, and the world object
// (tradeskill container) senders and handlers.
//
// Clusters of eq_entities.cpp (Batch F unit F11), split off by Batch J unit
// J08. Moved verbatim, no behavior change. See
// refactor/J08_eq_entities_split.md and refactor/F11_entities.md.
//
// NOTE: SendTradeSkillCombine and ZoneProcessTradeSkillCombine (nestled
// among the world-object senders/handlers in eq.cpp, between
// SendClickObject/SendCloseContainer and ZoneProcessClickObjectAction/
// SendJump respectively) are item-economy, judgment-called to stay in
// eq.cpp for consistency with F05 (merchant/inventory/trade split).
//
// NOTE: ZoneProcessPlayerStateAdd/Death/PlayerStateRemove/Stamina/
// ZonePlayerToBind/LevelUpdate (immediately following ZoneProcessMoveDoor
// in eq.cpp) are not named in this unit's scope and stay in eq.cpp.
//
// K06 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_entities_world.cpp to here, and the bodies rebased from
// EverQuest onto eqt::entities::EntitiesComponent. Still verbatim -- only the
// substitutions §Mechanism 4 allows (m_inventory_manager/m_bridge/
// m_zone_connection/m_active_tradeskill_object_id/m_game_state/
// m_audio_manager -> ctx_.*; m_world_objects -> m_world_objects (four
// reader sites remain on EverQuest: eq_player_misc.cpp,
// eq_action_handler_spells_trade.cpp, eq_bridge_intents.cpp,
// action/action_dispatcher.cpp -- moves here only when they retarget);
// PosX()/PosY()/PosZ()/MySpawnId()/IsFullyZonedIn() -> owner_.*, they stay on
// EverQuest; s_debug_level -> EverQuest::s_debug_level. m_pending_door_clicks
// moved WITH this family, so its reference needs no prefix at all. See the
// K06 Review. (T376/B141: that member is GONE -- the pending-click set is the
// door state manager's, reached as ctx_.game_state.doors().)
//
// T341 (board B038): the "stay on EverQuest" clauses above are HISTORY, not
// the current shape. This family now reaches EverQuest not at all -- the
// `owner_` handle and the `friend class` line are both gone; see the T341
// note in this family's component header.

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
#include "client/core/memory_ledger.h"  // B919: the gs.doors owner row
#include "common/logging.h"
#include "common/packet_structs.h"
#ifdef WITH_AUDIO
#include "client/audio/ui_sounds.h"
#endif
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

// Helper function to determine door type from door name
#ifdef WITH_AUDIO
static EQT::Audio::DoorType getDoorTypeFromName(const std::string& name) {
	// Convert to lowercase for comparison
	std::string lowerName = name;
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

	// Check for secret doors
	if (lowerName.find("secret") != std::string::npos ||
	    lowerName.find("hidden") != std::string::npos) {
		return EQT::Audio::DoorType::Secret;
	}

	// Check for metal doors/gates
	if (lowerName.find("gate") != std::string::npos ||
	    lowerName.find("iron") != std::string::npos ||
	    lowerName.find("metal") != std::string::npos ||
	    lowerName.find("grate") != std::string::npos ||
	    lowerName.find("portcullis") != std::string::npos ||
	    lowerName.find("bars") != std::string::npos) {
		return EQT::Audio::DoorType::Metal;
	}

	// Check for stone doors
	if (lowerName.find("stone") != std::string::npos ||
	    lowerName.find("rock") != std::string::npos ||
	    lowerName.find("boulder") != std::string::npos ||
	    lowerName.find("crypt") != std::string::npos ||
	    lowerName.find("tomb") != std::string::npos) {
		return EQT::Audio::DoorType::Stone;
	}

	// Check for sliding doors
	if (lowerName.find("slide") != std::string::npos ||
	    lowerName.find("panel") != std::string::npos) {
		return EQT::Audio::DoorType::Sliding;
	}

	// Default to wood for most doors
	return EQT::Audio::DoorType::Wood;
}
#endif

void eqt::entities::EntitiesComponent::ZoneProcessMoveDoor(const EQ::Net::Packet &p)
{
	// MoveDoor indicates door animations (opening/closing).
	// Packet format: opcode(2) + MoveDoor_Struct(2).
	//
	// T435 (board B428): the action byte means "open" or "close" only once
	// invert_state is applied. eqemu zone/doors.cpp:37-40 --
	//     #define OPEN_DOOR      0x02
	//     #define CLOSE_DOOR     0x03
	//     #define OPEN_INVDOOR   0x03
	//     #define CLOSE_INVDOOR  0x02
	// and every emitter picks between the pairs with
	// `m_invert_state == 0 ? OPEN_DOOR : OPEN_INVDOOR` (doors.cpp:274, 314,
	// 376, 402, 434, 474, 631, 663, 734). The old reading here was
	// `is_open = (action == 0x03)`, which is the INVERTED-door mapping applied
	// to every door in the game: a normal door read backwards on every packet,
	// and an invert_state=1 door (all three Kelethin FAYLEVATORs) read right by
	// accident.

	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "MoveDoor packet too small: {} bytes", p.Length());
		return;
	}

	uint8_t door_id = p.GetUInt8(2);
	uint8_t action = p.GetUInt8(3);

	// B919 (#238): named `game_state` owner row (WILLEQ_LEDGER_OWNERS=game_state):
	// the DoorStateManager's door table (doors_) and its pending-click set
	// (pendingClicks_, T376) are gs.doors.
	eqt::memory::StartupScope owner("gs.doors");

	// Update door state in game state manager
	const EQT::DoorState* door = ctx_.game_state.doors().getDoor(door_id);
	if (door) {
		constexpr uint8_t kOpenDoor = 0x02;     // eqemu OPEN_DOOR  / CLOSE_INVDOOR
		constexpr uint8_t kOpenInvDoor = 0x03;  // eqemu OPEN_INVDOOR / CLOSE_DOOR
		bool is_open = door->invertState ? (action == kOpenInvDoor)
		                                 : (action == kOpenDoor);

		// T376 (BOARD B141): the pending-click set lives on the door state
		// manager, which OWNS door state -- there is no longer a second copy
		// on this component. READ IT FIRST: setDoorState() consumes the entry
		// (it erases the id on every state update), so asking afterwards
		// always answers false.
		bool user_initiated = ctx_.game_state.doors().isClickPending(door_id);
		ctx_.game_state.doors().setDoorState(door_id, is_open);

		// Log at debug level 2+ for all doors, or level 1+ for user-initiated
		if (EverQuest::GetDebugLevel() >= 2 || (user_initiated && EverQuest::GetDebugLevel() >= 1)) {
			LOG_DEBUG(MOD_ENTITY, "Door {} {}{}", door_id,
			          is_open ? "opened" : "closed",
			          user_initiated ? " (user)" : "");
		}

#ifdef WITH_AUDIO
		// Play door sound only if player is close to the door
		if (ctx_.audio_manager) {
			glm::vec3 doorPos(door->x, door->y, door->z);
			glm::vec3 playerPos(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
			float distance = glm::distance(doorPos, playerPos);

			// Only play door sounds within 10 units
			constexpr float DOOR_SOUND_MAX_DISTANCE = 10.0f;
			if (distance <= DOOR_SOUND_MAX_DISTANCE) {
				EQT::Audio::DoorType doorType = getDoorTypeFromName(door->name);
				uint32_t soundId = EQT::Audio::DoorSounds::getDoorSound(doorType, is_open);
				ctx_.audio_manager->playSound(soundId, doorPos);
			}
		}
#endif

		// Publish DoorStateChanged event to bridge
		if (ctx_.bridge) {
			eqt::state::DoorStateChangedData data;
			data.doorId = door_id;
			data.isOpen = is_open;
			data.userInitiated = user_initiated;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::DoorStateChanged, std::move(data)));
		}
	} else {
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_ENTITY, "MoveDoor for unknown door {}", door_id);
		}
	}
}

void eqt::entities::EntitiesComponent::SendSpawnAppearance(uint16_t type, uint32_t value)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(10);  // SpawnAppearance packet size: 2 bytes opcode + 8 bytes struct
	
	p.PutUInt16(0, HC_OP_SpawnAppearance);
	p.PutUInt16(2, ctx_.self.MySpawnId());  // spawn_id
	p.PutUInt16(4, type);            // type
	p.PutUInt32(6, value);           // parameter
	// Removed the extra 4 bytes that were at offset 10

	// T360 (BOARD B068): SafeQueueZone, not zone_connection->QueuePacket.
	// This sender used to bypass PacketSender entirely and feed the T268
	// ordered-capture seam by hand (the deleted Test_RecordQueuedPacket call
	// T346 added) -- which left the R01 SESSION RECORDER blind to it, so a
	// --record-session .sleq was a false negative for every appearance the
	// client ever sent, and the Batch R replay oracle replayed a session that
	// never contained one (T358 measured exactly that on a live self-death).
	// SafeQueueZone feeds BOTH observers from one place, and takes the same
	// off-main-thread QueuePacketAsync path SendSave does, so the death
	// burst reaches the server in the order T349's oracle states.
	ctx_.net.SafeQueueZone(p);
	
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent SpawnAppearance: spawn_id={}, type={}, value={}", 
			ctx_.self.MySpawnId(), type, value);
	}
}

void eqt::entities::EntitiesComponent::SendAnimation(uint8_t animation_id, uint8_t animation_speed)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(6);  // Animation packet size: 2 bytes opcode + 4 bytes data
	
	p.PutUInt16(0, HC_OP_Animation);      // opcode (OP_Animation)
	p.PutUInt16(2, ctx_.self.MySpawnId());        // spawn_id
	p.PutUInt8(4, animation_speed);       // animation speed (usually 10)
	p.PutUInt8(5, animation_id);          // animation ID
	
	// T360 (BOARD B068): same bypass, same fix -- through PacketSender so the
	// session recorder and the T268 seam both see it. Still unreliable.
	ctx_.net.SafeQueueZone(p, 0, false);
	
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent Animation: spawn_id={}, speed={}, animation_id={}",
			ctx_.self.MySpawnId(), animation_speed, animation_id);
	}
}

void eqt::entities::EntitiesComponent::SendClickDoor(uint8_t door_id, uint32_t item_id)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click door - not fully zoned in");
		return;
	}

	// Check if door exists
	const EQT::DoorState* door = ctx_.game_state.doors().getDoor(door_id);
	if (!door) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown door {}", door_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking door {} ('{}')", door_id, door->name);

	// Track this as a user-initiated door click. T376 (BOARD B141): on the
	// door state manager, the one owner of door state -- it erases the id on
	// the confirming OP_MoveDoor and clears the whole set on clear(), which
	// is the zone-change cleanup ZoneComponent::CleanupZone already calls.
	ctx_.game_state.doors().addPendingClick(door_id);

	// Build ClickDoor packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickDoor_Struct));

	p.PutUInt16(0, HC_OP_ClickDoor);
	p.PutUInt8(2, door_id);           // doorid
	p.PutUInt8(3, 0);                 // unknown001
	p.PutUInt8(4, 0);                 // unknown002
	p.PutUInt8(5, 0);                 // unknown003
	p.PutUInt8(6, 0);                 // picklockskill
	p.PutUInt8(7, 0);                 // unknown005[0]
	p.PutUInt8(8, 0);                 // unknown005[1]
	p.PutUInt8(9, 0);                 // unknown005[2]
	p.PutUInt32(10, item_id);         // item_id (key)
	p.PutUInt16(14, ctx_.self.MySpawnId());   // player_id
	p.PutUInt16(16, 0);               // unknown014

	// T376 (BOARD B140): through PacketSender, not a raw
	// ctx_.zone_connection-> deref. SafeQueueZone guards the null connection
	// (this line segfaulted without one -- measured exit 139 in T374), feeds
	// the T268 ordered-capture seam BEFORE its gates, and feeds the R01
	// session recorder after them, so a --record-session .sleq finally
	// contains the door clicks the client sent. Same fix T360 made for
	// SendSpawnAppearance/SendAnimation (B068).
	ctx_.net.SafeQueueZone(p, 0, true);
}

// ============================================================================
// World Object / Tradeskill Methods
// ============================================================================

void eqt::entities::EntitiesComponent::SendClickObject(uint32_t drop_id)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click object - not fully zoned in");
		return;
	}

	// Check if object exists
	auto it = m_world_objects.find(drop_id);
	if (it == m_world_objects.end()) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown object {}", drop_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking object {} ('{}') type={} ({})",
		drop_id, it->second.name, it->second.object_type,
		it->second.getTradeskillName());

	// Build ClickObject packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObject_Struct));

	p.PutUInt16(0, HC_OP_ClickObject);
	p.PutUInt32(2, drop_id);           // drop_id
	p.PutUInt32(6, ctx_.self.MySpawnId());     // player_id

	// T376 (BOARD B140): see SendClickDoor above -- same bypass, same fix.
	ctx_.net.SafeQueueZone(p, 0, true);
}

void eqt::entities::EntitiesComponent::SendCloseContainer(uint32_t drop_id)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot close container - not fully zoned in");
		return;
	}

	LOG_DEBUG(MOD_ENTITY, "Closing tradeskill container {}", drop_id);

	// Build ClickObjectAction packet to close the container
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObjectAction_Struct));

	p.PutUInt16(0, HC_OP_ClickObjectAction);
	p.PutUInt32(2, ctx_.self.MySpawnId());     // player_id
	p.PutUInt32(6, drop_id);           // drop_id
	p.PutUInt32(10, 0);                // open = 0 (closing)
	p.PutUInt32(14, 0);                // type
	p.PutUInt32(18, 0);                // unknown16
	p.PutUInt32(22, 0);                // icon
	p.PutUInt32(26, 0);                // unknown24
	// object_name is 64 bytes of zeros (not needed for close)
	for (int i = 0; i < 64; i++) {
		p.PutUInt8(30 + i, 0);
	}

	// T376: the third and last direct sender in this TU. B140 named only the
	// two click senders; this one is the same shape in the same family, and
	// leaving it behind would have left the file half-routed.
	ctx_.net.SafeQueueZone(p, 0, true);

	// Clear active tradeskill object. T278: eqemu does NOT echo
	// ClickObjectAction open=0 (Object::Close() answers with OP_ClearObject
	// instead), so this local clear is the only one there will be -- T282
	// keeps the inventory model's copy in step with it here rather than
	// waiting for a confirmation that never comes.
	ctx_.active_tradeskill_object_id = 0;
	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->setWorldContainerOpen(false);
	}
}

const eqt::WorldObject* eqt::entities::EntitiesComponent::GetWorldObject(uint32_t drop_id) const
{
	auto it = m_world_objects.find(drop_id);
	if (it != m_world_objects.end()) {
		return &it->second;
	}
	return nullptr;
}

void eqt::entities::EntitiesComponent::ClearWorldObjects()
{
	LOG_DEBUG(MOD_ENTITY, "Clearing {} world objects", m_world_objects.size());
	m_world_objects.clear();
	ctx_.active_tradeskill_object_id = 0;
	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->setWorldContainerOpen(false);
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessClickObjectAction(const EQ::Net::Packet &p)
{
	// Parse ClickObjectAction_Struct from packet (skip 2-byte opcode)
	if (p.Length() < 2 + sizeof(EQT::ClickObjectAction_Struct)) {
		LOG_WARN(MOD_ENTITY, "ClickObjectAction packet too small: {} bytes (need {})",
			p.Length(), 2 + sizeof(EQT::ClickObjectAction_Struct));
		return;
	}

	const auto* action = reinterpret_cast<const EQT::ClickObjectAction_Struct*>(static_cast<const uint8_t*>(p.Data()) + 2);

	std::string objectName(action->object_name, strnlen(action->object_name, sizeof(action->object_name)));

	if (action->open == 1) {
		// Server is telling us to open a tradeskill container
		LOG_INFO(MOD_ENTITY, "Opening tradeskill container: drop_id={} type={} icon={} name='{}'",
			action->drop_id, action->type, action->icon, objectName);

		// Store the active tradeskill object ID
		ctx_.active_tradeskill_object_id = action->drop_id;
		// T282 -- the inventory model gates moves into/out of 4000-4009 on
		// this, because eqemu's SwapItem Step 3 is gated on the same thing
		// server-side and destroys the item when it is not.
		if (ctx_.inventory_manager) {
			ctx_.inventory_manager->setWorldContainerOpen(true);
		}

		// T328 Leg B: the official Titanium client plays itemclth.WAV (145)
		// when a container opens -- measured for an inventory bag AND for a
		// tradeskill container, which are the same sound
		// (review/T328/README.md rows 1 and 4). Chest_Op (134) is NOT it,
		// despite UISoundId::CHEST_OPEN's name. Played here, on the SERVER's
		// open confirmation, because that is the only point fable knows a
		// container opened -- fable has no client-side bag window to open.
#ifdef WITH_AUDIO
		if (ctx_.bridge) {
			ctx_.bridge->pushIntent(eqt::events::PlaySoundIntent{
				EQT::Audio::UISoundId::ITEM_CLOTH});
		}
#endif

		// Open tradeskill container via bridge
		if (ctx_.bridge) {
			eqt::state::TradeskillContainerOpenedEvent data;
			data.isWorldObject = true;
			data.objectId = action->drop_id;
			data.inventorySlot = -1;
			data.containerName = objectName;
			data.containerType = static_cast<uint8_t>(action->type);
			data.slotCount = eqt::inventory::WORLD_COUNT;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeskillContainerOpened, std::move(data)));
		}
	} else {
		// Server is confirming container is closed
		LOG_DEBUG(MOD_ENTITY, "Tradeskill container closed: drop_id={}", action->drop_id);
		ctx_.active_tradeskill_object_id = 0;
		if (ctx_.inventory_manager) {
			ctx_.inventory_manager->setWorldContainerOpen(false);
		}

		// T328 Leg B: close plays the SAME sound as open -- measured three
		// ways (the container window's "Done" button, its "X" button, and a
		// tradeskill container close; review/T328/README.md rows 2, 3, 5).
#ifdef WITH_AUDIO
		if (ctx_.bridge) {
			ctx_.bridge->pushIntent(eqt::events::PlaySoundIntent{
				EQT::Audio::UISoundId::ITEM_CLOTH});
		}
#endif

		// Close tradeskill container via bridge
		if (ctx_.bridge) {
			eqt::state::TradeskillContainerClosedEvent data;
			data.wasWorldObject = true;
			data.objectId = action->drop_id;
			data.inventorySlot = -1;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeskillContainerClosed, std::move(data)));
		}
	}
}

