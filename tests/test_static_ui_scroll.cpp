/**
 * T313 -- the static UI had no scroll INPUT at all.
 *
 * Three panels carried a `scrollOffset` that BOTH their render body and
 * their hit test honoured, and nothing anywhere ever wrote one except a
 * clamp or a reset:
 *
 *   chat   ChatPanelState::scrollOffset   -- only renderChatPanel()'s clamp
 *   skills SkillsPopupState::scrollOffset -- nothing at all
 *   loot   LootPanelState::scrollOffset   -- only open()'s reset and the
 *                                            remove-item clamp
 *
 * StaticUIInput::processInput() took `mouseX/mouseY/leftClicked` and no
 * wheel, RendererEventReceiver::OnEvent() had no EMIE_MOUSE_WHEEL case at
 * all, and no key was bound to a page action. So chat scrollback was
 * unreachable, the skills window showed only its first page, and -- the
 * user-visible one -- a corpse holding more than
 * LootPanelState::VISIBLE_ROWS items was PARTLY UNLOOTABLE: rows 9+ could
 * neither be drawn nor hit-tested, and the only way to get them was
 * Loot All.
 *
 * What is pinned here:
 *   Leg A  the router itself (scrollTargetAt / scrollPanel / processWheel /
 *          processScrollKey) -- the real production functions, no device.
 *   Leg B  per panel, that the ROW SET actually changed: for loot and
 *          skills through their own production hit tests (the twins of the
 *          render loops) and the real intents that come out of a click on a
 *          row that did not exist before the scroll; for chat, through
 *          renderChatPanel() itself into a real headless GLES2
 *          framebuffer (test_ui_atlas_pixels.cpp's harness), because the
 *          wrapped-line window it scrolls exists nowhere but inside that
 *          function.
 *   Leg C  the three reachable entry points -- the wheel, the
 *          PageUp/PageDown binding, and `/scroll` (the scriptable half, for
 *          the same reason /hideui exists: T301 Leg D).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <cstdlib>
#include <string>
#include <vector>

#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/chat/chat_message_buffer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/state/event_bus.h"
#include "client/input/hotkey_manager.h"

#include <irrlicht.h>

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;
const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// The whole scrollable-panel set, so every test drives the same production
// signature the renderer does.
struct Panels {
    UILayout layout;
    ChatPanelState chat;
    SkillsPopupState skills;
    LootPanelState loot;
    Panels() { layout.computeLayout(800, 600); }
};

// Centre of the chat panel / of the centre popup, at 800x600.
int chatX(const UILayout& l) { return (l.chatPanel.UpperLeftCorner.X + l.chatPanel.LowerRightCorner.X) / 2; }
int chatY(const UILayout& l) { return (l.chatPanel.UpperLeftCorner.Y + l.chatPanel.LowerRightCorner.Y) / 2; }
int popX(const UILayout& l) { return (l.centerPopup.UpperLeftCorner.X + l.centerPopup.LowerRightCorner.X) / 2; }
int popY(const UILayout& l) { return (l.centerPopup.UpperLeftCorner.Y + l.centerPopup.LowerRightCorner.Y) / 2; }

}  // namespace

// ===========================================================================
// Leg A -- the router
// ===========================================================================

class ScrollRouterTest : public ::testing::Test {
protected:
    Panels p;
    StaticUIInput input;
};

TEST_F(ScrollRouterTest, NothingUnderTheCursorIsNoTarget) {
    // Dead centre-top of the screen: no popup open, not the chat box.
    EXPECT_EQ(StaticUIInput::scrollTargetAt(400, 20, p.layout, p.skills, p.loot),
              ScrollTarget::None);
}

TEST_F(ScrollRouterTest, ChatPanelIsATargetAnywhereInsideIt) {
    EXPECT_EQ(StaticUIInput::scrollTargetAt(chatX(p.layout), chatY(p.layout),
                                            p.layout, p.skills, p.loot),
              ScrollTarget::Chat);
    // The tab strip at the very top of the box counts too -- a dead band
    // there is exactly the kind of thing the user reads as "scroll broken".
    EXPECT_EQ(StaticUIInput::scrollTargetAt(chatX(p.layout),
                                            p.layout.chatTabs.UpperLeftCorner.Y + 1,
                                            p.layout, p.skills, p.loot),
              ScrollTarget::Chat);
}

TEST_F(ScrollRouterTest, ClosedPopupsAreNotTargets) {
    EXPECT_EQ(StaticUIInput::scrollTargetAt(popX(p.layout), popY(p.layout),
                                            p.layout, p.skills, p.loot),
              ScrollTarget::None);
}

TEST_F(ScrollRouterTest, OpenSkillsPopupTakesTheCentre) {
    p.skills.isOpen = true;
    EXPECT_EQ(StaticUIInput::scrollTargetAt(popX(p.layout), popY(p.layout),
                                            p.layout, p.skills, p.loot),
              ScrollTarget::Skills);
}

TEST_F(ScrollRouterTest, LootWinsOverSkillsBecauseItIsDrawnOnTop) {
    p.skills.isOpen = true;
    p.loot.isOpen = true;
    EXPECT_EQ(StaticUIInput::scrollTargetAt(popX(p.layout), popY(p.layout),
                                            p.layout, p.skills, p.loot),
              ScrollTarget::Loot);
}

TEST_F(ScrollRouterTest, ChatOffsetIsMeasuredFromTheBottomSoUpIsPositive) {
    // The conventions are deliberately NOT harmonised: chat counts lines up
    // from the newest, the two lists count rows down from the first.
    StaticUIInput::scrollPanel(ScrollTarget::Chat, 5, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.chat.scrollOffset, 5);
    StaticUIInput::scrollPanel(ScrollTarget::Chat, -2, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.chat.scrollOffset, 3);
}

TEST_F(ScrollRouterTest, ChatCannotScrollBelowTheNewestLine) {
    StaticUIInput::scrollPanel(ScrollTarget::Chat, -50, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.chat.scrollOffset, 0);
}

TEST_F(ScrollRouterTest, ListOffsetsAreMeasuredFromTheTopSoUpIsNegative) {
    p.loot.isOpen = true;
    for (int i = 0; i < 20; ++i) {
        p.loot.items.push_back({static_cast<uint8_t>(i), 1000u + i, "item"});
    }
    p.loot.scrollOffset = 10;
    StaticUIInput::scrollPanel(ScrollTarget::Loot, 3, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.loot.scrollOffset, 7) << "wheel-up must move TOWARDS the first row";
    StaticUIInput::scrollPanel(ScrollTarget::Loot, -3, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.loot.scrollOffset, 10);
}

TEST_F(ScrollRouterTest, LootScrollSaturatesAtTheExistingMaxScrollOffset) {
    p.loot.isOpen = true;
    for (int i = 0; i < 12; ++i) {
        p.loot.items.push_back({static_cast<uint8_t>(i), 1000u + i, "item"});
    }
    // 12 items, 8 visible rows -> the panel's own helper says 4.
    ASSERT_EQ(p.loot.maxScrollOffset(), 4);
    StaticUIInput::scrollPanel(ScrollTarget::Loot, -999, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.loot.scrollOffset, 4) << "the writer must ride the panel's clamp, not past it";
    StaticUIInput::scrollPanel(ScrollTarget::Loot, 999, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.loot.scrollOffset, 0);
}

TEST_F(ScrollRouterTest, SkillsScrollSaturatesAtItsOwnMax) {
    p.skills.isOpen = true;
    const int visible = skillsVisibleRows(p.layout);
    ASSERT_GT(visible, 0);
    for (int i = 0; i < visible + 7; ++i) {
        p.skills.skills.push_back({static_cast<uint8_t>(i), "skill", 10, 200});
    }
    ASSERT_EQ(p.skills.maxScrollOffset(visible), 7);
    StaticUIInput::scrollPanel(ScrollTarget::Skills, -999, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.skills.scrollOffset, 7);
}

TEST_F(ScrollRouterTest, ShortListsDoNotScrollAtAll) {
    p.loot.isOpen = true;
    p.loot.items.push_back({0, 1001, "a"});
    StaticUIInput::scrollPanel(ScrollTarget::Loot, -5, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.loot.scrollOffset, 0);
}

TEST_F(ScrollRouterTest, NoneTargetWritesNothingAndReportsFalse) {
    p.chat.scrollOffset = 4;
    EXPECT_FALSE(StaticUIInput::scrollPanel(ScrollTarget::None, 3, p.layout,
                                            p.chat, p.skills, p.loot));
    EXPECT_EQ(p.chat.scrollOffset, 4);
}

// ===========================================================================
// Leg C (part 1) -- the wheel and the page keys, through the router
// ===========================================================================

TEST_F(ScrollRouterTest, WheelOverTheChatBoxScrollsChatBackwards) {
    EXPECT_TRUE(input.processWheel(chatX(p.layout), chatY(p.layout), 1.0f,
                                   p.layout, p.chat, p.skills, p.loot));
    EXPECT_EQ(p.chat.scrollOffset, StaticUIInput::WHEEL_LINES);
}

TEST_F(ScrollRouterTest, WheelOverEmptyScreenIsNotConsumed) {
    EXPECT_FALSE(input.processWheel(400, 20, 1.0f, p.layout, p.chat, p.skills, p.loot));
    EXPECT_EQ(p.chat.scrollOffset, 0);
}

TEST_F(ScrollRouterTest, ZeroWheelIsNeverConsumed) {
    EXPECT_FALSE(input.processWheel(chatX(p.layout), chatY(p.layout), 0.0f,
                                    p.layout, p.chat, p.skills, p.loot));
}

TEST_F(ScrollRouterTest, SeveralNotchesInOneFrameAllCount) {
    input.processWheel(chatX(p.layout), chatY(p.layout), 3.0f,
                       p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.chat.scrollOffset, 3 * StaticUIInput::WHEEL_LINES);
}

TEST_F(ScrollRouterTest, PageKeyMovesAWholePage) {
    p.loot.isOpen = true;
    for (int i = 0; i < 40; ++i) {
        p.loot.items.push_back({static_cast<uint8_t>(i), 1000u + i, "item"});
    }
    EXPECT_TRUE(input.processScrollKey(/*up=*/false, popX(p.layout), popY(p.layout),
                                       p.layout, p.chat, p.skills, p.loot));
    EXPECT_EQ(p.loot.scrollOffset, LootPanelState::VISIBLE_ROWS);
}

