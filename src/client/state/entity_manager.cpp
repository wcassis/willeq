#include "client/state/entity_manager.h"
#include "client/corpse_predicate.h"
#include "client/state/event_bus.h"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace eqt {
namespace state {

EntityManager::EntityManager() = default;

// ========== Entity Management ==========

bool EntityManager::addEntity(const Entity& entity) {
    if (m_entities.find(entity.spawnId) != m_entities.end()) {
        return false;  // Already exists
    }
    m_entities[entity.spawnId] = entity;
    fireEntitySpawnedEvent(entity);
    return true;
}

bool EntityManager::removeEntity(uint16_t spawnId) {
    auto it = m_entities.find(spawnId);
    if (it == m_entities.end()) {
        return false;
    }
    std::string name = it->second.name;
    m_entities.erase(it);
    fireEntityDespawnedEvent(spawnId, name);
    return true;
}

bool EntityManager::updateEntityPosition(uint16_t spawnId, float x, float y, float z, float heading,
                                         float dx, float dy, float dz, uint8_t animation) {
    auto it = m_entities.find(spawnId);
    if (it == m_entities.end()) {
        return false;
    }

    Entity& entity = it->second;
    entity.x = x;
    entity.y = y;
    entity.z = z;
    entity.heading = heading;
    entity.deltaX = dx;
    entity.deltaY = dy;
    entity.deltaZ = dz;
    entity.animation = animation;

    fireEntityMovedEvent(entity);
    return true;
}

bool EntityManager::updateEntityHP(uint16_t spawnId, uint8_t hpPercent) {
    auto it = m_entities.find(spawnId);
    if (it == m_entities.end()) {
        return false;
    }

    Entity& entity = it->second;
    if (entity.hpPercent != hpPercent) {
        entity.hpPercent = hpPercent;
        fireEntityStatsChangedEvent(entity);
    }
    return true;
}

bool EntityManager::updateEntityEquipment(uint16_t spawnId, int slot, uint32_t materialId, uint32_t tint) {
    if (slot < 0 || slot >= 9) {
        return false;
    }

    auto it = m_entities.find(spawnId);
    if (it == m_entities.end()) {
        return false;
    }

    Entity& entity = it->second;
    entity.equipment[slot] = materialId;
    entity.equipmentTint[slot] = tint;
    // Could fire an EntityAppearanceChanged event here
    return true;
}

void EntityManager::markAsCorpse(uint16_t spawnId) {
    auto it = m_entities.find(spawnId);
    if (it != m_entities.end()) {
        it->second.isCorpse = true;
    }
}

void EntityManager::clear() {
    // Fire despawn events for all entities
    for (const auto& pair : m_entities) {
        fireEntityDespawnedEvent(pair.first, pair.second.name);
    }
    m_entities.clear();
}

// ========== Entity Queries ==========

const Entity* EntityManager::getEntity(uint16_t spawnId) const {
    auto it = m_entities.find(spawnId);
    if (it == m_entities.end()) {
        return nullptr;
    }
    return &it->second;
}

Entity* EntityManager::getEntityMutable(uint16_t spawnId) {
    auto it = m_entities.find(spawnId);
    if (it == m_entities.end()) {
        return nullptr;
    }
    return &it->second;
}

// T173: rank the candidates instead of taking the first substring hit.
//
// Every corpse of X is named "X's corpse<id>", so every corpse of X CONTAINS
// X. The old loop returned the first entity whose lowercased name contained
// the search string, in map order -- so `/target Testmelee` handed back
// `Testmelee's corpse237` whenever the corpse happened to sit at a lower spawn
// id than the living player standing next to it. That is not theoretical: it
// cost three probes in one session (T172), and T161 lost a `#kill` to the same
// shape a day earlier.
//
// Four tiers, lowest wins; a corpse always loses to a non-corpse at the same
// quality of match, and an exact match always beats a substring:
//
//   0  exact name, not a corpse   -- "/target Testmelee", Testmelee alive
//   1  exact name, a corpse       -- "/target Testmelee's corpse237"
//   2  substring, not a corpse    -- "/target Testme"
//   3  substring, a corpse        -- the only tier the old code had
//
// Tier 1 above tier 2 is deliberate: naming a corpse exactly must still reach
// it, or /consider and /loot lose their subject.
//
// m_entities is a std::map keyed by spawn id, so iteration is ascending and
// "first seen in a tier" means "lowest spawn id in that tier" -- the result
// does not depend on hash order and does not move between runs.
//
// T244: the tier is the PRIMARY key; when a reference position is supplied
// (the /target form), squared distance from it is the SECONDARY key, so two
// living substring matches resolve to the nearer one rather than the one
// that happened to spawn first (T240 run 1: `/target excavator` picked an
// out-of-range roamer over the one at the caster's feet). Ties -- same
// tier, same distance, or no position given -- still fall to the lowest
// spawn id, so the no-position overload is byte-for-byte T173's behaviour.
const Entity* EntityManager::findEntityByName(const std::string& name) const {
    return findEntityByNameRanked(name, nullptr);
}

const Entity* EntityManager::findEntityByName(const std::string& name,
                                              float px, float py, float pz) const {
    const float pos[3] = {px, py, pz};
    return findEntityByNameRanked(name, pos);
}

const Entity* EntityManager::findEntityByNameRanked(const std::string& name,
                                                    const float* pos) const {
    if (name.empty()) {
        return nullptr;
    }

    std::string searchLower = name;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const Entity* best = nullptr;
    int bestTier = 4;
    float bestDistSq = 0.0f;

    for (const auto& pair : m_entities) {
        const Entity& entity = pair.second;

        std::string entityNameLower = entity.name;
        std::transform(entityNameLower.begin(), entityNameLower.end(), entityNameLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        int tier;
        if (entityNameLower == searchLower) {
            tier = 0;
        } else if (entityNameLower.find(searchLower) != std::string::npos) {
            tier = 2;
        } else {
            continue;
        }
        // One shared predicate, never a local re-derivation -- see
        // corpse_predicate.h.
        if (IsCorpseByFields(entity.isCorpse, entity.npcType, entity.name)) {
            tier += 1;
        }

        float distSq = 0.0f;
        if (pos) {
            const float dx = entity.x - pos[0];
            const float dy = entity.y - pos[1];
            const float dz = entity.z - pos[2];
            distSq = dx * dx + dy * dy + dz * dz;
        }

        if (tier < bestTier || (pos && tier == bestTier && distSq < bestDistSq)) {
            bestTier = tier;
            bestDistSq = distSq;
            best = &entity;
            if (tier == 0 && !pos) {
                break;  // nothing can beat an exact living match
            }
        }
    }

    return best;
}

const Entity* EntityManager::findEntityByExactName(const std::string& name) const {
    for (const auto& pair : m_entities) {
        if (pair.second.name == name) {
            return &pair.second;
        }
    }
    return nullptr;
}

bool EntityManager::hasEntity(uint16_t spawnId) const {
    return m_entities.find(spawnId) != m_entities.end();
}

// ========== Spatial Queries ==========

std::vector<uint16_t> EntityManager::getEntitiesInRange(float x, float y, float z, float range) const {
    std::vector<uint16_t> result;
    float rangeSq = range * range;

    for (const auto& pair : m_entities) {
        const Entity& entity = pair.second;
        float dx = entity.x - x;
        float dy = entity.y - y;
        float dz = entity.z - z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= rangeSq) {
            result.push_back(pair.first);
        }
    }
    return result;
}

const Entity* EntityManager::getNearestEntity(float x, float y, float z,
                                              std::function<bool(const Entity&)> filter) const {
    const Entity* nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (const auto& pair : m_entities) {
        const Entity& entity = pair.second;

        if (filter && !filter(entity)) {
            continue;
        }

        float dx = entity.x - x;
        float dy = entity.y - y;
        float dz = entity.z - z;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = &entity;
        }
    }
    return nearest;
}

