// T156: tests for the two routes that reach EQ::SkillManager::activateSkill().
//
// T155 proved on a live server that BOTH were dead: SkillActivateIntent had no
// producer at all, HotbarActivateIntent's only producer brace-initialised three
// of its four fields to zero, and EqActionHandler::useSkill() was a
// "not yet implemented" stub with no command registered against it. Every one
// of T152's five skill senders was therefore unreachable from anything a user
// or a --script probe could do.
//
// Two rules shape the tests below, both from the plan:
//
//  * The COMMAND leg outranks the UI leg, because every live probe in this
//    project is driven headlessly by --script and a mouse click is not
//    scriptable.
//  * The hotbar test must drive the REAL producer through to the REAL consumer
//    state, not construct a HotbarActivateIntent by hand — a hand-built fixture
//    would have passed against the pre-T156 code and hidden the whole defect
//    (T93's fake-substitution trap). So it runs
//        HotbarModel::assignSlot()            (real producer)
//      -> GameStateBridge event queue         (real transport)
//      -> applyHotbarSlotAssigned()           (the real renderer-side hop)
//      -> StaticUIInput::processInput()       (real intent producer)
//      -> drainIntents()                      (what eq_bridge.cpp consumes)
//    with no value copied by hand at any step.
//
// New file rather than growth of tests/test_command_processor.cpp, which is
// already past the 3000-line FAIL ceiling.

#include <gtest/gtest.h>

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/state/game_state.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/hotbar/hotbar_model.h"
#include "client/hotbar/hotbar_slot_type.h"
#include "client/skill/skill_constants.h"
#include "client/state/event_bus.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::state::GameState;

namespace {

// ===================================================================
// Leg 1 — /useskill, the scriptable route
// ===================================================================

class UseSkillCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
        dispatcher->setActionHandler(&handler);
        state.world().setZoneConnected(true);
        processor = std::make_unique<CommandProcessor>(state, *dispatcher);
    }

    GameState state;
    RecordingActionHandler handler;
    std::unique_ptr<ActionDispatcher> dispatcher;
    std::unique_ptr<CommandProcessor> processor;
};

TEST_F(UseSkillCommandTest, RegisteredInTheActionRegistry) {
    // The whole point of the unit: a command exists at all. Before T156,
    // `grep useSkill` over both command registries returned nothing.
    ASSERT_TRUE(processor->hasCommand("useskill"));
    auto info = processor->getCommandInfo("useskill");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Combat");
    // Alias, so `/skill taunt` works too.
    EXPECT_TRUE(processor->hasCommand("skill"));
}

TEST_F(UseSkillCommandTest, NumericIdReachesTheHandler) {
    auto r = processor->processCommand("/useskill 9");  // Bind Wound
    EXPECT_TRUE(r.success) << r.message;
    ASSERT_EQ(handler.calls.size(), 1u);
    EXPECT_EQ(handler.calls[0], "useSkill(9)");
}

TEST_F(UseSkillCommandTest, SkillNameResolvesToItsId) {
    // /useskill 9 is the "technically wired, practically unreachable"
    // interface this arc keeps deleting; the name form is the usable one.
    auto r = processor->processCommand("/useskill taunt");
    EXPECT_TRUE(r.success) << r.message;
    ASSERT_EQ(handler.calls.size(), 1u);
    EXPECT_EQ(handler.calls[0], "useSkill(73)");
}

TEST_F(UseSkillCommandTest, MultiWordSkillNameResolves) {
    auto r = processor->processCommand("/useskill Bind Wound");
    EXPECT_TRUE(r.success) << r.message;
    ASSERT_EQ(handler.calls.size(), 1u);
    EXPECT_EQ(handler.calls[0], "useSkill(9)");
}

TEST_F(UseSkillCommandTest, SkillNameIsPunctuationAndCaseInsensitive) {
    EXPECT_TRUE(processor->processCommand("/useskill applypoison").success);
    EXPECT_TRUE(processor->processCommand("/useskill Apply-Poison").success);
    EXPECT_TRUE(processor->processCommand("/useskill PICK POCKET").success);
    ASSERT_EQ(handler.calls.size(), 3u);
    EXPECT_EQ(handler.calls[0], "useSkill(6)");   // ApplyPoison
    EXPECT_EQ(handler.calls[1], "useSkill(6)");
    EXPECT_EQ(handler.calls[2], "useSkill(48)");  // PickPocket
}