TEST_F(ScrollRouterTest, PageKeyFindsTheOpenPopupEvenWithTheCursorAtTheOrigin) {
    // A --script run never moves the pointer; requiring it to be inside the
    // popup would make the keyboard path useless to exactly the harnesses it
    // exists for.
    p.skills.isOpen = true;
    for (int i = 0; i < 40; ++i) {
        p.skills.skills.push_back({static_cast<uint8_t>(i), "skill", 10, 200});
    }
    EXPECT_TRUE(input.processScrollKey(/*up=*/false, 0, 0,
                                       p.layout, p.chat, p.skills, p.loot));
    EXPECT_EQ(p.skills.scrollOffset, skillsVisibleRows(p.layout));
}

TEST_F(ScrollRouterTest, PageKeyWithNothingOpenFallsBackToChat) {
    EXPECT_TRUE(input.processScrollKey(/*up=*/true, 0, 0,
                                       p.layout, p.chat, p.skills, p.loot));
    EXPECT_EQ(p.chat.scrollOffset, chatVisibleLines(p.layout));
}

// ===========================================================================
// Leg B -- the loot panel: a row that was UNREACHABLE becomes lootable
// ===========================================================================

class LootScrollTest : public ::testing::Test {
protected:
    void SetUp() override {
        input.init(&bridge);
        p.loot.isOpen = true;
        p.loot.corpseId = 4242;
        p.loot.corpseName = "a decaying skeleton";
        // 14 items on a corpse that shows 8 rows: corpse slots 0..13.
        for (int i = 0; i < 14; ++i) {
            p.loot.items.push_back({static_cast<uint8_t>(i), 5000u + i,
                                    "item " + std::to_string(i)});
        }
    }

