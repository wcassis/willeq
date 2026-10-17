// B1081 (GitLab #401): the character-select screen must not come up on a
// zone change.
//
// The live mechanism (issue #401, Will's 2026-10-15 XU4 sessions): a run
// with the screen up keeps m_select_screen ON after its first Enter World
// (the screen only goes down for good when OP_EnterWorld is out). A zone
// change tears the world session down (ReconnectWorld resets m_enter_world_
// sent to false) and the zone-change pass never re-sends OP_SendCharInfo
// (eqemu's world skips the character block on a zoning pass), so
// CharacterSelectService::tick() re-activates the screen from the STALE
// first-login list for the frames until the automatic OP_EnterWorld goes
// out -- the flash. LoginWorldComponent::IsZoningReconnect() gates those
// frames, and CharacterSelectService::Activations() counts the screen-ups
// a run that never draws the screen (headless --scenario) can assert.
//
// This scenario is the lane: the run opts in to the screen
// (WantsCharacterSelectScreen -- the client's auto Enter World is suppressed
// while the screen is up, so a --scenario run only reaches a zone by this
// scenario pressing Enter World, exactly as the camp scenarios opt their
// runs in for the camp return), the character zones in, /gmzones to
// ScenarioOptions::zoneChangeTargetZone -- the GM form of a cross-zone
// change, the same ProcessDeferredZoneChange a zone-line crossing or a
// death-to-bind re-entry take -- and the assertion is that the screen came
// up exactly ONCE this run: the fresh login's. A zone change must add zero.
//
// The character must be a GM: /gmzone <name> <zone_id> is the only
// client-side way to start a cross-zone change on demand (command_
// processor_spells_gm.cpp), and the target must be a DIFFERENT zone than
// the character's -- a same-zone move relocates in place (ZoneProcess
// ZoneChange's same-zone branch, zone_component.cpp:90) and there is no
// world reconnect at all, so there would be nothing to gate.

#include "client/scenario/scenarios.h"

#include <sstream>
#include <string>

namespace eqt {
namespace scenario {
namespace {

// How long the target zone may take to zone in after the /gmzone went out.
// The zone-in bound itself is B1072's (30 s x 2 attempts); this only bounds
// the WHOLE transition (world pass + zone load) and must sit above it.
constexpr double kZoneDeadline = 180.0;

class ZoneChangeNoFlashScenario final : public IScenario {
public:
    explicit ZoneChangeNoFlashScenario(const ScenarioOptions& options)
        : target_(options.zoneChangeTargetZone) {}

    const char* Name() const override { return "zone_change_no_flash"; }
    bool WantsCharacterSelectScreen() const override { return true; }
    // The runner's pre-flight waits for IsFullyZonedIn before the first
    // Step, but a --scenario run with the screen up never auto-zones-in (the
    // automatic Enter World is suppressed while the screen is up,
    // WorldProcessExpansionInfo) -- this scenario drives its own zone-in by
    // pressing the screen's Enter World. Skip the wait and start at t=0.
    bool WantsPreFlight() const override { return false; }

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        switch (stage_) {
        case Stage::WaitList: {
            if (w.CharacterSelectActivations() < 1) {
                return StepResult::Running;  // the fresh login's list is still owed
            }
            // The character this run enters the world as: the select
            // screen's cursor entry, else the run's configured character
            // (game-state's player name is empty pre-zone-in).
            const std::string who = w.CharacterSelectName();
            if (who.empty()) {
                SetReason("no character to enter the world as: the list is empty and no configured character");
                return StepResult::Fail;
            }
            if (!w.EnterWorldAs(who)) {
                SetReason("Enter World as " + who + " was refused by the screen");
                return StepResult::Fail;
            }
            w.Note("zone_change_no_flash: screen up (activations " +
                   std::to_string(w.CharacterSelectActivations()) + "), Enter World " + who);
            stage_ = Stage::FirstZone;
            return StepResult::Running;
        }
        case Stage::FirstZone: {
            if (w.IsFullyZonedIn() && !w.ZoneName().empty()) {
                if (w.PlayerName().empty()) {
                    SetReason("the character name is unknown -- a GM character is needed for /gmzone");
                    return StepResult::Fail;
                }
                if (target_ <= 0) {
                    SetReason("zoneChangeTargetZone is 0: no zone to change to");
                    return StepResult::Fail;
                }
                firstZone_ = w.ZoneName();
                w.SendCommand("/gmzone " + w.PlayerName() + " " + std::to_string(target_));
                w.Note("in '" + firstZone_ + "': /gmzone " + w.PlayerName() + " " +
                       std::to_string(target_) + " (zone " + std::to_string(target_) + ")");
                sentAt_ = tick.sinceStart;
                stage_ = Stage::Transition;
            }
            return StepResult::Running;
        }
        case Stage::Transition: {
            if (w.IsFullyZonedIn() && !w.ZoneName().empty() && w.ZoneName() != firstZone_) {
                const long long a = w.CharacterSelectActivations();
                if (a != 1) {
                    std::ostringstream os;
                    os << "the character-select screen came up " << a << " times: once on the "
                       << "fresh login and again on the zone change to '" << w.ZoneName()
                       << "' -- the flash (#401). The gate is LoginWorldComponent::"
                       << "IsZoningReconnect in CharacterSelectService::tick.";
                    SetReason(os.str());
                    return StepResult::Fail;
                }
                std::ostringstream os;
                os << "screen up exactly once (the fresh login); zone change '" << firstZone_
                   << "' -> '" << w.ZoneName() << "' added zero screen-ups at "
                   << (tick.sinceStart - sentAt_) << " s after the /gmzone";
                SetReason(os.str());
                return StepResult::Pass;
            }
            if (tick.sinceStart - sentAt_ > kZoneDeadline) {
                std::ostringstream os;
                os << "the zone change to zone " << target_ << " never completed within "
                   << kZoneDeadline << " s";
                SetReason(os.str());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        }  // switch
        return StepResult::Running;
    }

private:
    enum class Stage { WaitList, FirstZone, Transition };
    Stage stage_ = Stage::WaitList;
    std::string firstZone_;
    long long target_ = 0;
    double sentAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeZoneChangeNoFlashScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new ZoneChangeNoFlashScenario(options));
}

}  // namespace scenario
}  // namespace eqt
