// B1005 (GitLab #324): the /camp scenarios. The measured rule they assert:
// include/client/player/camp_timer.h (review/UIGAP-ORACLE/324/camp.tsv).
//
//   camp_to_charselect  /camp; nothing leaves the zone before the countdown
//                       (OP_Logout at +30 s, then the server's reply); the
//                       character-select state comes up on a FRESH
//                       OP_SendCharInfo naming us; Enter World again; zoned in.
//   camp_desktop        /camp desktop; the client stops only after the
//                       countdown and the server's logout reply -- judged in
//                       OnClientStopped(), the runner's last look.
//
// Run them on a NON-GM character: eqemu answers a GM's OP_Camp at once
// (Handle_OP_Camp -> OnDisconnect, camp.tsv [16]), so a GM never counts down.

#include "client/scenario/scenarios.h"

#include "client/player/camp_timer.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

// The countdown is 30 s on the client's clock; the server's reply follows
// within milliseconds (camp.tsv [6][7]). Leaving earlier than this is a fail.
constexpr double kEarliestLeave = player::kCampSeconds - 0.5;
// Plenty for the reply + world handshake (~1.8 s measured, [10]).
constexpr double kSelectDeadline = player::kCampSeconds + 60.0;
constexpr double kReenterDeadline = 120.0;

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ",") + s;
    return out;
}

class CampToCharselectScenario final : public IScenario {
public:
    const char* Name() const override { return "camp_to_charselect"; }
    bool WantsCampToCharacterSelect() const override { return true; }

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        switch (stage_) {
        case Stage::Camp:
            me_ = w.PlayerName();
            zone_ = w.ZoneName();
            gen0_ = w.CharacterListGeneration();
            w.SendCommand("/camp");
            campAt_ = tick.sinceStart;
            if (w.CampPhase() != IScenarioWorld::kCampCounting) {
                SetReason("/camp did not start the countdown (phase " + std::to_string(w.CampPhase()) + ")");
                return StepResult::Fail;
            }
            w.Note("camp: /camp sent as " + me_ + " in " + zone_);
            stage_ = Stage::Countdown;
            return StepResult::Running;
        case Stage::Countdown: {
            const double t = tick.sinceStart - campAt_;
            if (!w.IsFullyZonedIn() || w.CampPhase() >= IScenarioWorld::kCampLogoutSent) {
                if (t < kEarliestLeave) {
                    std::ostringstream os;
                    os << "left the countdown after " << t << " s (< " << kEarliestLeave << " s)";
                    SetReason(os.str());
                    return StepResult::Fail;
                }
                std::ostringstream os;
                os << "countdown over after " << t << " s (phase " << w.CampPhase() << ")";
                w.Note(os.str());
                leftAt_ = t;
                stage_ = Stage::Select;
            } else if (w.CampPhase() != IScenarioWorld::kCampCounting) {
                SetReason("the countdown stopped by itself (phase " + std::to_string(w.CampPhase()) + ")");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        case Stage::Select: {
            const double t = tick.sinceStart - campAt_;
            if (w.AtCharacterSelect() && w.CharacterListGeneration() > gen0_) {
                const auto names = w.CharacterListNames();
                std::ostringstream os;
                os << "character select after " << t << " s: fresh list #" << w.CharacterListGeneration()
                   << " [" << Join(names) << "]";
                w.Note(os.str());
                if (std::find(names.begin(), names.end(), me_) == names.end()) {
                    SetReason("the fresh character list does not name " + me_ + ": " + Join(names));
                    return StepResult::Fail;
                }
                if (!w.EnterWorldAs(me_)) {
                    SetReason("Enter World as " + me_ + " was refused");
                    return StepResult::Fail;
                }
                selectAt_ = t;
                stage_ = Stage::Reenter;
            } else if (t > kSelectDeadline) {
                SetReason("no character-select state within " + std::to_string(kSelectDeadline) + " s of /camp");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        case Stage::Reenter: {
            const double t = tick.sinceStart - campAt_;
            if (w.IsFullyZonedIn() && !w.ZoneName().empty() && w.PlayerName() == me_) {
                std::ostringstream os;
                os << "/camp -> countdown " << leftAt_ << " s -> character select (fresh list) at "
                   << selectAt_ << " s -> Enter World " << me_ << " -> zoned in to '" << w.ZoneName()
                   << "' at " << t << " s";
                SetReason(os.str());
                return StepResult::Pass;
            }
            if (t - selectAt_ > kReenterDeadline) {
                SetReason("Enter World sent but no zone-in within " + std::to_string(kReenterDeadline) + " s");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        }
        return StepResult::Running;
    }

private:
    enum class Stage { Camp, Countdown, Select, Reenter };
    Stage stage_ = Stage::Camp;
    std::string me_, zone_;
    long long gen0_ = 0;
    double campAt_ = 0.0, leftAt_ = 0.0, selectAt_ = 0.0;
};

class CampDesktopScenario final : public IScenario {
public:
    const char* Name() const override { return "camp_desktop"; }

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        if (!sent_) {
            w.SendCommand("/camp desktop");
            sent_ = true;
            campAt_ = tick.sinceStart;
            if (w.CampPhase() != IScenarioWorld::kCampCounting) {
                SetReason("/camp desktop did not start the countdown");
                return StepResult::Fail;
            }
            w.Note("camp: /camp desktop sent");
            return StepResult::Running;
        }
        const double t = tick.sinceStart - campAt_;
        if (w.CampPhase() == IScenarioWorld::kCampToSelect || w.AtCharacterSelect()) {
            SetReason("/camp desktop went to character select");
            return StepResult::Fail;
        }
        if ((!w.IsFullyZonedIn() || w.CampPhase() == IScenarioWorld::kCampToDesktop) && t < kEarliestLeave) {
            std::ostringstream os;
            os << "left the zone after " << t << " s (< " << kEarliestLeave << " s)";
            SetReason(os.str());
            return StepResult::Fail;
        }
        if (t > kSelectDeadline) {
            SetReason("the client was still running " + std::to_string(t) + " s after /camp desktop");
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

    StepResult OnClientStopped(IScenarioWorld& w, const ScenarioTick& tick) override {
        if (!sent_) return StepResult::Running;
        const double t = tick.sinceStart - campAt_;
        std::ostringstream os;
        if (w.CampPhase() != IScenarioWorld::kCampToDesktop) {
            os << "the client stopped " << t << " s after /camp desktop without the server's logout reply (phase "
               << w.CampPhase() << ")";
            SetReason(os.str());
            return StepResult::Fail;
        }
        if (t < kEarliestLeave) {
            os << "the client stopped after " << t << " s (< " << kEarliestLeave << " s)";
            SetReason(os.str());
            return StepResult::Fail;
        }
        os << "/camp desktop: the server's logout reply, then the client stopped " << t << " s after the command";
        SetReason(os.str());
        return StepResult::Pass;
    }

private:
    bool sent_ = false;
    double campAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeCampToCharselectScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new CampToCharselectScenario());
}

std::unique_ptr<IScenario> MakeCampDesktopScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new CampDesktopScenario());
}

}  // namespace scenario
}  // namespace eqt