    // Click the Nth VISIBLE row. Geometry from loot_panel.cpp: title 20 +
    // pad 8 + button 16 + pad 8 below the popup top, rows 22 high with a
    // 2px gap.
    void clickVisibleRow(int row) {
        const int y = p.layout.centerPopup.UpperLeftCorner.Y + 20 + 8 + 16 + 8
                      + row * (22 + 2) + 4;
        input.processInput(popX(p.layout), y, /*leftClicked=*/true, p.layout,
                           hotbar, gems, inventory, spellbook, p.skills, buffs, p.loot);
    }

    std::vector<uint8_t> lootedSlots() {
        std::vector<uint8_t> out;
        for (auto& i : bridge.drainIntents()) {
            if (const auto* li = std::get_if<eqt::events::LootItemIntent>(&i)) {
                out.push_back(li->slot);
            }
        }
        return out;
    }

    Panels p;
    CapturingBridge bridge;
    StaticUIInput input;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    BuffBarState buffs;
};

TEST_F(LootScrollTest, TheDefect_CorpseSlot13IsUnreachableBeforeAnyScroll) {
    // Every visible row, unscrolled. Rows 8..13 simply are not drawn and
    // hitTestLootRow() cannot return them -- that is the bug: those items
    // could only be taken with Loot All.
    for (int row = 0; row < LootPanelState::VISIBLE_ROWS; ++row) {
        clickVisibleRow(row);
    }
    auto slots = lootedSlots();
    ASSERT_EQ(slots.size(), static_cast<size_t>(LootPanelState::VISIBLE_ROWS));
    for (auto s : slots) {
        EXPECT_LT(s, LootPanelState::VISIBLE_ROWS)
            << "an unscrolled loot window must only reach its first page";
    }
}