const Entity* EntityManager::getNearestNPC(float x, float y, float z) const {
    return getNearestEntity(x, y, z, [](const Entity& e) {
        return e.isNPC() && !e.isAnyCorpse();
    });
}

const Entity* EntityManager::getNearestPlayer(float x, float y, float z, uint16_t excludeSpawnId) const {
    return getNearestEntity(x, y, z, [excludeSpawnId](const Entity& e) {
        return e.isPlayer() && !e.isAnyCorpse() && e.spawnId != excludeSpawnId;
    });
}

const Entity* EntityManager::getNearestCorpse(float x, float y, float z) const {
    return getNearestEntity(x, y, z, [](const Entity& e) {
        return e.isAnyCorpse();
    });
}

// ========== Pet Queries ==========

bool EntityManager::isPet(uint16_t spawnId) const {
    const Entity* entity = getEntity(spawnId);
    return entity && entity->isPet != 0;
}

uint32_t EntityManager::getPetOwnerId(uint16_t spawnId) const {
    const Entity* entity = getEntity(spawnId);
    if (entity && entity->isPet != 0) {
        return entity->petOwnerId;
    }
    return 0;
}

uint16_t EntityManager::findPetByOwner(uint16_t ownerSpawnId) const {
    for (const auto& pair : m_entities) {
        if (pair.second.isPet != 0 && pair.second.petOwnerId == ownerSpawnId) {
            return pair.first;
        }
    }
    return 0;
}