TEST_F(UseSkillCommandTest, AliasWorks) {
    EXPECT_TRUE(processor->processCommand("/skill disarm").success);
    ASSERT_EQ(handler.calls.size(), 1u);
    EXPECT_EQ(handler.calls[0], "useSkill(16)");
}

TEST_F(UseSkillCommandTest, AllFiveT152SkillsAreNowReachableByName) {
    // The five senders T152 fixed and T155 could not probe. ApplyPoison,
    // Disarm and PickPocket were the three BLOCKED rows in T155's matrix.
    const std::pair<const char*, unsigned> cases[] = {
        {"apply poison", 6}, {"bind wound", 9}, {"disarm", 16},
        {"pick pocket", 48}, {"taunt", 73},
    };
    for (const auto& c : cases) {
        handler.calls.clear();
        auto r = processor->processCommand(std::string("/useskill ") + c.first);
        EXPECT_TRUE(r.success) << c.first << ": " << r.message;
        ASSERT_EQ(handler.calls.size(), 1u) << c.first;
        EXPECT_EQ(handler.calls[0], "useSkill(" + std::to_string(c.second) + ")") << c.first;
    }
}

TEST_F(UseSkillCommandTest, OutOfRangeIdIsRejectedWithoutDispatch) {
    // T95: an unvalidated int must never become a skill index.
    auto r = processor->processCommand("/useskill 200");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("Unknown skill id"), std::string::npos) << r.message;
    EXPECT_TRUE(handler.calls.empty());
}

TEST_F(UseSkillCommandTest, IdInsideTheTableButUnnamedIsRejected) {
    // 75..99 are inside MAX_PP_SKILL but have no name and no meaning.
    auto r = processor->processCommand("/useskill 90");
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(handler.calls.empty());
}

TEST_F(UseSkillCommandTest, UnknownNameIsRejectedWithoutDispatch) {
    auto r = processor->processCommand("/useskill flimflam");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("Unknown skill"), std::string::npos) << r.message;
    EXPECT_TRUE(handler.calls.empty());
}

TEST_F(UseSkillCommandTest, NonActivatableSkillIsRejectedWithoutDispatch) {
    // "Defense" (15) is a passive; activating it is meaningless and the
    // server would reject it. Caught before the packet, with a real message.
    auto r = processor->processCommand("/useskill defense");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("not a skill you can activate"), std::string::npos) << r.message;
    EXPECT_TRUE(handler.calls.empty());
}

TEST_F(UseSkillCommandTest, NoArgumentPrintsUsage) {
    auto r = processor->processCommand("/useskill");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("Usage:"), std::string::npos) << r.message;
    EXPECT_TRUE(handler.calls.empty());
}

TEST_F(UseSkillCommandTest, RequiresZoneConnection) {
    state.world().setZoneConnected(false);
    auto r = processor->processCommand("/useskill taunt");
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(handler.calls.empty());
}

// ===================================================================
// Leg 2 — the hotbar, end to end, and the numbering
// ===================================================================

