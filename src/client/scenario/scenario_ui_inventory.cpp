// UIGAP-live: the live legs of the cursor-gesture (#151, B442), container /
// Combine (#215, B899) units.
//
//   pickup_roundtrip    an item goes general -> bag -> bank -> general, by
//                       /pickup /putdown /bag AND by synthetic slot clicks
//                       (IScenarioWorld::ClickSlot = the CursorGestureIntent a
//                       window click pushes). Asserts the client's own model
//                       at every hop and that it does not revert (a refused
//                       OP_MoveItem answers with a resync). The DATABASE side
//                       is asserted by scripts/scenario_configs/
//                       pickup_roundtrip.post.sh against the sidecar's
//                       1 Hz poll of the item's `inventory` row.
//   click_equip_actor   the peer: a weapon bag -> primary by two clicks.
//   click_equip_observed the verdict: the PEER's primary material, as the
//                       server told THIS client (OP_WearChange or the spawn).
//   combine_sewing      a carried sewing kit (Ruined Wolf Pelt) and the world
//                       forge (A Ruby, nofail): components placed by
//                       /pickup + /putdown, /combine, products on the CURSOR
//                       QUEUE (215 combine.tsv row 4: the returned tool first,
//                       the product behind it).
//
// Slot numbers are `inventory.slot_id` throughout (scenario.h).

#include "client/scenario/scenarios.h"
#include "client/state/event_types.h"
#include "scenario_steps.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace eqt {
namespace scenario {
namespace {

using steps::Step;
using steps::StepScenario;

constexpr int kCursor = 33;

std::string Slots(IScenarioWorld& w, std::initializer_list<int> slots) {
    std::ostringstream os;
    bool first = true;
    for (int s : slots) {
        if (!first) os << ' ';
        first = false;
        os << s << '=' << w.ClientItemAt(s);
    }
    os << " cursorQueue=" << w.CursorQueueSize();
    return os.str();
}

// ---------------------------------------------------------------------------
// pickup_roundtrip
// ---------------------------------------------------------------------------

// The fixture (scripts/scenario_configs/pickup_roundtrip_seed.sql): Testchar
// next to the banker Sorn Copperhold in South Qeynos, a Backpack (17005) in
// general 23, a Rusty Dagger (7007) in general 24.
constexpr int kBagSlot = 23;
constexpr int kHome = 24;
constexpr int kBagFirst = 4010;   // general 23's first content slot (RoF2 numbering)
constexpr int kBank1 = 2000;
constexpr long long kDagger = 7007;
constexpr long long kBackpack = 17005;

class PickupRoundtrip final : public StepScenario {
public:
    explicit PickupRoundtrip(std::vector<steps::Step> s)
        : StepScenario("pickup_roundtrip", 20.0, std::move(s)) {}

protected:
    std::string Summary(IScenarioWorld& w) override {
        return "MODEL " + Slots(w, {kHome, kBagFirst, kBank1, kCursor}) +
               " -- the dagger went 24 -> cursor -> 4010 -> cursor -> 2000 -> cursor -> 24 "
               "(3 hops by command, 3 by click) and every hop held its dwell unreverted";
    }
};

std::unique_ptr<IScenario> MakePickup() {
    auto at = [](int slot, long long item) {
        return [slot, item](IScenarioWorld& w) { return w.ClientItemAt(slot) == item; };
    };
    auto onCursorOnly = [](int from) {
        return [from](IScenarioWorld& w) {
            return w.ClientItemAt(kCursor) == kDagger && w.ClientItemAt(from) == 0;
        };
    };
    auto placed = [](int to) {
        return [to](IScenarioWorld& w) {
            return w.ClientItemAt(to) == kDagger && w.CursorQueueSize() == 0;
        };
    };
    auto desc = [](IScenarioWorld& w) {
        return Slots(w, {kBagSlot, kHome, kBagFirst, kBank1, kCursor});
    };
    auto click = [](int slot) {
        return [slot](IScenarioWorld& w) {
            const int action = w.ClickSlot(slot, false, false, false);
            w.Note("click slot " + std::to_string(slot) + " -> GestureAction " +
                   std::to_string(action));
        };
    };
    const double dwell = 3.0;  // > the sidecar's 1 s DB poll, so every hop is sampled
    std::vector<steps::Step> s;
    s.push_back({"fixture: backpack in 23, dagger in 24, cursor empty", nullptr,
                 [=](IScenarioWorld& w) {
                     return w.ClientItemAt(kBagSlot) == kBackpack && w.ClientItemAt(kHome) == kDagger &&
                            w.CursorQueueSize() == 0;
                 },
                 nullptr, desc, 30.0, 0.0});
    s.push_back({"open the bank (target the banker, /bank)",
                 [](IScenarioWorld& w) {
                     w.SendCommand("/target Sorn");
                     w.SendCommand("/bank");
                 },
                 nullptr, nullptr, desc, 1.0, 2.0});
    s.push_back({"/pickup 24 -> cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 24"); },
                 onCursorOnly(kHome), nullptr, desc, 10.0, dwell});
    s.push_back({"/bag 23, /putdown 4010 -> bag", [](IScenarioWorld& w) {
                     w.SendCommand("/bag 23");
                     w.SendCommand("/putdown 4010");
                 },
                 placed(kBagFirst), nullptr, desc, 10.0, dwell});
    s.push_back({"CLICK bag slot 4010 -> cursor", click(kBagFirst), onCursorOnly(kBagFirst), nullptr,
                 desc, 10.0, dwell});
    s.push_back({"CLICK bank slot 2000 -> bank", click(kBank1), placed(kBank1), nullptr, desc, 10.0,
                 dwell});
    s.push_back({"/pickup 2000 -> cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 2000"); },
                 onCursorOnly(kBank1), nullptr, desc, 10.0, dwell});
    s.push_back({"CLICK general slot 24 -> home", click(kHome), placed(kHome), nullptr, desc, 10.0,
                 dwell});
    s.push_back({"close the bag; the dagger stays home", [](IScenarioWorld& w) { w.SendCommand("/bag 23"); },
                 at(kHome, kDagger), nullptr, desc, 5.0, 5.0});
    return std::make_unique<PickupRoundtrip>(std::move(s));
}

