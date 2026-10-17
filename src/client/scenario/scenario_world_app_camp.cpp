// B1005 (GitLab #324): the /camp and character-select observables and the one
// action, over the real client. Contracts in scenario.h.
//
// EnterWorldAs() calls LoginWorldComponent::EnterWorldFromSelect -- the body
// the character-select screen's Enter World request reaches through
// CharacterSelectService -- so what goes on the wire is what the button sends.

#include "client/scenario/scenario_world_app.h"

#include "client/eq.h"
#include "client/login_world/login_world_component.h"
#include "client/player/player_component.h"

namespace eqt {
namespace scenario {

int ApplicationScenarioWorld::CampPhase() const {
    return static_cast<int>(client_.Player().Camp().phase());
}

bool ApplicationScenarioWorld::AtCharacterSelect() const {
    const auto& lw = client_.LoginWorld();
    return lw.CharacterSelectScreen() && lw.CharacterListGeneration() > 0 &&
           !lw.AwaitingFreshCharacterList() && !lw.HasSentEnterWorld();
}

std::vector<std::string> ApplicationScenarioWorld::CharacterListNames() const {
    std::vector<std::string> names;
    for (const auto& e : client_.LoginWorld().GetCharacterSelectList()) names.push_back(e.name);
    return names;
}

long long ApplicationScenarioWorld::CharacterListGeneration() const {
    return static_cast<long long>(client_.LoginWorld().CharacterListGeneration());
}

bool ApplicationScenarioWorld::EnterWorldAs(const std::string& name) {
    return client_.LoginWorld().EnterWorldFromSelect(name);
}

// B1072 (#392): zone_connect_timeout's observable and its lever
// (EverQuest::SimulateUnreachableZoneConnects, zone_connect.cpp).
long long ApplicationScenarioWorld::ZoneConnectOpens() const {
    return client_.ZoneConnectOpenCount();
}

bool ApplicationScenarioWorld::SimulateUnreachableZoneConnects(int attempts, int port) {
    if (attempts < 0 || port < 1 || port > 65535) return false;
    client_.SimulateUnreachableZoneConnects(attempts, static_cast<uint16_t>(port));
    return true;
}

// B1079 (#399): the "zone answered then went silent" lever (EverQuest::
// SimulateZoneSilentAfterHandshake, zone_connect.cpp / everquest.cpp). The
// silent arm of zone_connect_timeout.
bool ApplicationScenarioWorld::SimulateZoneSilentAfterHandshake() {
    client_.SimulateZoneSilentAfterHandshake();
    return true;
}

}  // namespace scenario
}  // namespace eqt
