// B1068 (#388): zone_line_walk -- a WALKED zone-line crossing zones the client.
//
// The live rig (scripts/scenario.sh zone_line_walk) seeds the character a few
// units short of a zone line facing it (zone_line_walk_seed.sql), runs this
// client X11-windowed under Xvfb, and its sidecar holds the forward key with
// xdotool -- so the movement is the RENDERER's keyboard path, the one Will
// walks, not a game-thread /moveto. This state machine only watches: it
// records the zone it started in and passes once the client is fully zoned in
// to a DIFFERENT zone. A run whose crossing never zones ends on the runner's
// timeout (2); the post-check (zone_line_walk.post.sh) asserts the server
// half -- the zone log's OP_ZoneChange for the character.

#include "client/scenario/scenarios.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

class ZoneLineWalkScenario final : public IScenario {
public:
    const char* Name() const override { return "zone_line_walk"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (startZone_.empty()) {
            startZone_ = world.ZoneName();
            if (startZone_.empty()) {
                SetReason("fully zoned in but the zone name is empty");
                return StepResult::Fail;
            }
            world.Note("zone_line_walk: started in '" + startZone_ +
                       "', waiting for the walked crossing to zone");
            SetReason("still in '" + startZone_ + "'");
            return StepResult::Running;
        }
        if (world.IsFullyZonedIn()) {
            const std::string zone = world.ZoneName();
            if (!zone.empty() && zone != startZone_) {
                std::ostringstream os;
                os << "walked across a zone line: '" << startZone_ << "' -> '" << zone
                   << "', sinceStart=" << tick.sinceStart << "s";
                SetReason(os.str());
                return StepResult::Pass;
            }
        }
        return StepResult::Running;
    }

private:
    std::string startZone_;
};

}  // namespace

std::unique_ptr<IScenario> MakeZoneLineWalkScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new ZoneLineWalkScenario());
}

}  // namespace scenario
}  // namespace eqt