// ---------------------------------------------------------------------------
// click_equip_actor / click_equip_observed
// ---------------------------------------------------------------------------

// Fixture: Testbuddy carries a Backpack in general 23 holding a Rusty Dagger
// (7007) in its first slot (4010), primary EMPTY (the observer's baseline
// must be 0 -- B796's trap). MEASURED 2026-10-06: the first fixture used a
// Rusty Long Sword (5019, classes 413 -- no Magician bit), and Testbuddy is a
// Magician, so the client REFUSED the click onto primary (silently: a click
// does not echo) and the sword stayed on the cursor. The dagger (classes
// 32153) carries the Magician bit.
constexpr int kPrimary = 13;
constexpr long long kSword = 7007;
constexpr int kPrimaryMaterialSlot = 7;

std::unique_ptr<IScenario> MakeClickEquipActor() {
    auto desc = [](IScenarioWorld& w) { return Slots(w, {kBagFirst, kPrimary, kCursor}); };
    std::vector<steps::Step> s;
    s.push_back({"fixture: sword in bag slot 4010, primary empty", nullptr,
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(kBagFirst) == kSword && w.ClientItemAt(kPrimary) == 0;
                 },
                 nullptr, desc, 30.0, 0.0});
    s.push_back({"CLICK bag slot 4010 -> cursor",
                 [](IScenarioWorld& w) { w.SendCommand("/bag 23"); w.ClickSlot(kBagFirst, false, false, false); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(kCursor) == kSword; }, nullptr, desc,
                 10.0, 2.0});
    s.push_back({"CLICK primary 13 -> equipped",
                 [](IScenarioWorld& w) { w.ClickSlot(kPrimary, false, false, false); },
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(kPrimary) == kSword && w.CursorQueueSize() == 0;
                 },
                 nullptr, desc, 10.0, 0.0});
    // Stay in the zone so the observer can see a LIVE OP_WearChange if the
    // server sends one, then exit (the harness logs the character back in).
    s.push_back({"hold 40 s in the zone", nullptr,
                 [](IScenarioWorld& w) { return w.ClientItemAt(kPrimary) == kSword; }, nullptr, desc,
                 1.0, 40.0});
    return std::make_unique<StepScenario>("click_equip_actor", 25.0, std::move(s));
}

