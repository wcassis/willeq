#include "client/scenario/scenarios.h"

namespace eqt {
namespace scenario {
namespace {

// B1025-H5 (#345): the hotbar_lifecycle lane's PEER. It exists to make the
// card's acceptance C -- "a SECOND character on the account sees empty slots"
// -- a real assertion instead of a file-absence assumption.
//
// It runs as a plain zone_in (the peer process, the lane's second account
// character, Testbuddy, non-GM), and when zoned it assigns ONE skill slot
// through the same user path (HotbarAssignIntent -> HotbarModel::assignSlot)
// the main character uses. That assignment is deliberate:
//   1. it is the second character's own hotbar activity -- proof the peer is
//      a live client whose per-character INI the post-check can read, and
//   2. it gives the post-check's isolation assertion something to prove: the
//      peer's per-character INI ends up holding ONLY this skill slot and none
//      of Testcaster's spell/item/skill assignments -- the per-character file
//      (not a shared config, not a character-inherited import) is what keeps
//      the two characters apart.
//
// The assignment itself is not a server-visible result (it is the user path
// under test, and the user path's persistence is the lane's point), so this
// peer asserts only that it zoned in and the assignment was accepted; the
// verdict lives in the lane's post-check reading both INI files.
class HotbarIsolationAssignScenario final : public IScenario {
public:
    const char* Name() const override { return "hotbar_isolation_assign"; }

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;
        if (!assigned_) {
            if (!w.IsFullyZonedIn() || w.PlayerName().empty()) {
                if (t > 120.0) {
                    SetReason("hotbar_isolation_assign: no zone-in within 120 s");
                    return StepResult::Fail;
                }
                return StepResult::Running;
            }
            // The peer's one slot: a skill the class may not even own is fine
            // -- assignment is the user path, not a cast. Slot 0, page 0.
            if (!w.AssignHotbarSlot(0, 0, 2 /*Skill*/, 55 /*Fishing*/, {})) {
                SetReason("hotbar_isolation_assign: AssignHotbarSlot(skill) refused");
                return StepResult::Fail;
            }
            assigned_ = true;
            w.Note("hotbar_isolation_assign: " + w.PlayerName() +
                   " assigned page-0 slot 0 = skill 55 (its own INI only)");
            SetReason("second character " + w.PlayerName() + " zoned and assigned its one slot");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    bool assigned_ = false;
};

}  // namespace

std::unique_ptr<IScenario> MakeHotbarIsolationAssignScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new HotbarIsolationAssignScenario());
}

}  // namespace scenario
}  // namespace eqt