TEST_F(LootScrollTest, TheFix_ScrollingBringsSlot13OntoARowAndLootsIt) {
    // 14 items, 8 rows -> maxScrollOffset() == 6, so the last page shows
    // corpse slots 6..13 and slot 13 lands on the LAST visible row.
    ASSERT_EQ(p.loot.maxScrollOffset(), 6);
    ASSERT_TRUE(input.processWheel(popX(p.layout), popY(p.layout), -2.0f,
                                   p.layout, p.chat, p.skills, p.loot));
    ASSERT_EQ(p.loot.scrollOffset, 6) << "two notches of 3 lines, clamped at the end";

    clickVisibleRow(LootPanelState::VISIBLE_ROWS - 1);
    auto slots = lootedSlots();
    ASSERT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots[0], 13) << "the row that no click could reach before T313";
}

TEST_F(LootScrollTest, TheVisibleRowSetChanges) {
    // The row set is what hitTestLootRow() -- renderLootPopup()'s twin --
    // says is on screen. Before and after, top row and bottom row.
    auto rowItem = [&](int row) {
        const int y = p.layout.centerPopup.UpperLeftCorner.Y + 20 + 8 + 16 + 8
                      + row * (22 + 2) + 4;
        return hitTestLootRow(p.layout, p.loot, popX(p.layout), y);
    };
    EXPECT_EQ(rowItem(0), 0);
    EXPECT_EQ(rowItem(7), 7);

    StaticUIInput::scrollPanel(ScrollTarget::Loot, -4, p.layout, p.chat, p.skills, p.loot);

    EXPECT_EQ(rowItem(0), 4);
    EXPECT_EQ(rowItem(7), 11);
}

TEST_F(LootScrollTest, ScrollingClearsTheStaleHoveredRow) {
    p.loot.hoveredRow = 3;
    StaticUIInput::scrollPanel(ScrollTarget::Loot, -2, p.layout, p.chat, p.skills, p.loot);
    EXPECT_EQ(p.loot.hoveredRow, -1);
}

TEST_F(LootScrollTest, ClosingAndReopeningStillResetsToTheTop) {
    StaticUIInput::scrollPanel(ScrollTarget::Loot, -6, p.layout, p.chat, p.skills, p.loot);
    ASSERT_EQ(p.loot.scrollOffset, 6);
    eqt::state::LootWindowOpenedData opened;
    opened.corpseId = 99;
    opened.corpseName = "another corpse";
    applyLootWindowOpened(p.loot, opened);
    EXPECT_EQ(p.loot.scrollOffset, 0) << "the panel's own reset must still win";
}