// Trivial concrete GameStateBridge: pushEvent/pushIntent/drainIntents are all
// concrete on the base, so no renderer and no Irrlicht device is needed. Same
// idiom as test_static_ui_panels.cpp's TestBridge.
class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    // applyEvent is never reached in these tests; the renderer side here is
    // drainEvents() + applyHotbarSlotAssigned(), exactly as IrrlichtBridge
    // does it in production.
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class HotbarReachabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        layout.computeLayout(800, 600);
        model.setBridge(&bridge);
        input.init(&bridge);
    }

    // Run the real chain for one slot assignment, with nothing copied by hand.
    void assignAndPump(int slot, eqt::HotbarSlotType type, uint32_t id,
                       const std::string& emoteText = "") {
        model.assignSlot({0, slot}, type, id, emoteText);
        pumpEvents();
    }

    // HotbarModel pushed real GameEvents; drain them off the real bridge queue
    // and feed each through the same free function IrrlichtBridge's event
    // switch calls in production.
    void pumpEvents() {
        for (const auto& ev : bridge.drainEvents()) {
            if (ev.type != eqt::state::GameEventType::HotbarSlotAssigned) continue;
            EQT::Graphics::applyHotbarSlotAssigned(
                hotbar, std::get<eqt::state::HotbarSlotAssignedData>(ev.data));
        }
    }

    // Click the centre of hotbar slot `slot` (800x600 layout: hotbar starts at
    // x=222, y=556, slots 32px wide with a 4px margin).
    void clickSlot(int slot) {
        const int x = 222 + slot * (EQT::Graphics::UILayout::SLOT_SIZE +
                                    EQT::Graphics::UILayout::MARGIN) + 4;
        input.processInput(x, 570, /*leftClicked=*/true, layout, hotbar, gems,
                           inventory, spellbook, skills, buffs, loot);
    }

    std::vector<eqt::events::RendererIntent> drain() { return bridge.drainIntents(); }

    CapturingBridge bridge;
    eqt::HotbarModel model;
    EQT::Graphics::UILayout layout;
    EQT::Graphics::HotbarPanelState hotbar;
    EQT::Graphics::SpellGemPanelState gems;
    EQT::Graphics::InventoryPanelState inventory;
    EQT::Graphics::SpellbookPopupState spellbook;
    EQT::Graphics::SkillsPopupState skills;
    EQT::Graphics::BuffBarState buffs;
    EQT::Graphics::LootPanelState loot;
    EQT::Graphics::StaticUIInput input;
};

// THE test that fails against the old numbering.
//
// Before T156 this fails three separate ways, any one of which is fatal:
//   * the producer pushed HotbarActivateIntent{slot} — id was 0, not 73;
//   * buttonType was 0, so the consumer took the Spell arm;
//   * even had the raw type been passed through, the consumer's numbering
//     (0=Spell, 1=Item, 2=Emote, 3=Skill) reads HotbarSlotType::Skill (2)
//     as Emote and HotbarSlotType::Item (3) as Skill.
// The intent now carries eqt::HotbarSlotType itself, so "which numbering"
// is not a question the consumer can get wrong.
TEST_F(HotbarReachabilityTest, SkillSlotProducesASkillIntentCarryingTheSkillId) {
    assignAndPump(3, eqt::HotbarSlotType::Skill, /*Taunt*/ 73);
    clickSlot(3);

    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* hb = std::get_if<eqt::events::HotbarActivateIntent>(&intents[0]);
    ASSERT_NE(hb, nullptr) << "expected a HotbarActivateIntent";
    EXPECT_EQ(hb->index, 3);
    EXPECT_EQ(hb->type, eqt::HotbarSlotType::Skill);
    EXPECT_EQ(hb->id, 73u);
}

// The mirror image: Item is 3 in the surviving numbering and 3 was the
// consumer's OLD "Skill" case. If anything ever reverts to raw ints, this
// pins which side the drift landed on.
TEST_F(HotbarReachabilityTest, ItemSlotIsNotMistakenForASkill) {
    assignAndPump(0, eqt::HotbarSlotType::Item, 21779);
    clickSlot(0);

    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* hb = std::get_if<eqt::events::HotbarActivateIntent>(&intents[0]);
    ASSERT_NE(hb, nullptr);
    EXPECT_EQ(hb->type, eqt::HotbarSlotType::Item);
    EXPECT_NE(hb->type, eqt::HotbarSlotType::Skill);
    EXPECT_EQ(hb->id, 21779u);
}

TEST_F(HotbarReachabilityTest, SpellSlotCarriesItsSpellId) {
    assignAndPump(9, eqt::HotbarSlotType::Spell, 278);
    clickSlot(9);

    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* hb = std::get_if<eqt::events::HotbarActivateIntent>(&intents[0]);
    ASSERT_NE(hb, nullptr);
    EXPECT_EQ(hb->index, 9);
    EXPECT_EQ(hb->type, eqt::HotbarSlotType::Spell);
    EXPECT_EQ(hb->id, 278u);
}

