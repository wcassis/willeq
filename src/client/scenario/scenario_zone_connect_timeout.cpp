// B1072 (GitLab #392): zone_connect_timeout -- the zone-IN bound, live.
//
//   /camp to character select (the camp_to_charselect route: the runner only
//   Steps a zoned-in client, and a non-GM account needs no GM command), then
//   arm the client's dead-zone lever for TWO connects and Enter World. World
//   names a real zone; the client connects to a dead port on that host
//   instead, so nothing answers. Assert: no zone-in, exactly two connects
//   (the attempt and ONE retry), back at character select on a FRESH list no
//   sooner than two timeouts and well inside a third; then Enter World again
//   (lever spent) and zone in -- the recovery is complete, not just reached.
//
// Run it on a NON-GM character (camp_to_charselect's reason: eqemu answers a
// GM's OP_Camp at once). scripts/scenario_configs/zone_connect_timeout.json.

#include "client/scenario/scenarios.h"

#include "client/player/camp_timer.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

// ZoneComponent::kZoneConnectTimeoutMs / kZoneConnectRetries, restated: the
// scenario TU links without the client (tests/cmake/core_utils.cmake), and
// test_zone_connect_timeout pins the production values.
constexpr double kBoundSeconds = 30.0;
constexpr int kAttempts = 2;
// UDP port 1 on the zone host: nothing answers a Daybreak session request.
constexpr int kDeadPort = 1;
constexpr double kSelectDeadline = player::kCampSeconds + 60.0;
// Two bounds plus the world handshake (~1.8 s measured for the camp return,
// camp.tsv [10]) and slack; a third bound's worth means no give-up happened.
constexpr double kRecoveryDeadline = kAttempts * kBoundSeconds + 25.0;
constexpr double kReenterDeadline = 120.0;

class ZoneConnectTimeoutScenario final : public IScenario {
public:
    const char* Name() const override { return "zone_connect_timeout"; }
    bool WantsCampToCharacterSelect() const override { return true; }

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        switch (stage_) {
        case Stage::Camp:
            me_ = w.PlayerName();
            gen_ = w.CharacterListGeneration();
            w.SendCommand("/camp");
            mark_ = tick.sinceStart;
            w.Note("zone_connect_timeout: /camp as " + me_ + " to reach character select");
            stage_ = Stage::Select;
            return StepResult::Running;
        case Stage::Select:
            if (w.AtCharacterSelect() && w.CharacterListGeneration() > gen_) {
                opens0_ = w.ZoneConnectOpens();
                silences0_ = w.ZoneAudioSilenceCount();
                if (opens0_ < 0 || !w.SimulateUnreachableZoneConnects(kAttempts, kDeadPort)) {
                    SetReason("the dead-zone lever is not available on this world");
                    return StepResult::Fail;
                }
                gen_ = w.CharacterListGeneration();
                if (!w.EnterWorldAs(me_)) {
                    SetReason("Enter World as " + me_ + " was refused");
                    return StepResult::Fail;
                }
                mark_ = tick.sinceStart;
                w.Note("zone_connect_timeout: lever armed (" + std::to_string(kAttempts) +
                       " connects to port " + std::to_string(kDeadPort) + "), Enter World sent");
                stage_ = Stage::Bound;
            } else if (tick.sinceStart - mark_ > kSelectDeadline) {
                SetReason("no character-select state within " + std::to_string(kSelectDeadline) + " s of /camp");
                return StepResult::Fail;
            }
            return StepResult::Running;
        case Stage::Bound: {
            const double t = tick.sinceStart - mark_;
            if (w.IsFullyZonedIn()) {
                SetReason("zoned in through a dead zone port -- the lever did not take");
                return StepResult::Fail;
            }
            if (w.AtCharacterSelect() && w.CharacterListGeneration() > gen_) {
                const long long opens = w.ZoneConnectOpens() - opens0_;
                std::ostringstream os;
                os << "back at character select " << t << " s after Enter World, " << opens
                   << " zone connect(s)";
                w.Note(os.str());
                if (opens != kAttempts) {
                    SetReason(os.str() + " (want " + std::to_string(kAttempts) + ": the attempt + one retry)");
                    return StepResult::Fail;
                }
                if (t < kAttempts * kBoundSeconds - 1.0) {
                    SetReason(os.str() + " -- sooner than " + std::to_string(kAttempts) + " bounds");
                    return StepResult::Fail;
                }
                // B1080 (#400): the give-up's teardown (AbandonZoneConnect ->
                // DisconnectFromZone -> CleanupZone) ran the zone audio
                // silence. Baseline was captured at arm time (the /camp
                // teardown is NOT this run's), so exactly ONE silence must
                // have landed since -- while the loading screen is up, the
                // leaving zone is quiet instead of audible. A world that
                // cannot read the count cannot verify the fix: fail.
                const long long silences = w.ZoneAudioSilenceCount();
                if (silences < 0) {
                    SetReason("the world cannot read the zone audio silence count (unreadable)");
                    return StepResult::Fail;
                }
                if (silences - silences0_ != 1) {
                    SetReason("the zone connect give-up did not run exactly one zone audio silence (saw " +
                              std::to_string(silences - silences0_) + ") -- the leaving zone's music and emitters kept playing");
                    return StepResult::Fail;
                }
                recoveredAt_ = t;
                if (!w.EnterWorldAs(me_)) {
                    SetReason("the second Enter World as " + me_ + " was refused");
                    return StepResult::Fail;
                }
                mark_ = tick.sinceStart;
                stage_ = Stage::Reenter;
            } else if (t > kRecoveryDeadline) {
                std::ostringstream os;
                os << "still not back at character select " << t << " s after Enter World (zone connects: "
                   << (w.ZoneConnectOpens() - opens0_) << ")";
                SetReason(os.str());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        case Stage::Reenter: {
            const double t = tick.sinceStart - mark_;
            if (w.IsFullyZonedIn() && !w.ZoneName().empty()) {
                std::ostringstream os;
                os << "dead zone port -> " << kAttempts << " connects -> character select (fresh list) at "
                   << recoveredAt_ << " s -> Enter World " << me_ << " -> zoned in to '" << w.ZoneName()
                   << "' " << t << " s later";
                SetReason(os.str());
                return StepResult::Pass;
            }
            if (t > kReenterDeadline) {
                SetReason("recovered, but the second Enter World never zoned in within " +
                          std::to_string(kReenterDeadline) + " s");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        }
        return StepResult::Running;
    }

private:
    enum class Stage { Camp, Select, Bound, Reenter };
    Stage stage_ = Stage::Camp;
    std::string me_;
    long long gen_ = 0, opens0_ = 0, silences0_ = 0;
    double mark_ = 0.0, recoveredAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeZoneConnectTimeoutScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new ZoneConnectTimeoutScenario());
}

}  // namespace scenario
}  // namespace eqt
