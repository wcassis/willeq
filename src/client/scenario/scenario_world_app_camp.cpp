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

}  // namespace scenario
}  // namespace eqt