TEST_F(HotbarReachabilityTest, EmoteSlotCarriesItsText) {
    // emoteText was the third field the old brace-init dropped; without it
    // the Emote arm was a no-op even if it had been reachable.
    assignAndPump(1, eqt::HotbarSlotType::Emote, 0, "/wave");
    clickSlot(1);

    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* hb = std::get_if<eqt::events::HotbarActivateIntent>(&intents[0]);
    ASSERT_NE(hb, nullptr);
    EXPECT_EQ(hb->type, eqt::HotbarSlotType::Emote);
    EXPECT_EQ(hb->emoteText, "/wave");
}

TEST_F(HotbarReachabilityTest, EmptySlotPushesNothing) {
    clickSlot(5);
    EXPECT_TRUE(drain().empty());
}

TEST_F(HotbarReachabilityTest, ClearingASlotStopsItActivating) {
    assignAndPump(2, eqt::HotbarSlotType::Skill, 16);
    model.clearSlot({0, 2});
    pumpEvents();
    clickSlot(2);
    EXPECT_TRUE(drain().empty());
}

// The published event itself carries the enum, so the numbering cannot be
// reinterpreted between HotbarModel and the renderer either.
TEST_F(HotbarReachabilityTest, PublishedEventCarriesTheTypedEnumAndTheId) {
    model.assignSlot({0, 4}, eqt::HotbarSlotType::Skill, 6);
    auto events = bridge.drainEvents();
    ASSERT_FALSE(events.empty());
    const auto& d = std::get<eqt::state::HotbarSlotAssignedData>(events[0].data);
    EXPECT_EQ(d.page, 0);
    EXPECT_EQ(d.index, 4);
    EXPECT_EQ(d.type, eqt::HotbarSlotType::Skill);
    EXPECT_EQ(d.id, 6u);
}

// T95: a config file holding a type the enum does not have must not become a
// bogus enumerator. The real loadFromJson is the producer here.
TEST_F(HotbarReachabilityTest, OutOfRangeConfigTypeClearsTheSlotInsteadOfCasting) {
    Json::Value cfg;
    Json::Value buttons(Json::arrayValue);
    Json::Value bad;
    bad["type"] = 99;
    bad["id"] = 73;
    buttons.append(bad);
    cfg["buttons"] = buttons;

    model.loadFromJson(cfg);
    EXPECT_EQ(model.getSlot({0, 0}).type, eqt::HotbarSlotType::Empty);
    EXPECT_EQ(model.getSlot({0, 0}).id, 0u);
}

TEST_F(HotbarReachabilityTest, ValidConfigTypeRoundTripsThroughJson) {
    Json::Value cfg;
    Json::Value buttons(Json::arrayValue);
    Json::Value btn;
    btn["type"] = static_cast<int>(eqt::HotbarSlotType::Skill);
    btn["id"] = 48;
    buttons.append(btn);
    cfg["buttons"] = buttons;

    model.loadFromJson(cfg);
    EXPECT_EQ(model.getSlot({0, 0}).type, eqt::HotbarSlotType::Skill);
    EXPECT_EQ(model.getSlot({0, 0}).id, 48u);
    EXPECT_EQ(model.getSlot({1, 0}).type, eqt::HotbarSlotType::Empty);
}

TEST_F(HotbarReachabilityTest, LegacyEleventhButtonIsIgnored) {
    Json::Value config;
    Json::Value buttons(Json::arrayValue);
    for (int i = 0; i < 11; ++i) {
        Json::Value button;
        button["type"] = static_cast<int>(eqt::HotbarSlotType::Skill);
        button["id"] = i + 1;
        buttons.append(button);
    }
    config["buttons"] = buttons;
    model.loadFromJson(config);
    EXPECT_EQ(model.getSlot({0, 9}).id, 10u);
    EXPECT_EQ(model.getSlot({0, 10}).type, eqt::HotbarSlotType::Empty);
    EXPECT_EQ(model.getSlot({1, 0}).type, eqt::HotbarSlotType::Empty);
}

