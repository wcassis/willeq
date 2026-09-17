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
// WHY THE ACTOR LOGS OUT AND BACK IN (T494, MEASURED). eqemu does NOT
// broadcast a weapon WearChange when a weapon is equipped by OP_MoveItem:
// zone/inventory.cpp:2205 computes the material slot with
// Client::SlotConvert2(), whose switch (zone/client.cpp:4128-4154) has cases
// for the seven ARMOR slots ONLY -- slotPrimary(13) and slotSecondary(14)
// fall through to its `uint8 slot2 = 0` default, so the packet it sends
// describes the HEAD, whose material did not change, and
// zone/mob_appearance.cpp:420-428's dedupe then drops it. MEASURED on the
// local lane 2026-09-17: the row moved 23 -> 13 in `inventory` and the
// observing client logged not one OP_WearChange in the 150 s that followed.
// The weapon materials ARE broadcast at SPAWN
// (Mob::SendWearChangeAndLighting over textureBegin..LastTexture, which
// includes weaponPrimary=7), which the observer sees as a burst of nine.
//
// So the observable the server WILL produce is the peer's next spawn. The
// actor equips and then exits; the harness logs the same character straight
// back in; the fresh spawn packet carries the primary material read from the
// database. That is still entirely the server's statement -- the actor
// process that believed anything is gone by then.
//
// The slot the actor types is the DB slot (`inventory.slot_id`, RoF2
// numbering): 23 is general slot 1, which the row seeded by the harness uses.
// Before T494 that argument reached model slot 23 (DB 24), which is empty,
// and no WearChange ever followed.

constexpr int kPrimaryMaterialSlot = 7;  // Titanium wear slots: 7 = primary

// All nine, for the trace: a run that fails should say whether the peer's
// OTHER materials are there (they arrive in the spawn packet, not a
// WearChange), which tells "the entity is fine, no WearChange came" apart
// from "we are not reading the entity at all".
std::string Materials(IScenarioWorld& world) {
    std::ostringstream os;
    os << '[';
    for (int slot = 0; slot < 9; ++slot) {
        if (slot) os << ',';
        os << world.PeerEquipmentMaterial("", slot);
    }
    os << ']';
    return os.str();
}

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
                   << " (0 = empty hand); all nine materials " << Materials(world)
                   << "; watching for " << wearDeadlineSeconds_ << "s";
                world.Note(os.str());
                SetReason(os.str());
                if (baseline_ != 0) {
                    std::ostringstream fail;
                    fail << "the peer is ALREADY holding something (material slot "
                         << materialSlot_ << " = " << baseline_
                         << ") -- this run could not tell an equip from a character that "
                            "was wearing it all along, which is the trap B796's own "
                            "experiment fell into";
                    SetReason(fail.str());
                    return StepResult::Fail;
                }
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

        // The peer LEAVING is expected -- it equips and logs out, and the
        // proof is what the server puts in its next spawn packet.
        const long long material = world.PeerEquipmentMaterial("", materialSlot_);
        if (material < 0) {
            if (!peerLeft_) {
                peerLeft_ = true;
                std::ostringstream os;
                os << "peer left at " << tick.sinceStart << "s; waiting for it to come back";
                world.Note(os.str());
            }
            if (tick.sinceStart - peerAt_ >= wearDeadlineSeconds_) {
                SetReason("the peer never came back");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        if (material > 0) {
            std::ostringstream os;
            os << "OBSERVED: the peer's material slot " << materialSlot_ << " went "
               << baseline_ << " -> " << material << " (all nine " << Materials(world)
               << ") " << (tick.sinceStart - peerAt_)
               << "s after it first appeared -- read off the SERVER's packets for another "
                  "character, never this client's own belief";
            world.Note(os.str());
            SetReason(os.str());
            return StepResult::Pass;
        }
        if (tick.sinceStart - peerAt_ >= wearDeadlineSeconds_) {
            std::ostringstream os;
            os << "peer's material slot " << materialSlot_ << " never left " << baseline_
               << " in " << wearDeadlineSeconds_ << "s (all nine materials now "
               << Materials(world) << ") -- the server never told this client the other "
                  "character is holding anything, so the /equip changed nothing it agreed "
                  "with";
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
    bool peerLeft_ = false;
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

/**
 * equip_actor_return -- the SECOND login of the same character. It sends
 * nothing at all; it exists only to be spawned, so the server hands the
 * observer a fresh spawn packet built from the database. zone_in would do the
 * job except that it passes and exits the instant it is zoned in, which is
 * too short for the observer to read the entity.
 */
class StayLoggedInScenario final : public IScenario {
public:
    StayLoggedInScenario(const char* name, double holdSeconds, long long expectSelfMaterial = -1)
        : name_(name), holdSeconds_(holdSeconds), expectSelfMaterial_(expectSelfMaterial) {}

    const char* Name() const override { return name_; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (!world.IsFullyZonedIn()) {
            SetReason("left the zone during the hold");
            return StepResult::Fail;
        }
        // T498 (board B755/B823): the SELF half of the proof. The observer
        // process reads the server's number for this character off its spawn
        // packet; this reads the number this client derived for itself from
        // the same item, which before T498 came from data/item_models.json and
        // could differ. They must be the same number.
        const long long self = world.SelfEquipmentMaterial(kPrimaryMaterialSlot);
        if (expectSelfMaterial_ >= 0 && self >= 0 && self != selfSeen_) {
            selfSeen_ = self;
            std::ostringstream os;
            os << "self material slot " << kPrimaryMaterialSlot << " reads " << self
               << " at " << tick.sinceStart << "s (expecting " << expectSelfMaterial_ << ")";
            world.Note(os.str());
        }
        if (tick.sinceStart >= holdSeconds_) {
            std::ostringstream os;
            os << "stayed logged in for " << tick.sinceStart << "s in '" << world.ZoneName()
               << "', sending nothing";
            if (expectSelfMaterial_ >= 0) {
                os << "; own material slot " << kPrimaryMaterialSlot << " = " << self;
                if (self != expectSelfMaterial_) {
                    os << ", but the server's items.idfile says " << expectSelfMaterial_
                       << " -- the self path is NOT computing a weapon material the way the"
                          " server does";
                    SetReason(os.str());
                    return StepResult::Fail;
                }
                os << ", matching the server's items.idfile";
            }
            SetReason(os.str());
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    const char* name_;
    double holdSeconds_;
    long long expectSelfMaterial_ = -1;
    long long selfSeen_ = -2;
};

}  // namespace

std::unique_ptr<IScenario> MakeEquipActorReturnScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(
        new StayLoggedInScenario("equip_actor_return", 60.0, options.expectSelfMaterial));
}

std::unique_ptr<IScenario> MakeEquipObservedScenario(const ScenarioOptions& options) {
    (void)options;
    // The peer window matches two_client_survive's (the harness staggers the
    // actor's launch); the wear window is generous because the actor settles
    // before it types anything.
    return std::unique_ptr<IScenario>(
        new EquipObservedScenario(kPrimaryMaterialSlot, 180.0, 240.0));
}

std::unique_ptr<IScenario> MakeEquipActorScenario(const ScenarioOptions& options) {
    (void)options;
    // `/equip list` first: it prints every carried slot through describeSlot(),
    // so the run also MEASURES the bag half of the table -- the harness seeds a
    // row at DB slot 5210 (backpack in general slot 29, first bag index) and the
    // list has to name it 5210, not the model's 311.
    return std::unique_ptr<IScenario>(
        new EquipActorScenario({"/equip list", "/equip 23 13"}, 25.0, 15.0));
}

}  // namespace scenario
}  // namespace eqt