// ===========================================================================
// Leg B -- the skills popup
// ===========================================================================

class SkillsScrollTest : public ::testing::Test {
protected:
    void SetUp() override {
        input.init(&bridge);
        p.skills.isOpen = true;
        visible = skillsVisibleRows(p.layout);
        // One more than a page, so the last skill starts off screen.
        for (int i = 0; i < visible + 5; ++i) {
            p.skills.skills.push_back(
                {static_cast<uint8_t>(i), "skill " + std::to_string(i), 10, 200});
        }
    }

    void clickVisibleRow(int row) {
        const int y = p.layout.centerPopup.UpperLeftCorner.Y + 24
                      + row * (SkillsPopupState::LINE_H + SkillsPopupState::ROW_PAD) + 4;
        input.processInput(popX(p.layout), y, /*leftClicked=*/true, p.layout,
                           hotbar, gems, inventory, spellbook, p.skills, buffs, p.loot);
    }

    std::vector<uint8_t> activated() {
        std::vector<uint8_t> out;
        for (auto& i : bridge.drainIntents()) {
            if (const auto* si = std::get_if<eqt::events::SkillActivateIntent>(&i)) {
                out.push_back(si->skillId);
            }
        }
        return out;
    }

    Panels p;
    CapturingBridge bridge;
    StaticUIInput input;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    BuffBarState buffs;
    int visible = 0;
};

TEST_F(SkillsScrollTest, TheDefect_TheLastSkillIsUnreachableUnscrolled) {
    for (int row = 0; row < visible; ++row) clickVisibleRow(row);
    auto ids = activated();
    ASSERT_EQ(ids.size(), static_cast<size_t>(visible));
    for (auto id : ids) EXPECT_LT(id, visible);
}

TEST_F(SkillsScrollTest, TheFix_ScrollingReachesTheLastSkill) {
    ASSERT_TRUE(input.processScrollKey(/*up=*/false, popX(p.layout), popY(p.layout),
                                       p.layout, p.chat, p.skills, p.loot));
    EXPECT_EQ(p.skills.scrollOffset, 5) << "one page down, clamped at the end of the list";
    clickVisibleRow(visible - 1);
    auto ids = activated();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], static_cast<uint8_t>(visible + 4)) << "the last skill in the list";
}

TEST_F(SkillsScrollTest, TheVisibleRowSetChanges) {
    clickVisibleRow(0);
    auto before = activated();
    ASSERT_EQ(before.size(), 1u);
    EXPECT_EQ(before[0], 0);

    StaticUIInput::scrollPanel(ScrollTarget::Skills, -3, p.layout, p.chat, p.skills, p.loot);

    clickVisibleRow(0);
    auto after = activated();
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0], 3) << "the same screen row is a different skill after a scroll";
}

TEST_F(SkillsScrollTest, TheRowHitTestNeverReachesPastTheDRAWNWindow) {
    // Pre-T313 the hit test looped over a hard-coded 20 rows while the popup
    // only ever DRAWS skillsVisibleRows() of them, so a click low in the
    // popup could activate a skill that was not on screen.
    clickVisibleRow(visible);      // the first row past the drawn window
    EXPECT_TRUE(activated().empty());
}

// ===========================================================================
// Leg B -- the chat panel, through the real render body
// ===========================================================================
//
// Chat is the one panel whose scroll window cannot be asserted without
// rendering: renderChatPanel() word-wraps the message buffer at draw time
// and the wrapped-line count exists nowhere else. Same headless GLES2
// harness as test_ui_atlas_pixels.cpp; self-skips without the EQE1 asset or
// a usable device.

// Set WILLEQ_T313_CAPTURE_DIR to have the pixel legs below also write their
// framebuffers out as PNGs -- that is how review/T313's before/after pair was
// made, so the reviewed pixels come from the same production render calls the
// assertions run on rather than from a separate one-off harness.
const char* captureDir() { return std::getenv("WILLEQ_T313_CAPTURE_DIR"); }

