#include "client/state/entity_manager.h"
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

const Entity* EntityManager::findEntityByName(const std::string& name) const {
    if (name.empty()) {
        return nullptr;
    }

    // Convert search name to lowercase
    std::string searchLower = name;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& pair : m_entities) {
        std::string entityNameLower = pair.second.name;
        std::transform(entityNameLower.begin(), entityNameLower.end(), entityNameLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (entityNameLower.find(searchLower) != std::string::npos) {
            return &pair.second;
        }
    }
    return nullptr;
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
