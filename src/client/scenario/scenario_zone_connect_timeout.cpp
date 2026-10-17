// B1072 (GitLab #392) + B1079 (GitLab #399): zone_connect_timeout -- the
// zone-IN bound, live.
//
// The PLAIN arm (B1072): /camp to character select, arm the dead-zone lever
// for TWO connects and Enter World. World names a real zone; the client
// connects to a dead port on that host instead, so nothing answers. Assert:
// no zone-in, exactly two connects (the attempt and ONE retry), back at
// character select on a FRESH list no sooner than two timeouts and well
// inside a third; then Enter World again (lever spent) and zone in -- the
// recovery is complete, not just reached.
//
// The SILENT arm (B1079, GitLab #399): the 2026-10-15 XU4 shape. /camp to
// character select, arm BOTH the dead-zone lever (one connect) and the
// "zone answered then went silent" lever, and Enter World. The client's
// ConnectToZone connects to the dead port (nothing answers) but overrides its
// handshake to ZONE_RECEIVING_PROFILE -- the phase the zone's PlayerProfile
// sets once it has answered the session and OP_ZoneEntry. Since that phase is
// past ZONE_CONNECTING, the bound walks its own 10 s deadline and escalates
// WITHOUT a connect retry (a re-connect would duplicate OP_ZoneEntry). Assert:
// no zone-in, exactly ONE connect, back at character select within the profile
// bound + handshake (and no sooner than the profile bound -- not the 30 s
// connect bound, which would be a wrong escalation). This covers zone load #N
// (the bound arms after the camp return, not only on a cold login).
//
// Run it on a NON-GM character (camp_to_charselect's reason: eqemu answers a
// GM's OP_Camp at once). scripts/scenario_configs/zone_connect_timeout.json.
// The silent arm is selected with --scenario-silent-zone.

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
// B1079: ZONE_RECEIVING_PROFILE's own bound (zone_connect_watchdog.h: 10000 ms,
// the network-tail row). The silent arm escalates on this, not on kBoundSeconds.
constexpr double kProfileBoundSeconds = 10.0;
constexpr int kAttempts = 2;
// UDP port 1 on the zone host: nothing answers a Daybreak session request.
constexpr int kDeadPort = 1;
constexpr double kSelectDeadline = player::kCampSeconds + 60.0;
// Two bounds plus the world handshake (~1.8 s measured for the camp return,
// camp.tsv [10]) and slack; a third bound's worth means no give-up happened.
constexpr double kRecoveryDeadline = kAttempts * kBoundSeconds + 25.0;
// B1079: the world handshake to the ZoneServerInfo (~1.8 s) + the profile
// bound + slack. One bound's worth of headroom over the profile bound is
// enough; two bounds would blur it against the connect bound.
constexpr double kSilentDeadline = kProfileBoundSeconds + 20.0;
constexpr double kReenterDeadline = 120.0;

class ZoneConnectTimeoutScenario final : public IScenario {
public:
    explicit ZoneConnectTimeoutScenario(const ScenarioOptions& options)
        : silent_(options.silentZone) {}

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
                silences0_ = w.ZoneAudioSilenceCount();  // B1080
                if (opens0_ < 0) {
                    SetReason("the zone-connect observable is not available on this world");
                    return StepResult::Fail;
                }
                if (silent_) {
                    // B1079 silent arm: one dead connect (nothing answers) +
                    // the handshake pinned past ZONE_CONNECTING (the zone
                    // "answered", then went silent). The bound must escalate
                    // on the profile phase's own budget, WITHOUT a retry.
                    if (!w.SimulateUnreachableZoneConnects(1, kDeadPort) ||
                        !w.SimulateZoneSilentAfterHandshake()) {
                        SetReason("the silent-zone levers are not available on this world");
                        return StepResult::Fail;
                    }
                    w.Note("zone_connect_timeout: silent arm -- 1 connect to port " +
                           std::to_string(kDeadPort) + ", handshake pinned past ZONE_CONNECTING");
                } else {
                    if (!w.SimulateUnreachableZoneConnects(kAttempts, kDeadPort)) {
                        SetReason("the dead-zone lever is not available on this world");
                        return StepResult::Fail;
                    }
                    w.Note("zone_connect_timeout: lever armed (" + std::to_string(kAttempts) +
                           " connects to port " + std::to_string(kDeadPort) + ")");
                }
                gen_ = w.CharacterListGeneration();
                if (!w.EnterWorldAs(me_)) {
                    SetReason("Enter World as " + me_ + " was refused");
                    return StepResult::Fail;
                }
                mark_ = tick.sinceStart;
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
                // B1079: the silent arm escalates WITHOUT a retry -- exactly one
                // connect. The plain arm is the B1072 shape: attempt + one retry.
                const long long want = silent_ ? 1 : kAttempts;
                if (opens != want) {
                    SetReason(os.str() + " (want " + std::to_string(want) +
                              (silent_ ? ": the zone answered, so NO connect retry"
                                       : ": the attempt + one retry") + ")");
                    return StepResult::Fail;
                }
                if (silent_) {
                    // Escalated on the profile phase's own budget, not the
                    // connect bound: no sooner than the profile bound, and
                    // well inside the connect bound (a 30 s escalation would be
                    // the B1072 shape -- a wrong escalation).
                    if (t < kProfileBoundSeconds - 1.0) {
                        SetReason(os.str() + " -- sooner than the profile bound");
                        return StepResult::Fail;
                    }
                    if (t > kBoundSeconds + 1.0) {
                        SetReason(os.str() + " -- later than the connect bound; the "
                                  "silent arm escalated on the wrong budget");
                        return StepResult::Fail;
                    }
                    SetReason(os.str() + " (the zone answered then went silent -- "
                              "escalated on the " + std::to_string((long)kProfileBoundSeconds) +
                              " s profile bound with no connect retry)");
                    return StepResult::Pass;
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
            } else if (t > (silent_ ? kSilentDeadline : kRecoveryDeadline)) {
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
    const bool silent_;
    long long gen_ = 0, opens0_ = 0, silences0_ = 0;
    double mark_ = 0.0, recoveredAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeZoneConnectTimeoutScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new ZoneConnectTimeoutScenario(options));
}

}  // namespace scenario
}  // namespace eqt