void maybeCapture(irr::video::IVideoDriver* driver, irr::video::IImage* shot,
                  const std::string& name) {
    const char* dir = captureDir();
    if (!dir || !shot) return;
    driver->writeImageToFile(shot, (std::string(dir) + "/" + name + ".png").c_str());
}

class ChatScrollPixelTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch textBatch_;
    UILayout layout_;
    eqt::ui::ChatMessageBuffer buffer_;

    static constexpr irr::u32 kW = 800;
    static constexpr irr::u32 kH = 600;

    void SetUp() override {
        std::ifstream probe(kWilleqEqe1);
        if (!probe.good()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
        if (!loader_.open(kWilleqEqe1)) GTEST_SKIP() << "Failed to open " << kWilleqEqe1;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device (surfaceless EGL unavailable)";
        driver_ = device_->getVideoDriver();
        textBatch_.init(device_->getGUIEnvironment());
        layout_.computeLayout(static_cast<irr::s32>(kW), static_cast<irr::s32>(kH));

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
        atlas_ = std::make_unique<UIAtlas>();
        if (!atlas_->load(driver_, cache_.get())) {
            GTEST_SKIP() << "UIAtlas::load() failed against willeq.eqe1";
        }

        // 120 distinct, single-line messages: far more than the ~9 lines the
        // 300x160 chat box shows, so every page is visibly different.
        for (int i = 0; i < 120; ++i) {
            buffer_.addSystemMessage("line " + std::to_string(i));
        }
        buffer_.processPending();
    }

    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    // Renders the chat panel at `offset` and returns the message area's
    // pixels. The prime frame, the repeated content frame and the single
    // font draw are test_ui_atlas_pixels.cpp's documented harness facts for
    // this surfaceless-EGL/llvmpipe backend, not anything this unit changed.
    std::vector<uint32_t> renderAt(int offset, int* offsetAfter) {
        UIRenderer ui;
        ui.init(driver_, atlas_.get(), &textBatch_);
        ChatPanelState state;
        state.messageBuffer = &buffer_;
        state.mouseHover = true;     // paints the panel background
        state.scrollOffset = offset;

        irr::video::IImage* shot = nullptr;
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui.beginFrame();
            renderChatPanel(ui, layout_, state);
            ui.endFrame();
            driver_->endScene();
        }
        shot = driver_->createScreenShot();
        maybeCapture(driver_, shot, "chat_offset_" + std::to_string(offset));
        std::vector<uint32_t> px;
        if (shot) {
            const auto& r = layout_.chatMessages;
            for (int y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y) {
                for (int x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x) {
                    px.push_back(shot->getPixel(x, y).color);
                }
            }
            shot->drop();
        }
        if (offsetAfter) *offsetAfter = state.scrollOffset;
        return px;
    }
};

TEST_F(ChatScrollPixelTest, ScrollingUpChangesTheDrawnLines) {
    int clamped0 = -1, clamped1 = -1;
    auto atBottom = renderAt(0, &clamped0);
    ASSERT_FALSE(atBottom.empty()) << "framebuffer readback produced nothing";
    ASSERT_EQ(clamped0, 0);

    // One page up, by the same helper the input path uses.
    auto scrolled = renderAt(chatVisibleLines(layout_), &clamped1);
    ASSERT_EQ(scrolled.size(), atBottom.size());
    EXPECT_GT(clamped1, 0) << "renderChatPanel() clamped the scroll away -- with 120 "
                              "messages in the buffer it should not have to";
    EXPECT_NE(atBottom, scrolled)
        << "the chat message area drew identical pixels at offset 0 and one page up -- "
           "scrollback is still pinned to the newest line";
}

TEST_F(ChatScrollPixelTest, RenderClampsAnOverscrollBackOntoTheOldestLine) {
    // The chat writer deliberately does NOT bound the top: renderChatPanel()
    // owns that clamp because it is the only code that knows the wrapped-line
    // count. Pin that it still does.
    int clamped = -1;
    renderAt(100000, &clamped);
    EXPECT_GT(clamped, 0);
    EXPECT_LT(clamped, 100000);
}

