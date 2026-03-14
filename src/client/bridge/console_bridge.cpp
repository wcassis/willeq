/*
 * ConsoleBridge — logs game events to console for headless mode.
 * D25: Only handles events useful for headless monitoring.
 */

#include "client/bridge/console_bridge.h"
#include "common/logging.h"

namespace eqt {
namespace bridge {

void ConsoleBridge::applyEvent(const state::GameEvent& event) {
    switch (event.type) {

    // Entity lifecycle
    case state::GameEventType::EntitySpawned: {
        auto& d = std::get<state::EntitySpawnedData>(event.data);
        const char* type = d.npcType == 0 ? "PC" : d.npcType == 1 ? "NPC" : "Corpse";
        LOG_INFO(MOD_ENTITY, "[SPAWN] {} '{}' (id={}) at ({:.0f}, {:.0f}, {:.0f}) level={}",
            type, d.name, d.spawnId, d.x, d.y, d.z, d.level);
        break;
    }
    case state::GameEventType::EntityDespawned: {
        auto& d = std::get<state::EntityDespawnedData>(event.data);
        LOG_INFO(MOD_ENTITY, "[DESPAWN] '{}' (id={})", d.name, d.spawnId);
        break;
    }

    // Chat
    case state::GameEventType::ChatMessage: {
        auto& d = std::get<state::ChatMessageData>(event.data);
        if (!d.sender.empty()) {
            LOG_INFO(MOD_MAIN, "[CHAT] {}: {}", d.sender, d.message);
        } else {
            LOG_INFO(MOD_MAIN, "[CHAT] {}", d.message);
        }
        break;
    }
    case state::GameEventType::SystemMessage: {
        // SystemMessage uses ChatMessageData
        auto& d = std::get<state::ChatMessageData>(event.data);
        LOG_INFO(MOD_MAIN, "[SYS] {}", d.message);
        break;
    }

    // Player stats
    case state::GameEventType::PlayerStatsChanged: {
        auto& d = std::get<state::PlayerStatsChangedData>(event.data);
        LOG_INFO(MOD_MAIN, "[STATS] HP={}/{} Mana={}/{} End={}/{}",
            d.curHP, d.maxHP, d.curMana, d.maxMana, d.curEndurance, d.maxEndurance);
        break;
    }

    // Combat
    case state::GameEventType::CombatEvent: {
        auto& d = std::get<state::CombatEventData>(event.data);
        switch (d.type) {
            case state::CombatEventData::Type::Hit:
                LOG_INFO(MOD_COMBAT, "[COMBAT] {} hit {} for {} damage",
                    d.sourceName, d.targetName, d.damage);
                break;
            case state::CombatEventData::Type::Miss:
                LOG_INFO(MOD_COMBAT, "[COMBAT] {} missed {}", d.sourceName, d.targetName);
                break;
            case state::CombatEventData::Type::Death:
                LOG_INFO(MOD_COMBAT, "[COMBAT] {} killed {}", d.sourceName, d.targetName);
                break;
            default:
                break;
        }
        break;
    }

    // Target
    case state::GameEventType::TargetChanged: {
        auto& d = std::get<state::TargetChangedData>(event.data);
        if (d.spawnId == 0) {
            LOG_INFO(MOD_MAIN, "[TARGET] Cleared");
        } else {
            LOG_INFO(MOD_MAIN, "[TARGET] {} (id={}) level={} HP={}%%",
                d.name, d.spawnId, d.level, d.hpPercent);
        }
        break;
    }

    // Zone
    case state::GameEventType::ZoneChanged: {
        auto& d = std::get<state::ZoneChangedData>(event.data);
        LOG_INFO(MOD_ZONE, "[ZONE] Entering {} at ({:.0f}, {:.0f}, {:.0f})",
            d.zoneName, d.x, d.y, d.z);
        break;
    }
    case state::GameEventType::ZoneLoaded: {
        auto& d = std::get<state::ZoneLoadedData>(event.data);
        LOG_INFO(MOD_ZONE, "[ZONE] {} loaded", d.zoneName);
        break;
    }

    // Group
    case state::GameEventType::GroupChanged: {
        auto& d = std::get<state::GroupChangedData>(event.data);
        if (d.inGroup) {
            LOG_INFO(MOD_MAIN, "[GROUP] In group ({} members), leader: {}",
                d.memberCount, d.leaderName);
        } else {
            LOG_INFO(MOD_MAIN, "[GROUP] Not in group");
        }
        break;
    }

    // Everything else — silently ignored
    default:
        break;
    }
}

} // namespace bridge
} // namespace eqt
