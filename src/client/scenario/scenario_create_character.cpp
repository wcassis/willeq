#include "client/scenario/scenarios.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

/**
 * create_character -- B888 (issue #204). Run with --create-character: the
 * world script creates the character at character select and enters world
 * with it, so by the time the runner calls Step() (fully zoned in) the whole
 * creation already happened. This scenario asserts its RESULT:
 *
 *   1. the player in world is the character the script created;
 *   2. it is in its chosen home city -- one of the city's zones
 *      (data/char_create.tsv), and
 *   3. never a tutorial zone (Will 2026-09-23: no tutorial; eqemu's
 *      World:TutorialZoneID is 189, short name tutorialb).
 *
 * A name or a creation the server refused never reaches a zone: the script
 * ends the run itself (kClientFatalExit) and the runner reports
 * could-not-start, never a pass.
 */
class CreateCharacterScenario final : public IScenario {
public:
    explicit CreateCharacterScenario(const ScenarioOptions& o)
        : name_(o.expectCharacterName), zones_(o.expectHomeZones) {}

    const char* Name() const override { return "create_character"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick&) override {
        if (name_.empty()) {
            SetReason("create_character needs --create-character (no expected name)");
            return StepResult::Fail;
        }
        const std::string player = world.PlayerName();
        const std::string zone = world.ZoneName();
        std::ostringstream os;
        os << "player='" << player << "' zone='" << zone << "'";
        if (player != name_) {
            os << ": expected the created character '" << name_ << "'";
            SetReason(os.str());
            return StepResult::Fail;
        }
        if (zone.rfind("tutorial", 0) == 0) {
            os << ": the character was sent to the tutorial (Will: home city only)";
            SetReason(os.str());
            return StepResult::Fail;
        }
        if (!zones_.empty() && std::find(zones_.begin(), zones_.end(), zone) == zones_.end()) {
            os << ": not one of the home city's zones {";
            for (size_t i = 0; i < zones_.size(); ++i) os << (i ? "," : "") << zones_[i];
            os << "}";
            SetReason(os.str());
            return StepResult::Fail;
        }
        os << ": created and in its home city";
        SetReason(os.str());
        return StepResult::Pass;
    }

private:
    std::string name_;
    std::vector<std::string> zones_;
};

}  // namespace

std::unique_ptr<IScenario> MakeCreateCharacterScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new CreateCharacterScenario(options));
}

}  // namespace scenario
}  // namespace eqt
