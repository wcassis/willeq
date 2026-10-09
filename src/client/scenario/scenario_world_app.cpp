#include "client/scenario/scenario_world_app.h"

#include "client/application.h"
#include "client/eq.h"
#include "client/movement/movement_component.h"
#include "client/spell/buff_manager.h"
#include "client/state/game_state.h"
#include "common/logging.h"

namespace eqt {
namespace scenario {

// The constructor and the UIGAP-live observables live in
// scenario_world_app_ui.cpp.

bool ApplicationScenarioWorld::IsConnected() const {
    // There is no separate "connected" term on EverQuest -- the client's own
    // gate is IsFullyZonedIn() (application.cpp's isConnected is exactly
    // that). The zone name is the earliest thing the world server hands us,
    // so a non-empty name with IsFullyZonedIn() still false is precisely
    // "the handshake got somewhere and then stalled", which is the only
    // distinction the could-not-start message needs to draw.
    return !client_.GetCurrentZoneName().empty();
}

bool ApplicationScenarioWorld::IsFullyZonedIn() const {
    return client_.IsFullyZonedIn();
}

std::string ApplicationScenarioWorld::ZoneName() const {
    return client_.GetCurrentZoneName();
}

int ApplicationScenarioWorld::OtherPlayerCount() const {
    const uint16_t me = client_.GetMySpawnID();
    int count = 0;
    for (const auto& entry : client_.GetEntities()) {
        // npc_type 0 is a live player; 2 and 3 are corpses, 1 is an NPC.
        if (entry.second.npc_type == 0 && entry.first != me) {
            ++count;
        }
    }
    return count;
}

std::string ApplicationScenarioWorld::PlayerName() const {
    return client_.GetGameState().player().name();
}

std::vector<uint32_t> ApplicationScenarioWorld::PlayerBuffSpellIds() const {
    std::vector<uint32_t> ids;
    EQ::BuffManager* buffs = client_.GetBuffManager();
    if (!buffs) {
        return ids;
    }
    for (const auto& buff : buffs->getPlayerBuffs()) {
        ids.push_back(buff.spell_id);
    }
    return ids;
}

bool ApplicationScenarioWorld::PlayerLevitating() const {
    // B892 (issue #208): the game thread's OWN levitation state -- the
    // MovementComponent's stored predicate result (any SPA-57 buff, or a
    // self flymode 2/5 the server sent), flipped on buff apply/fade. The
    // base IScenarioWorld answers false (unreadable), which is what keeps
    // the scenario from ever passing on a world that does not read it.
    return client_.Movement().PlayerLevitating();
}

long long ApplicationScenarioWorld::PeerEquipmentMaterial(const std::string& peerName,
                                                          int materialSlot) const {
    // T494 (board B796). Entity::equipment[] (include/client/eq_types.h:69) is
    // written in exactly one place -- EntitiesComponent::ZoneProcessWearChange()
    // (src/client/entities/entities_component_appearance.cpp:409-440), the
    // OP_WearChange handler -- so what this returns is the SERVER's statement
    // about another character, never this client's belief about itself.
    if (materialSlot < 0 || materialSlot >= 9) {
        return -1;
    }
    // With several matching peers this returns the LARGEST material any of
    // them is wearing. That is deliberate: a character who logs out leaves a
    // linkdead entity behind for a while, so the same name can be present
    // twice, and the stale copy reads 0 -- taking the max means the fresh
    // spawn is what a scenario sees, not whichever came first in the map.
    const uint16_t me = client_.GetMySpawnID();
    long long best = -1;
    for (const auto& entry : client_.GetEntities()) {
        if (entry.second.npc_type != 0 || entry.first == me) {
            continue;  // same "live player, not us" rule OtherPlayerCount() uses
        }
        if (!peerName.empty() && entry.second.name != peerName) {
            continue;
        }
        const long long material = static_cast<long long>(entry.second.equipment[materialSlot]);
        if (material > best) {
            best = material;
        }
    }
    return best;
}

long long ApplicationScenarioWorld::SelfEquipmentMaterial(int materialSlot) const {
    // T498 (board B755/B823). The counterpart of PeerEquipmentMaterial(): that
    // one reads what the SERVER said about someone else, this one reads what
    // THIS client derived for itself.
    //
    // The store is GameState's, not EverQuest's m_entities, because
    // UpdatePlayerAppearanceFromInventory() derives equipment from the
    // INVENTORY and writes it back through ApplyAppearanceToGameState() only
    // (T221's note in entities_component_appearance.cpp says so out loud) --
    // m_entities never sees it, so reading there would always answer 0.
    if (materialSlot < 0 || materialSlot >= 9) {
        return -1;
    }
    const auto* me = client_.GetGameState().entities().getEntity(client_.GetMySpawnID());
    if (!me) {
        return -1;
    }
    return static_cast<long long>(me->equipment[materialSlot]);
}

void ApplicationScenarioWorld::SendCommand(const std::string& rawLine) {
    // The same unified dispatch --script and piped stdin use (T29), so a
    // scenario can never reach a command a human operator could not.
    app_.DispatchSlashCommand(rawLine);
}

long long ApplicationScenarioWorld::PlayerHP() const {
    return static_cast<long long>(client_.GetGameState().player().curHP());
}

bool ApplicationScenarioWorld::SimulateFall(float fallHeight) {
    // Game thread only (the automation callback runs here), so calling the
    // component directly is thread-safe: the same thread that owns
    // MovementComponent is the one sending. This drives the client's OWN
    // fall-damage sender against a synthetic height -- the headless scenario
    // has no renderer free-fall integrator to produce a real drop.
    client_.Movement().SendFallDamage(fallHeight);
    return true;
}

void ApplicationScenarioWorld::Note(const std::string& line) {
    LOG_INFO(MOD_SCENARIO, "{}", line);
}

}  // namespace scenario
}  // namespace eqt
