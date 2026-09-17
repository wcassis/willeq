#include "client/scenario/scenarios.h"

#include <sstream>
#include <string>
#include <vector>

namespace eqt {
namespace scenario {
namespace {

// T494 (board B796): the OBSERVED half of the /equip proof.
//
// A client that runs `/equip` and then asks its own InventoryManager whether
// the item moved proves nothing -- it is asserting on the belief it just
// wrote (`two-client-live-testing`: single-client verification is circular).
// The pair below splits actor from witness across two PROCESSES:
//
//   equip_actor    -- the peer. Zones in, settles, sends ONE `/equip <db
//                     general slot> 13`, then just stays logged in.
//   equip_observed -- the observer. Sends NOTHING. It waits for the peer to
//                     appear and then watches the peer's
//                     `Entity::equipment[materialSlot]`.
//
// `Entity::equipment[]` is filled in exactly one place:
// EntitiesComponent::ZoneProcessWearChange()
// (src/client/entities/entities_component_appearance.cpp:409-440), the
// handler for the server's OP_WearChange. So a 0 -> non-zero transition on
// the PEER's primary material is the server telling this client that the
// other character is now holding something -- the server's verdict on the
// actor's slot number, observed from outside.
//
// The slot the actor types is the DB slot (`inventory.slot_id`, RoF2
// numbering): 23 is general slot 1, which the row seeded by the harness uses.
// Before T494 that argument reached model slot 23 (DB 24), which is empty,
// and no WearChange ever followed.

constexpr int kPrimaryMaterialSlot = 7;  // Titanium wear slots: 7 = primary

class EquipObservedScenario final : public IScenario {
public:
    EquipObservedScenario(int materialSlot, double peerDeadlineSeconds, double wearDeadlineSeconds)
        : materialSlot_(materialSlot),
          peerDeadlineSeconds_(peerDeadlineSeconds),
          wearDeadlineSeconds_(wearDeadlineSeconds) {}

    const char* Name() const override { return "equip_observed"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (!peerSeen_) {
            if (world.OtherPlayerCount() >= 1) {
                const long long material = world.PeerEquipmentMaterial("", materialSlot_);
                if (material < 0) {
                    return StepResult::Running;  // spawn seen, entity not readable yet
                }
                peerSeen_ = true;
                peerAt_ = tick.sinceStart;
                baseline_ = material;
                std::ostringstream os;
                os << "peer visible after " << peerAt_ << "s; its material slot "
                   << materialSlot_ << " reads " << baseline_
                   << " (0 = empty hand); watching for " << wearDeadlineSeconds_ << "s";
                world.Note(os.str());
                SetReason(os.str());
                return StepResult::Running;
            }
            if (tick.sinceStart >= peerDeadlineSeconds_) {
                std::ostringstream os;
                os << "no second player appeared within " << peerDeadlineSeconds_
                   << "s of zone-in (zone '" << world.ZoneName() << "')";
                SetReason(os.str());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }

        const long long material = world.PeerEquipmentMaterial("", materialSlot_);
        if (material < 0) {
            std::ostringstream os;
            os << "peer vanished " << (tick.sinceStart - peerAt_) << "s into the watch";
            SetReason(os.str());
            return StepResult::Fail;
        }
        if (material != baseline_ && material != 0) {
            std::ostringstream os;
            os << "OBSERVED: peer's material slot " << materialSlot_ << " went " << baseline_
               << " -> " << material << ", " << (tick.sinceStart - peerAt_)
               << "s after it appeared -- the server's OP_WearChange, not this client's "
                  "own belief";
            world.Note(os.str());
            SetReason(os.str());
            return StepResult::Pass;
        }
        if (tick.sinceStart - peerAt_ >= wearDeadlineSeconds_) {
            std::ostringstream os;
            os << "peer's material slot " << materialSlot_ << " never left " << baseline_
               << " in " << wearDeadlineSeconds_ << "s -- no OP_WearChange arrived, so the "
                  "peer's /equip moved nothing the server agreed with";
            SetReason(os.str());
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

private:
    int materialSlot_;
    double peerDeadlineSeconds_;
    double wearDeadlineSeconds_;
    bool peerSeen_ = false;
    double peerAt_ = 0.0;
    long long baseline_ = 0;
};

class EquipActorScenario final : public IScenario {
public:
    EquipActorScenario(std::vector<std::string> commands, double settleSeconds, double holdSeconds)
        : commands_(std::move(commands)),
          settleSeconds_(settleSeconds),
          holdSeconds_(holdSeconds) {}

    const char* Name() const override { return "equip_actor"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (!sent_) {
            // Settle first: the inventory arrives in the zone-in burst, and a
            // /equip issued before InventoryManager has the row refuses with
            // "nothing to move" and the run proves nothing.
            if (tick.sinceStart < settleSeconds_) {
                return StepResult::Running;
            }
            std::ostringstream os;
            os << "sent";
            for (const std::string& command : commands_) {
                world.SendCommand(command);
                os << " '" << command << "'";
            }
            sent_ = true;
            sentAt_ = tick.sinceStart;
            os << " at " << sentAt_ << "s; staying logged in for " << holdSeconds_
               << "s so the observer can watch";
            world.Note(os.str());
            SetReason(os.str());
            return StepResult::Running;
        }
        if (!world.IsFullyZonedIn()) {
            SetReason("actor left the zone before the hold finished");
            return StepResult::Fail;
        }
        if (tick.sinceStart - sentAt_ >= holdSeconds_) {
            std::ostringstream os;
            os << "sent " << commands_.size() << " command(s) and held "
               << (tick.sinceStart - sentAt_) << "s";
            SetReason(os.str());
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    std::vector<std::string> commands_;
    double settleSeconds_;
    double holdSeconds_;
    bool sent_ = false;
    double sentAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeEquipObservedScenario(const ScenarioOptions& options) {
    (void)options;
    // The peer window matches two_client_survive's (the harness staggers the
    // actor's launch); the wear window is generous because the actor settles
    // before it types anything.
    return std::unique_ptr<IScenario>(
        new EquipObservedScenario(kPrimaryMaterialSlot, 180.0, 150.0));
}

std::unique_ptr<IScenario> MakeEquipActorScenario(const ScenarioOptions& options) {
    (void)options;
    // `/equip list` first: it prints every carried slot through describeSlot(),
    // so the run also MEASURES the bag half of the table -- the harness seeds a
    // row at DB slot 5210 (backpack in general slot 29, first bag index) and the
    // list has to name it 5210, not the model's 311.
    return std::unique_ptr<IScenario>(
        new EquipActorScenario({"/equip list", "/equip 23 13"}, 25.0, 120.0));
}

}  // namespace scenario
}  // namespace eqt