class ClickEquipObserved final : public IScenario {
public:
    const char* Name() const override { return "click_equip_observed"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const long long m = world.PeerEquipmentMaterial("", kPrimaryMaterialSlot);
        if (!seen_) {
            if (world.OtherPlayerCount() >= 1 && m >= 0) {
                seen_ = true;
                seenAt_ = tick.sinceStart;
                if (m != 0) {
                    SetReason("the peer already holds something in primary (material " +
                              std::to_string(m) + ") -- no baseline");
                    return StepResult::Fail;
                }
                world.Note("peer visible at " + std::to_string(tick.sinceStart) +
                           "s, primary material 0; watching");
            } else if (tick.sinceStart > 180.0) {
                SetReason("no peer appeared within 180 s");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        if (m < 0) {
            if (!left_) {
                left_ = true;
                world.Note("peer LEFT at " + std::to_string(tick.sinceStart) +
                           "s without any primary material reaching this client while it was here");
            }
        } else if (m > 0) {
            std::ostringstream os;
            os << "OBSERVED: the peer's primary material 0 -> " << m << ' '
               << (left_ ? "only after it logged back in (its SPAWN packet; no live OP_WearChange "
                           "was sent for the click-equip)"
                         : "while it was still in the zone (a LIVE OP_WearChange)")
               << ", " << (tick.sinceStart - seenAt_) << "s after it appeared";
            SetReason(os.str());
            world.Note(os.str());
            return StepResult::Pass;
        }
        if (tick.sinceStart - seenAt_ > 300.0) {
            SetReason(std::string("the peer's primary material never left 0 in 300 s") +
                      (left_ ? " (it left and did not come back armed)" : ""));
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

private:
    bool seen_ = false;
    bool left_ = false;
    double seenAt_ = 0.0;
};

// ---------------------------------------------------------------------------
// combine_sewing (+ the world forge)
// ---------------------------------------------------------------------------

// Fixture (combine_sewing_seed.sql): Testchar next to the South Qeynos forge
// (object 201), Collapsible Sewing Kit (17165, 8 slots) in general 23, one
// Low Quality Wolf Skin (13753) in 24, a Skinning Knife (21565) in 25, and the
// forge recipe "A Ruby" (3532, nofail) components 6027 in 26 and 12900 in 27.
constexpr long long kKit = 17165;
constexpr long long kSkin = 13753;
constexpr long long kKnife = 21565;
constexpr long long kPelt = 13782;
constexpr long long kRubyA = 6027;
constexpr long long kRubyB = 12900;
constexpr long long kRuby = 12896;
constexpr int kForgeType = 17;
constexpr int kTradeskillOpened =
    static_cast<int>(eqt::state::GameEventType::TradeskillContainerOpened);

std::unique_ptr<IScenario> MakeCombine(int tradeskillOpenedEventType) {
    auto desc = [](IScenarioWorld& w) {
        return Slots(w, {23, 24, 25, 26, 27, 4010, 4011, 4000, 4001, kCursor});
    };
    auto pickPut = [](int from, int to) {
        return [from, to](IScenarioWorld& w) {
            w.SendCommand("/pickup " + std::to_string(from));
            w.SendCommand("/putdown " + std::to_string(to));
        };
    };
    auto moved = [](int from, int to, long long item) {
        return [from, to, item](IScenarioWorld& w) {
            return w.ClientItemAt(to) == item && w.ClientItemAt(from) == 0 && w.CursorQueueSize() == 0;
        };
    };
    auto forgeOpenedBase = std::make_shared<long long>(0);
    std::vector<steps::Step> s;
    s.push_back({"fixture: kit 23, skin 24, knife 25, ruby parts 26/27", nullptr,
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(23) == kKit && w.ClientItemAt(24) == kSkin &&
                            w.ClientItemAt(25) == kKnife && w.ClientItemAt(26) == kRubyA &&
                            w.ClientItemAt(27) == kRubyB && w.CursorQueueSize() == 0;
                 },
                 nullptr, desc, 30.0, 0.0});
    s.push_back({"/bag 23 (open the kit)", [](IScenarioWorld& w) { w.SendCommand("/bag 23"); }, nullptr,
                 nullptr, desc, 1.0, 1.0});
    s.push_back({"skin 24 -> kit 4010", pickPut(24, 4010), moved(24, 4010, kSkin), nullptr, desc, 10.0,
                 2.0});
    s.push_back({"knife 25 -> kit 4011", pickPut(25, 4011), moved(25, 4011, kKnife), nullptr, desc, 10.0,
                 2.0});
    s.push_back({"/combine 23 -> kit emptied, results on the CURSOR, knife first",
                 [](IScenarioWorld& w) { w.SendCommand("/combine 23"); },
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(4010) == 0 && w.ClientItemAt(4011) == 0 &&
                            w.CursorQueueSize() >= 2 && w.ClientItemAt(kCursor) == kKnife;
                 },
                 nullptr, desc, 15.0, 2.0});
    s.push_back({"/putdown 24 -> knife home, the pelt comes to the cursor front",
                 [](IScenarioWorld& w) { w.SendCommand("/putdown 24"); },
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(24) == kKnife && w.ClientItemAt(kCursor) == kPelt &&
                            w.CursorQueueSize() == 1;
                 },
                 nullptr, desc, 10.0, 2.0});
    s.push_back({"/putdown 25 -> pelt home, cursor empty",
                 [](IScenarioWorld& w) { w.SendCommand("/putdown 25"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(25) == kPelt && w.CursorQueueSize() == 0; },
                 nullptr, desc, 10.0, 2.0});
    s.push_back({"close the kit; open the nearest forge",
                 [forgeOpenedBase, tradeskillOpenedEventType](IScenarioWorld& w) {
                     w.SendCommand("/bag 23");
                     *forgeOpenedBase = w.EventCount(tradeskillOpenedEventType);
                     const long long id = w.NearestWorldObjectId(kForgeType);
                     w.Note("nearest forge drop id " + std::to_string(id));
                     w.SendCommand("/clickobject " + std::to_string(id));
                 },
                 [forgeOpenedBase, tradeskillOpenedEventType](IScenarioWorld& w) {
                     return w.EventCount(tradeskillOpenedEventType) > *forgeOpenedBase;
                 },
                 [](IScenarioWorld& w) {
                     return w.NearestWorldObjectId(kForgeType) == 0
                                ? std::string("no forge (object type 17) is known to this client")
                                : std::string();
                 },
                 desc, 15.0, 1.0});
    s.push_back({"ruby part 26 -> forge 4000", pickPut(26, 4000), moved(26, 4000, kRubyA), nullptr, desc,
                 10.0, 2.0});
    s.push_back({"ruby part 27 -> forge 4001", pickPut(27, 4001), moved(27, 4001, kRubyB), nullptr, desc,
                 10.0, 2.0});
    s.push_back({"/combine (the world forge) -> forge emptied, the ruby on the cursor",
                 [](IScenarioWorld& w) { w.SendCommand("/combine"); },
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(4000) == 0 && w.ClientItemAt(4001) == 0 &&
                            w.ClientItemAt(kCursor) == kRuby;
                 },
                 nullptr, desc, 15.0, 2.0});
    s.push_back({"/putdown 26 -> ruby home",
                 [](IScenarioWorld& w) { w.SendCommand("/putdown 26"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(26) == kRuby && w.CursorQueueSize() == 0; },
                 nullptr, desc, 10.0, 2.0});
    s.push_back({"/closecontainer; everything stays put",
                 [](IScenarioWorld& w) { w.SendCommand("/closecontainer"); },
                 [](IScenarioWorld& w) {
                     return w.ClientItemAt(24) == kKnife && w.ClientItemAt(25) == kPelt &&
                            w.ClientItemAt(26) == kRuby && w.ClientItemAt(27) == 0;
                 },
                 nullptr, desc, 5.0, 5.0});
    class Combine final : public StepScenario {
    public:
        explicit Combine(std::vector<steps::Step> s) : StepScenario("combine_sewing", 20.0, std::move(s)) {}

    protected:
        std::string Summary(IScenarioWorld& w) override {
            return "MODEL " + Slots(w, {24, 25, 26, 27, 4010, 4011, 4000, 4001, kCursor}) +
                   " -- sewing kit: skin+knife -> knife back + Ruined Wolf Pelt, both via the "
                   "cursor queue (knife first); forge: A Ruby via the cursor";
        }
    };
    return std::make_unique<Combine>(std::move(s));
}

}  // namespace

std::unique_ptr<IScenario> MakePickupRoundtripScenario(const ScenarioOptions&) { return MakePickup(); }
std::unique_ptr<IScenario> MakeClickEquipActorScenario(const ScenarioOptions&) {
    return MakeClickEquipActor();
}
std::unique_ptr<IScenario> MakeClickEquipObservedScenario(const ScenarioOptions&) {
    return std::make_unique<ClickEquipObserved>();
}
std::unique_ptr<IScenario> MakeCombineSewingScenario(const ScenarioOptions&) {
    return MakeCombine(kTradeskillOpened);
}

}  // namespace scenario
}  // namespace eqt
