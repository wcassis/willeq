#include "client/scenario/scenarios.h"

#include <algorithm>

namespace eqt {
namespace scenario {

namespace {

struct Entry {
    const char* name;
    std::unique_ptr<IScenario> (*make)(const ScenarioOptions&);
};

const Entry kScenarios[] = {
    {"zone_in", &MakeZoneInScenario},
    {"two_client_survive", &MakeTwoClientSurviveScenario},
    {"cast_lands", &MakeCastLandsScenario},
    // T494 (board B796): a PAIR. equip_observed is the verdict; equip_actor is
    // the peer scripts/scenario.sh launches for it.
    {"equip_observed", &MakeEquipObservedScenario},
    {"equip_actor", &MakeEquipActorScenario},
};

}  // namespace

std::unique_ptr<IScenario> MakeScenario(const std::string& name, const ScenarioOptions& options) {
    for (const auto& entry : kScenarios) {
        if (name == entry.name) {
            return entry.make(options);
        }
    }
    return nullptr;
}

std::vector<std::string> ScenarioNames() {
    std::vector<std::string> names;
    names.reserve(sizeof(kScenarios) / sizeof(kScenarios[0]));
    for (const auto& entry : kScenarios) {
        names.emplace_back(entry.name);
    }
    return names;
}

bool IsScenarioName(const std::string& name) {
    return std::any_of(std::begin(kScenarios), std::end(kScenarios),
                       [&name](const Entry& e) { return name == e.name; });
}

}  // namespace scenario
}  // namespace eqt
