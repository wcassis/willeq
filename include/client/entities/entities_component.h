#pragma once

// Batch K (refactor/K_component_extraction.md), unit K06.
//
// The entities family, lifted out of class EverQuest, following K01's
// eqt::guild::GuildComponent shape. Every body in
// src/client/entities/entities_component*.cpp is the body that used to live
// in one of the four src/client/eq_entities*.cpp files, moved verbatim; the
// only edits inside a body are the substitutions §Mechanism 4 allows (m_x ->
// ctx_.x / owner_.m_x, Foo() -> owner_.Foo() for a method that stayed on
// EverQuest).
//
// m_entities (the spawn-id -> Entity map itself) STAYS in EverQuest as a
// core member, reached here as ctx_.entities: 11 TU families read it, well
// past the batch's "core" threshold. This component owns the HANDLERS that
// populate and mutate that map -- zone-spawn/appearance/health/wear/illusion
// packet processing, door and world-object interaction, and the entity
// lookup helpers -- not the map storage itself.
//
// T341: the `owner_` TRANSITION handle and the
// `friend class eqt::entities::EntitiesComponent` line are BOTH GONE.
// m_world_objects came here at K13; m_guild_id went to
// eqt::guild::GuildComponent at T341 (reached as ctx_.Guild().SetGuildID());
// the loading-phase and update-loop reaches became ctx_.Bridge() and
// ctx_.lifecycle. Both axes of the K13 (ii) friend test are empty.

#include "client/client_context.h"
// K13: m_world_objects is a std::map BY VALUE -- WorldObject must be complete.
#include "client/world_object.h"

#include <cstdint>
#include <map>
#include <string>

class EverQuest;
struct Entity;

namespace eqt {
struct WorldObject;
namespace state {
struct EntityAppearanceChangedData;
}
}

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace entities {

// T448 (board B464): the SpawnDoor debug dump line, extracted from
// ZoneProcessSpawnDoor() so both of its indices can be asserted.
// `record` points at ONE door's own 80-byte Door_Struct inside the SpawnDoor
// packet -- not at the packet base -- and the eight bytes it prints are that
// record's offsets 60..67: doorId, opentype, state_at_spawn, invert_state and
// the four bytes of door_param.
std::string FormatDoorSpawnDump(uint32_t loopIndex, const uint8_t* record);

class EntitiesComponent {
public:
	// T341: the EverQuest& transition handle is GONE -- this family reaches
	// zero EverQuest private members and names no EverQuest-scoped private
	// name (the two-axis rule, K13 correction (ii) / B039). It is the
	// second component to pass both axes, after eqt::gm::GmComponent.
	explicit EntitiesComponent(ClientContext& ctx) : ctx_(ctx) {}

	// eq_entities.cpp: zone spawns, time of day, spawn doors, zonepoints,
	// spawn appearance.
	void ZoneProcessZoneSpawns(const EQ::Net::Packet &p);
	void ZoneProcessTimeOfDay(const EQ::Net::Packet &p);
	void ZoneProcessSpawnDoor(const EQ::Net::Packet &p);
	void ZoneProcessSendZonepoints(const EQ::Net::Packet &p);
	void ZoneProcessSpawnAppearance(const EQ::Net::Packet &p);

	// eq_entities_appearance.cpp: teardown/health/appearance cluster.
	void ZoneProcessDeleteSpawn(const EQ::Net::Packet &p);
	void ZoneProcessMobHealth(const EQ::Net::Packet &p);
	void ZoneProcessHPUpdate(const EQ::Net::Packet &p);
	void ListEntities(const std::string& search = "") const;
	void ZoneProcessWearChange(const EQ::Net::Packet &p);
	void ApplyAppearanceToGameState(const eqt::state::EntityAppearanceChangedData& data);
	void UpdatePlayerAppearanceFromInventory();
	void ZoneProcessIllusion(const EQ::Net::Packet &p);

	// eq_entities_updates.cpp: ground spawns, weather, new spawns, exp
	// updates, client position updates.
	void ZoneProcessGroundSpawn(const EQ::Net::Packet &p);
	void ZoneProcessWeather(const EQ::Net::Packet &p);
	void ZoneProcessNewSpawn(const EQ::Net::Packet &p);
	void ZoneProcessExpUpdate(const EQ::Net::Packet &p);
	void ZoneProcessClientUpdate(const EQ::Net::Packet &p);

	// eq_entities_world.cpp: doors, spawn-appearance/animation/click-door
	// senders, world-object (tradeskill container) senders and handlers.
	void ZoneProcessMoveDoor(const EQ::Net::Packet &p);
	void SendSpawnAppearance(uint16_t type, uint32_t value);
	void SendAnimation(uint8_t animation_id, uint8_t animation_speed = 10);
	void SendClickDoor(uint8_t door_id, uint32_t item_id = 0);
	void SendClickObject(uint32_t drop_id);
	void SendCloseContainer(uint32_t drop_id);
	const eqt::WorldObject* GetWorldObject(uint32_t drop_id) const;
	void ClearWorldObjects();
	void ZoneProcessClickObjectAction(const EQ::Net::Packet &p);

	// K13 (Batch K): the world-object map (forges, looms, groundspawns, ...),
	// re-homed here from EverQuest. This family creates it
	// (ZoneProcessGroundSpawn), reads it (GetWorldObject/SendClickObject/
	// SendCloseContainer) and clears it (ClearWorldObjects); the only reader
	// outside it is one BridgeComponent intent arm and one action-handler
	// listing, both of which go through the accessors below.
	std::map<uint32_t, eqt::WorldObject>& WorldObjects() { return m_world_objects; }
	const std::map<uint32_t, eqt::WorldObject>& GetWorldObjects() const { return m_world_objects; }

private:
	ClientContext& ctx_;

	// K13: world object tracking (forges, looms, groundspawns, etc.). Was
	// EverQuest::m_world_objects.
	std::map<uint32_t, eqt::WorldObject> m_world_objects;
};

}  // namespace entities
}  // namespace eqt
