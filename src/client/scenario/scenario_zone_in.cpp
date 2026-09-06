#include "client/scenario/scenarios.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

/**
 * zone_in -- the cheapest possible live assertion, and the one every unit on
 * the board re-implements by grepping "Zone connection complete" out of a
 * session log.
 *
 * The runner does not call Step() until IsFullyZonedIn() is true, so this
 * scenario's whole job is to check that arriving there produced a coherent
 * zone: a non-empty zone name. An empty name with the three zoned-in terms
 * set means the world/zone handshake finished against no zone record, which
 * a log grep for "Zone connection complete" cannot see at all.
 */
class ZoneInScenario final : public IScenario {
public:
    const char* Name() const override { return "zone_in"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const std::string zone = world.ZoneName();
        if (zone.empty()) {
            SetReason("fully zoned in but the zone name is empty");
            return StepResult::Fail;
        }
        std::ostringstream os;
        os << "zoned in to '" << zone << "' (other players in zone: "
           << world.OtherPlayerCount() << ", sinceStart=" << tick.sinceStart << "s)";
        SetReason(os.str());
        return StepResult::Pass;
    }
};

}  // namespace

std::unique_ptr<IScenario> MakeZoneInScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new ZoneInScenario());
}

}  // namespace scenario
}  // namespace eqt
