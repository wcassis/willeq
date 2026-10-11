#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

#include "client/corpse_predicate.h"  // T456 (B478): the ONE corpse predicate

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

/**
 * Entity - Represents an entity in the game world (NPC, player, corpse).
 *
 * This struct matches the Entity struct in eq.h but is defined here to
 * decouple the state layer from the EverQuest class.
 */
struct Entity {
    uint16_t spawnId = 0;
    std::string name;
    float x = 0, y = 0, z = 0;
    float heading = 0;
    uint8_t level = 0;
    uint8_t classId = 0;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    uint32_t guildId = 0;
    uint8_t animation = 0;
    uint8_t hpPercent = 100;
    uint16_t curMana = 0;
    uint16_t maxMana = 0;
    float size = 0;
    bool isCorpse = false;

    // Appearance data
    uint8_t face = 0;
    uint8_t haircolor = 0;
    uint8_t hairstyle = 0;
    uint8_t beardcolor = 0;
    uint8_t beard = 0;
    uint8_t equipChest2 = 0;
    uint8_t helm = 0;
    uint8_t showhelm = 0;
    uint8_t bodytype = 0;
    uint8_t npcType = 0;  // 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
    uint8_t light = 0;

    // Equipment textures (9 slots)
    uint32_t equipment[9] = {0};
    uint32_t equipmentTint[9] = {0};

    // Movement tracking
    float deltaX = 0, deltaY = 0, deltaZ = 0;
    float deltaHeading = 0;
    uint32_t lastUpdateTime = 0;

    // Pet tracking
    uint8_t isPet = 0;           // Non-zero if this entity is a pet
    uint32_t petOwnerId = 0;     // Spawn ID of pet's owner

    // Weapon skill types for combat animations (255 = unknown/none)
    uint8_t primaryWeaponSkill = 255;
    uint8_t secondaryWeaponSkill = 255;

    // Helper methods
    bool isPlayer() const { return npcType == 0; }
    bool isNPC() const { return npcType == 1; }
    bool isPlayerCorpse() const { return npcType == 2; }
    bool isNPCCorpse() const { return npcType == 3; }
    // T456 (board B478): delegates to THE predicate rather than being a fifth
    // copy of it. The form that was here had no name test at all, so an entity
    // named only by OP_MobRename -- whose npc_type this client never saw --
    // came back "not a corpse" here and "a corpse" from IsCorpseByFields()
    // everywhere else. corpse_predicate.h's own note says do not write a
    // second corpse test; there were five.
    bool isAnyCorpse() const { return IsCorpseByFields(isCorpse, npcType, name); }

    glm::vec3 position() const { return glm::vec3(x, y, z); }
};

/**
 * EntityManager - Manages all entities in the game world.
 *
 * This class handles entity tracking including spawning, despawning,
 * movement updates, and queries. It fires events through the EventBus
 * when entities are added, removed, or updated.
 */
class EntityManager {
public:
    EntityManager();
    ~EntityManager() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Entity Management ==========

    /**
     * Add a new entity. Fires EntitySpawned event.
     * @param entity The entity to add
     * @return true if added, false if spawn ID already exists
     */
    bool addEntity(const Entity& entity);

    /**
     * Remove an entity by spawn ID. Fires EntityDespawned event.
     * @param spawnId The spawn ID of the entity to remove
     * @return true if removed, false if not found
     */
    bool removeEntity(uint16_t spawnId);

    /**
     * Update an entity's position and animation. Fires EntityMoved event.
     * @param spawnId The spawn ID of the entity
     * @param x New X position
     * @param y New Y position
     * @param z New Z position
     * @param heading New heading
     * @param dx X velocity
     * @param dy Y velocity
     * @param dz Z velocity
     * @param animation Animation ID
     * @return true if updated, false if entity not found
     */
    bool updateEntityPosition(uint16_t spawnId, float x, float y, float z, float heading,
                              float dx = 0, float dy = 0, float dz = 0, uint8_t animation = 0);

    /**
     * Clear all entities.
     */
    void clear();

    // ========== Entity Queries ==========

    /**
     * Get an entity by spawn ID.
     * @param spawnId The spawn ID to look up
     * @return Pointer to entity or nullptr if not found
     */
    const Entity* getEntity(uint16_t spawnId) const;

    /**
     * Get a mutable entity by spawn ID.
     * @param spawnId The spawn ID to look up
     * @return Pointer to entity or nullptr if not found
     */
    Entity* getEntityMutable(uint16_t spawnId);

    /**
     * Find an entity by name (case-insensitive partial match).
     * @param name Name to search for
     * @return Pointer to entity or nullptr if not found
     */
    const Entity* findEntityByName(const std::string& name) const;

    /**
     * T244: same ranked resolver, with distance from (px,py,pz) -- the
     * player's position -- as the secondary key. Living-over-corpse and
     * exact-over-substring (T173) stay primary; among equal-tier matches
     * the NEAREST wins instead of the lowest spawn id. /target uses this.
     */
    const Entity* findEntityByName(const std::string& name,
                                   float px, float py, float pz) const;

    /**
     * Get all entities.
     * @return Reference to the entity map
     */
    const std::map<uint16_t, Entity>& getAllEntities() const { return m_entities; }

    /**
     * Get the number of entities.
     * @return Entity count
     */
    size_t getEntityCount() const { return m_entities.size(); }

    /**
     * Check if an entity exists.
     * @param spawnId The spawn ID to check
     * @return true if entity exists
     */
    bool hasEntity(uint16_t spawnId) const;

    // ========== Spatial Queries ==========

    /**
     * Get the nearest entity matching a filter.
     * @param x Center X position
     * @param y Center Y position
     * @param z Center Z position
     * @param filter Function returning true for entities to consider
     * @return Pointer to nearest matching entity or nullptr
     */
    const Entity* getNearestEntity(float x, float y, float z,
                                   std::function<bool(const Entity&)> filter = nullptr) const;

    /**
     * Get the nearest NPC.
     * @param x Center X position
     * @param y Center Y position
     * @param z Center Z position
     * @return Pointer to nearest NPC or nullptr
     */
    const Entity* getNearestNPC(float x, float y, float z) const;

    /**
     * Get the nearest player.
     * @param x Center X position
     * @param y Center Y position
     * @param z Center Z position
     * @param excludeSpawnId Spawn ID to exclude (e.g., self)
     * @return Pointer to nearest player or nullptr
     */
    const Entity* getNearestPlayer(float x, float y, float z, uint16_t excludeSpawnId = 0) const;

    // ========== Pet Queries ==========

    /**
     * Check if an entity is a pet.
     * @param spawnId The spawn ID to check
     * @return true if entity is a pet
     */
    bool isPet(uint16_t spawnId) const;

private:
    // T244: shared body of the two findEntityByName overloads; pos is null
    // for the id-ordered form, else {x,y,z} of the reference point.
    const Entity* findEntityByNameRanked(const std::string& name, const float* pos) const;

    void fireEntitySpawnedEvent(const Entity& entity);
    void fireEntityDespawnedEvent(uint16_t spawnId, const std::string& name);
    void fireEntityMovedEvent(const Entity& entity);

    float calculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const;

    EventBus* m_eventBus = nullptr;
    std::map<uint16_t, Entity> m_entities;
};

} // namespace state
} // namespace eqt