std::vector<uint16_t> EntityManager::getAllPets() const {
    std::vector<uint16_t> result;
    for (const auto& pair : m_entities) {
        if (pair.second.isPet != 0) {
            result.push_back(pair.first);
        }
    }
    return result;
}

// ========== Iteration ==========

void EntityManager::forEachEntity(std::function<void(const Entity&)> callback) const {
    for (const auto& pair : m_entities) {
        callback(pair.second);
    }
}

void EntityManager::forEachEntityMutable(std::function<void(Entity&)> callback) {
    for (auto& pair : m_entities) {
        callback(pair.second);
    }
}

// ========== Event Firing ==========

void EntityManager::fireEntitySpawnedEvent(const Entity& entity) {
    if (m_eventBus) {
        EntitySpawnedData data;
        data.spawnId = entity.spawnId;
        data.name = entity.name;
        data.x = entity.x;
        data.y = entity.y;
        data.z = entity.z;
        data.heading = entity.heading;
        data.raceId = entity.raceId;
        data.classId = entity.classId;
        data.level = entity.level;
        data.gender = entity.gender;
        data.npcType = entity.npcType;
        data.isCorpse = entity.isCorpse;
        m_eventBus->publish(GameEventType::EntitySpawned, data);
    }
}

void EntityManager::fireEntityDespawnedEvent(uint16_t spawnId, const std::string& name) {
    if (m_eventBus) {
        EntityDespawnedData data;
        data.spawnId = spawnId;
        data.name = name;
        m_eventBus->publish(GameEventType::EntityDespawned, data);
    }
}

void EntityManager::fireEntityMovedEvent(const Entity& entity) {
    if (m_eventBus) {
        EntityMovedData data;
        data.spawnId = entity.spawnId;
        data.x = entity.x;
        data.y = entity.y;
        data.z = entity.z;
        data.heading = entity.heading;
        data.dx = entity.deltaX;
        data.dy = entity.deltaY;
        data.dz = entity.deltaZ;
        data.animation = entity.animation;
        m_eventBus->publish(GameEventType::EntityMoved, data);
    }
}

void EntityManager::fireEntityStatsChangedEvent(const Entity& entity) {
    if (m_eventBus) {
        EntityStatsChangedData data;
        data.spawnId = entity.spawnId;
        data.hpPercent = entity.hpPercent;
        data.curMana = entity.curMana;
        data.maxMana = entity.maxMana;
        m_eventBus->publish(GameEventType::EntityStatsChanged, data);
    }
}

float EntityManager::calculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace state
} // namespace eqt
