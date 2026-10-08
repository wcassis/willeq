// B893: fall_damage -- the client SENDS fall damage and the server's HP
// change comes back.
//
// The state machine is the offline-testable unit (it runs in
// tests/test_scenario_state_machines.cpp with a FakeWorld, no client, no
// server). In a live run:
//
//   1. Read the player's current HP (the baseline).
//   2. Drive the client's OWN fall-damage sender against a synthetic height
//      (world.SimulateFall -> MovementComponent::SendFallDamage ->
//      OP_EnvDamage dmgtype 252). A headless scenario has no renderer
//      free-fall integrator to produce a real drop, so the world does it.
//   3. Wait up to fallWaitSeconds for the server's HP change to arrive
//      (eqemu's non-GM path does SetHP(GetHP() - damage) + SendHPUpdate();
//      a GM takes 1 HP and gets the "Your GM status protects you from N
//      points of ... damage" chat line -- either way the HP drops).
//
// A Pass is "the HP dropped after the send". A Fail is "the send went out
// and the HP did not move within the deadline" -- which is exactly the
// pre-B893 defect: the client had no OP_EnvDamage sender at all.

#include "client/scenario/scenarios.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

class FallDamageScenario final : public IScenario {
public:
    explicit FallDamageScenario(const ScenarioOptions& options)
        : height_(options.fallDamageHeight),
          waitSeconds_(options.fallWaitSeconds) {}

    const char* Name() const override { return "fall_damage"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (!armed_) {
            baseline_ = world.PlayerHP();
            if (baseline_ < 0) {
                SetReason("could not read the player's HP before zone-in");
                return StepResult::Fail;
            }
            if (!world.SimulateFall(height_)) {
                SetReason("the world cannot drive a fall (SimulateFall unavailable)");
                return StepResult::Fail;
            }
            armed_ = true;
            world.Note("fall_damage: simulated a " + std::to_string(height_) +
                       "-unit drop, baseline HP=" + std::to_string(baseline_));
            return StepResult::Running;
        }

        const long long now = world.PlayerHP();
        if (now >= 0 && now < baseline_) {
            std::ostringstream os;
            os << "fall damage landed: HP " << baseline_ << " -> " << now
               << " (dropped " << (baseline_ - now) << ") after a "
               << height_ << "-unit drop, sinceStart=" << tick.sinceStart << "s";
            SetReason(os.str());
            return StepResult::Pass;
        }
        if (tick.sinceStart >= waitSeconds_) {
            SetReason("sent a " + std::to_string(height_) + "-unit fall but HP never "
                      "dropped within " + std::to_string(waitSeconds_) + "s "
                      "(baseline=" + std::to_string(baseline_) + ", now=" +
                      std::to_string(now) + ")");
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

private:
    float height_;
    double waitSeconds_;
    bool armed_ = false;
    long long baseline_ = -1;
};

}  // namespace

std::unique_ptr<IScenario> MakeFallDamageScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new FallDamageScenario(options));
}

}  // namespace scenario
}  // namespace eqt