TEST_F(HotbarReachabilityTest, PageAddressRoundTripsAndPublishesActivePage) {
    model.assignSlot({3, 7}, eqt::HotbarSlotType::Skill, 48);
    auto assigned = bridge.drainEvents();
    ASSERT_EQ(assigned.size(), 1u);
    const auto& assignedData = std::get<eqt::state::HotbarSlotAssignedData>(assigned[0].data);
    EXPECT_EQ(assignedData.page, 3);
    EXPECT_EQ(assignedData.currentPage, 0);
    EQT::Graphics::HotbarPanelState panel;
    EQT::Graphics::applyHotbarSlotAssigned(panel, assignedData);
    EXPECT_EQ(panel.slots[7].type, eqt::HotbarSlotType::Empty);
    model.setCurrentPage(3);
    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 10u);
    const auto& d = std::get<eqt::state::HotbarSlotAssignedData>(events[7].data);
    EXPECT_EQ(d.page, 3);
    EXPECT_EQ(d.index, 7);
    EXPECT_EQ(d.id, 48u);

    eqt::HotbarModel loaded;
    loaded.loadFromJson(model.saveToJson());
    EXPECT_EQ(loaded.currentPage(), 3);
    EXPECT_EQ(loaded.getSlot({3, 7}).id, 48u);
    EXPECT_EQ(loaded.getSlot({0, 7}).type, eqt::HotbarSlotType::Empty);
    loaded.setCurrentPage(10);
    EXPECT_EQ(loaded.currentPage(), 3);
    loaded.assignSlot({10, 7}, eqt::HotbarSlotType::Item, 999);
    EXPECT_EQ(loaded.getSlot({3, 7}).id, 48u);
}

// ===================================================================
// Leg 3 — SkillActivateIntent finally has a producer
// ===================================================================

class SkillsPopupProducerTest : public ::testing::Test {
protected:
    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        skills.isOpen = true;
        // Rows as renderSkillsPopup() draws them.
        skills.skills = {
            {6,  "Apply Poison", 10, 200},
            {16, "Disarm",       25, 200},
            {73, "Taunt",        50, 200},
        };
    }

    // Click row `row` of the popup. centerPopup for 800x600 is
    // {200,125,600,475}; rows start at popupY+24 with an 18px line and 2px pad.
    void clickRow(int row) {
        const int y = 125 + 24 + row * (18 + 2) + 4;
        input.processInput(300, y, /*leftClicked=*/true, layout, hotbar, gems,
                           inventory, spellbook, skills, buffs, loot);
    }

    CapturingBridge bridge;
    EQT::Graphics::UILayout layout;
    EQT::Graphics::HotbarPanelState hotbar;
    EQT::Graphics::SpellGemPanelState gems;
    EQT::Graphics::InventoryPanelState inventory;
    EQT::Graphics::SpellbookPopupState spellbook;
    EQT::Graphics::SkillsPopupState skills;
    EQT::Graphics::BuffBarState buffs;
    EQT::Graphics::LootPanelState loot;
    EQT::Graphics::StaticUIInput input;
};

TEST_F(SkillsPopupProducerTest, ClickingASkillRowProducesSkillActivateIntent) {
    // Before T156, SkillActivateIntent appeared four times in the whole tree
    // — the consumer, the struct, the variant entry and a doc line. This is
    // its first producer.
    clickRow(2);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* si = std::get_if<eqt::events::SkillActivateIntent>(&intents[0]);
    ASSERT_NE(si, nullptr) << "expected a SkillActivateIntent";
    EXPECT_EQ(si->skillId, 73u);  // Taunt, the third row
}

TEST_F(SkillsPopupProducerTest, RowIsRelativeToTheScrollOffset) {
    skills.scrollOffset = 1;
    clickRow(0);  // first VISIBLE row is skills[1]
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* si = std::get_if<eqt::events::SkillActivateIntent>(&intents[0]);
    ASSERT_NE(si, nullptr);
    EXPECT_EQ(si->skillId, 16u);  // Disarm
}

TEST_F(SkillsPopupProducerTest, ClickingPastTheLastSkillProducesNothing) {
    clickRow(7);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(SkillsPopupProducerTest, ClosedPopupProducesNothing) {
    skills.isOpen = false;
    clickRow(0);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(SkillsPopupProducerTest, HoverWithoutClickProducesNothing) {
    const int y = 125 + 24 + 4;
    input.processInput(300, y, /*leftClicked=*/false, layout, hotbar, gems,
                       inventory, spellbook, skills, buffs, loot);
    EXPECT_EQ(skills.hoveredRow, 0);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

} // namespace