// ===========================================================================
// Leg C (part 2) -- the reachable entry points
// ===========================================================================

class ScrollCommandTest : public ::testing::Test {
protected:
    void SetUp() override { renderer.setBridge(&bridge); }
    IrrlichtRenderer renderer;
    CapturingBridge bridge;
};

TEST_F(ScrollCommandTest, SlashScrollNamesAPanelExplicitlyForScriptRuns) {
    renderer.processSlashCommand("/scroll chat up 5");
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Scroll: chat up");
}

TEST_F(ScrollCommandTest, SlashScrollWithNoTargetSaysSoRatherThanSilentlyDoingNothing) {
    // No panel named, no popup open, cursor at the origin: nothing to
    // scroll, and the command says so instead of appearing to work.
    renderer.processSlashCommand("/scroll up");
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("no scrollable panel under the cursor"), std::string::npos)
        << "got: " << msgs[0];
}

TEST_F(ScrollCommandTest, SlashScrollIsInTheCommandCatalog) {
    // /help has to list it, or the scriptable path is undiscoverable.
    const std::string src = readFile(kRoot / "src/client/help/renderer_command_catalog.cpp");
    EXPECT_NE(src.find("{\"scroll\","), std::string::npos);
}

TEST(ScrollBindings, PageUpAndPageDownAreBoundToTheScrollActions) {
    auto& hotkeys = eqt::input::HotkeyManager::instance();
    hotkeys.resetToDefaults();
    auto up = hotkeys.getAction(irr::KEY_PRIOR, false, false, false,
                                eqt::input::HotkeyMode::Global);
    ASSERT_TRUE(up.has_value()) << "PageUp is not bound";
    EXPECT_EQ(*up, eqt::input::HotkeyAction::ScrollUIUp);

    auto down = hotkeys.getAction(irr::KEY_NEXT, false, false, false,
                                  eqt::input::HotkeyMode::Global);
    ASSERT_TRUE(down.has_value()) << "PageDown is not bound";
    EXPECT_EQ(*down, eqt::input::HotkeyAction::ScrollUIDown);
}

TEST(ScrollBindings, StrafeRightMovedOffPageDownSoScrollingDoesNotAlsoMoveThePlayer) {
    // PageDown used to be a second StrafeRight binding (hotkey_manager.cpp),
    // which would have made every loot-list page-down strafe the character.
    // It pairs with StrafeLeft's End on Home now.
    auto& hotkeys = eqt::input::HotkeyManager::instance();
    hotkeys.resetToDefaults();
    eqt::input::HotkeyAction action;
    EXPECT_FALSE(hotkeys.isMovementKey(irr::KEY_NEXT, action))
        << "PageDown still moves the player";
    ASSERT_TRUE(hotkeys.isMovementKey(irr::KEY_HOME, action));
    EXPECT_EQ(action, eqt::input::HotkeyAction::StrafeRight);
}

// --- the two steps that need a live device to execute ---

TEST(ScrollSourceInvariants, OnEventAccumulatesTheMouseWheel) {
    // THE DEFECT. EMIE_MOUSE_WHEEL had no case in this switch at all; the
    // wheel fell through to `default: break` and the whole client discarded
    // it. (Which is also why routing it to the UI cannot regress camera
    // zoom: zoom is bound to HA::CameraZoomIn/Out, never to the wheel.)
    const std::string src = readFile(kRoot / "src/client/graphics/renderer_event_receiver.cpp");  // J11
    EXPECT_NE(src.find("case irr::EMIE_MOUSE_WHEEL:"), std::string::npos)
        << "the mouse wheel is dropped by OnEvent() again";
    EXPECT_NE(src.find("mouseWheelDelta_ += event.MouseInput.Wheel;"), std::string::npos);
}

TEST(ScrollSourceInvariants, NothingElseConsumesTheWheelAccumulator) {
    // One consumer, so a UI-consumed notch can never also drive the camera.
    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
    size_t n = 0, at = 0;
    while ((at = frame.find("getMouseWheelDelta()", at)) != std::string::npos) { ++n; ++at; }
    EXPECT_EQ(n, 2u) << "expected exactly the UI read and the no-UI drain in "
                        "renderer_frame.cpp";
}

