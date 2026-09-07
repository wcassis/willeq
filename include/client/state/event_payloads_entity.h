#pragma once

// Event payloads: player, entity, door and zone.
//
// T451: split out of the 1,269-line include/client/state/event_bus.h.
// Every declaration is a contiguous line range of that file, moved verbatim;
// "client/state/event_bus.h" still includes all of them, so no include site
// changed.

#include <functional>
#include <vector>
#include <variant>
#include <string>
#include <cstdint>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>

#include "client/hotbar/hotbar_slot_type.h"  // T156: HotbarSlotAssignedData carries the enum, not a raw uint8_t
#include "client/spell/spell_visual_vocabulary.h"  // T381: SpellCastVisual*Data carry the render vocabulary by value, not a SpellDatabase*

class HCMap;
class PathfinderNavmesh;

#include "client/state/event_types.h"

namespace eqt {
namespace state {

// --- Player events ---

struct PlayerMovedData {
    float x, y, z;
    float heading;
    float dx, dy, dz;  // Velocity
    bool isMoving;
};

struct PlayerStatsChangedData {
    uint32_t curHP, maxHP;
    uint32_t curMana, maxMana;
    uint32_t curEndurance, maxEndurance;
    uint8_t level;
};

struct PlayerPositionStateChangedData {
    uint8_t state;  // PositionState enum value
};

struct PlayerMovementModeChangedData {
    uint8_t mode;  // MovementMode enum value
};

// --- Entity events ---

struct EntitySpawnedData {
    uint16_t spawnId;
    std::string name;
    float x, y, z;
    float heading;
    uint16_t raceId;
    uint8_t classId;
    uint8_t level;
    uint8_t gender;
    uint8_t npcType;  // 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
    bool isCorpse;
    bool isPlayer = false;
    float serverSize = 0.0f;
    // Appearance
    uint8_t face = 0;
    uint8_t haircolor = 0;
    uint8_t hairstyle = 0;
    uint8_t beardcolor = 0;
    uint8_t beard = 0;
    uint8_t texture = 0;     // equip_chest2
    uint8_t helm = 0;
    uint32_t equipment[9] = {0};
    uint32_t equipmentTint[9] = {0};
};

struct EntityDespawnedData {
    uint16_t spawnId;
    std::string name;
};

struct EntityMovedData {
    uint16_t spawnId;
    float x, y, z;
    float heading;
    float dx, dy, dz;
    int32_t animation;
};

struct EntityStatsChangedData {
    uint16_t spawnId;
    uint8_t hpPercent;
    uint16_t curMana;
    uint16_t maxMana;
};

struct EntityAppearanceChangedData {
    uint16_t spawnId;
    uint16_t raceId;
    uint8_t gender;
    bool isPlayer;  // true if this is the local player (update paperdoll too)
    // Full appearance data for renderer replay
    uint8_t face = 0;
    uint8_t haircolor = 0;
    uint8_t hairstyle = 0;
    uint8_t beardcolor = 0;
    uint8_t beard = 0;
    uint8_t texture = 0;     // equip_chest2
    uint8_t helm = 0;
    uint32_t equipment[9] = {0};
    uint32_t equipmentTint[9] = {0};
};

// T318: a server-authoritative size change (shrink, grow, illusion). `size` is
// on the server's own scale -- a normal humanoid is 6.0, eqemu clamps a client
// or a pet to [3, 15] -- NOT a multiplier.
struct EntitySizeChangedData {
    uint16_t spawnId = 0;
    float size = 0.0f;
};

struct EntityLightChangedData {
    uint16_t spawnId;
    uint8_t lightLevel;
};

struct EntityAnimationEventData {
    uint16_t spawnId;
    uint8_t animCode;
    std::string animName;  // Animation string (e.g. "c01", "o02", "d02")
    bool loop;
    bool playThrough;
};

struct EntityWeaponSkillsChangedData {
    uint16_t spawnId;
    uint8_t primaryWeaponSkill;
    uint8_t secondaryWeaponSkill;
};

struct EntityPoseStateChangedData {
    uint16_t spawnId;
    uint8_t poseState;
};

struct EntityDeathAnimationData {
    uint16_t spawnId;
};

struct CorpseDecayStartedData {
    uint16_t spawnId;
};

struct CombatAnimationData {
    uint16_t sourceId;
    uint16_t targetId;
    uint8_t damageType;
    int32_t damageAmount;
    uint8_t damagePercent;
};

struct CombatSkillAnimationData {
    uint16_t spawnId;
    std::string animCode;  // e.g. EQ::ANIM_KICK, EQ::ANIM_BASH
};

struct ReceivedDamageAnimationData {
    uint16_t spawnId;
};

struct PlayerSpawnIdSetData {
    uint16_t spawnId;
};

struct NetworkReadyData {
    uint32_t expectedEntityCount;
    bool ready;
};

// --- Door events ---

struct DoorSpawnedData {
    uint8_t doorId;
    std::string name;
    float x, y, z;
    float heading;
    uint32_t incline = 0;
    uint16_t size = 0;
    uint8_t opentype = 0;
    bool isOpen = false;
    bool invertState = false;   // T219: carried so the projection holds the full DoorState
    uint32_t doorParam = 0;     // T219: lock type / key item id
};

struct DoorStateChangedData {
    uint8_t doorId;
    bool isOpen;
    bool userInitiated = false;
};

// --- Zone events ---

struct ZoneChangedData {
    std::string zoneName;
    uint16_t zoneId;
    float x, y, z;
    float heading;
};

struct ZoneLoadingData {
    std::string zoneName;
    uint16_t zoneId;
    float progress;  // 0.0 to 1.0
    std::string statusMessage;
};

struct ZoneLoadedData {
    std::string zoneName;
    uint16_t zoneId;
};

} // namespace state
} // namespace eqt