TEST(ScrollSourceInvariants, TheFrameRoutesTheWheelIntoTheStaticUI) {
    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
    EXPECT_NE(frame.find("staticUIInput_->processWheel("), std::string::npos)
        << "the wheel never reaches the static UI -- every panel is pinned to page one again";
    EXPECT_NE(frame.find("staticUIInput_->processScrollKey("), std::string::npos)
        << "PageUp/PageDown never reach the static UI";
}

// ===========================================================================
// Leg B (pixels) -- the loot popup, drawn by renderLootPopup() itself
// ===========================================================================
//
// The loot leg above proves the ROW SET through hitTestLootRow() and the real
// intents. This proves the same thing in pixels, through the production
// render function and the real UI atlas, and is what review/T313's
// before/after pair is captured from.

class LootScrollPixelTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch textBatch_;
    UILayout layout_;
    LootPanelState loot_;

    void SetUp() override {
        std::ifstream probe(kWilleqEqe1);
        if (!probe.good()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
        if (!loader_.open(kWilleqEqe1)) GTEST_SKIP() << "Failed to open " << kWilleqEqe1;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device";
        driver_ = device_->getVideoDriver();
        textBatch_.init(device_->getGUIEnvironment());
        layout_.computeLayout(800, 600);

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
        atlas_ = std::make_unique<UIAtlas>();
        if (!atlas_->load(driver_, cache_.get())) GTEST_SKIP() << "UIAtlas::load() failed";

        // 14 items, 8 visible rows: slots 8..13 are off the first page.
        loot_.isOpen = true;
        loot_.corpseId = 4242;
        loot_.corpseName = "a decaying skeleton";
        static const char* kNames[14] = {
            "Rusty Short Sword", "Tattered Cloth Cap", "Bone Chips",
            "Small Bronze Ring", "Rat Whiskers", "Cracked Staff",
            "Fire Beetle Eye", "Worn Leather Boots",
            "Silver Amulet", "Gnoll Fang", "Muffin", "Bottle of Milk",
            "Jade Shard", "A Tiny Gold Idol"};
        for (int i = 0; i < 14; ++i) {
            loot_.items.push_back({static_cast<uint8_t>(i), 5000u + i, kNames[i]});
        }
    }

    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    std::vector<uint32_t> renderAndRead(const std::string& captureName) {
        UIRenderer ui;
        ui.init(driver_, atlas_.get(), &textBatch_);
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui.beginFrame();
            renderLootPopup(ui, layout_, loot_);
            ui.endFrame();
            driver_->endScene();
        }
        irr::video::IImage* shot = driver_->createScreenShot();
        maybeCapture(driver_, shot, captureName);
        std::vector<uint32_t> px;
        if (shot) {
            const auto& r = layout_.centerPopup;
            for (int y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y) {
                for (int x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x) {
                    px.push_back(shot->getPixel(x, y).color);
                }
            }
            shot->drop();
        }
        return px;
    }
};

TEST_F(LootScrollPixelTest, ScrollingRedrawsTheLootListWithDifferentRows) {
    ASSERT_EQ(loot_.scrollOffset, 0);
    auto firstPage = renderAndRead("loot_before_scroll_page1_of_14_items");
    ASSERT_FALSE(firstPage.empty()) << "framebuffer readback produced nothing";

    ChatPanelState chat;
    SkillsPopupState skills;
    StaticUIInput::scrollPanel(ScrollTarget::Loot, -100, layout_, chat, skills, loot_);
    ASSERT_EQ(loot_.scrollOffset, loot_.maxScrollOffset());
    ASSERT_EQ(loot_.scrollOffset, 6);

    auto lastPage = renderAndRead("loot_after_scroll_page2_slots_6_to_13");
    ASSERT_EQ(lastPage.size(), firstPage.size());
    EXPECT_NE(firstPage, lastPage)
        << "renderLootPopup() drew identical pixels before and after a full scroll -- "
           "the last six items are still unreachable";
}
