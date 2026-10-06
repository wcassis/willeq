// T462 (boards B505, B506): the static UI's per-frame cost.
//
// T459 measured on the Orange Pi that opening the inventory adds ~6 ms of
// "Static UI" per frame and that the section grows with chat history. This
// file is the ACCOUNT of where that goes and the regression net for the fix.
//
// It renders the same panel functions FrameComponent::renderStaticUI() calls,
// in the same order, into a real headless GLES2 device (surfaceless EGL /
// llvmpipe -- test_ui_atlas_pixels.cpp's harness) with the real UIAtlas and
// item icon sheets out of willeq.eqe1, and reads UIRenderer::takeFlushStats().
// Self-skips when the pack or the device is unavailable.
//
// The timing account (AccountPerStageTimings) runs only with
// WILLEQ_T462_ACCOUNT=1 in the environment (and WILLEQ_T462_RES=1280x720 to
// change the resolution): wall-clock numbers are evidence for a Review, not
// something a ctest run under -j6 load can assert.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/chat_wrap_cache.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/chat/chat_message_buffer.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" void glFinish(void);

using namespace EQT::Graphics;

namespace {

const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

bool accountEnabled() {
    const char* v = std::getenv("WILLEQ_T462_ACCOUNT");
    return v && v[0] == '1';
}

// 800x600 unless WILLEQ_T462_RES=<w>x<h> (the account only; every pin below
// was taken at 800x600 and self-skips at any other size).
void resolution(irr::u32& w, irr::u32& h) {
    w = 800; h = 600;
    if (const char* v = std::getenv("WILLEQ_T462_RES")) {
        unsigned rw = 0, rh = 0;
        if (std::sscanf(v, "%ux%u", &rw, &rh) == 2 && rw > 0 && rh > 0) { w = rw; h = rh; }
    }
}

// Chat lines shaped like T459's P1 session (guard fights in nro): combat
// spam, a few long system lines that wrap, some says.
std::string chatLine(int i) {
    switch (i % 5) {
        case 0: return "a Qeynos guard hits a gnoll pup for " + std::to_string(10 + i % 17) +
                       " points of damage.";
        case 1: return "a gnoll pup tries to bite a Qeynos guard, but misses!";
        case 2: return "Your faction standing with the Guards of Qeynos could not possibly get any "
                       "better. You have gained experience! The guard says, 'Hail, citizen " +
                       std::to_string(i) + "'.";
        case 3: return "a Qeynos guard has been slain by a gnoll pup!";
        default: return "Testcaster says, 'line " + std::to_string(i) + "'";
    }
}

// The pre-T462 renderChatPanel() wrap, copied here VERBATIM from
// static_panels.cpp at f70b6c97 (the fable tip T462 started from) so the
// cache is compared against the code it replaced, not against itself.
std::vector<ChatWrappedLine> preT462WrapAll(const eqt::ui::ChatMessageBuffer& buffer,
                                            irr::s32 areaW, bool showTimestamps,
                                            const TextBatch& tbRef) {
    const TextBatch* tb = &tbRef;
    const auto& messages = buffer.getMessages();
    struct WrappedLine {
        std::string text;
        irr::video::SColor color;
    };
    std::vector<WrappedLine> wrappedLines;

    for (size_t i = 0; i < messages.size(); ++i) {
        std::string display = eqt::ui::formatMessageForDisplay(messages[i], showTimestamps);
        // Word-wrap using pixel width measurement
        while (tb->getTextWidth(display) > areaW) {
            // Binary search for the longest substring that fits
            int lo = 1, hi = static_cast<int>(display.size());
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (tb->getTextWidth(display.substr(0, mid)) <= areaW)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            // Find last space within the fitting range for word break
            int breakAt = lo;
            for (int j = lo; j > lo / 2; --j) {
                if (display[j] == ' ') { breakAt = j; break; }
            }
            wrappedLines.push_back({display.substr(0, breakAt), messages[i].color});
            display = display.substr(breakAt);
            if (!display.empty() && display[0] == ' ') display = display.substr(1);
        }
        if (!display.empty()) {
            wrappedLines.push_back({display, messages[i].color});
        }
    }
    std::vector<ChatWrappedLine> out;
    for (auto& l : wrappedLines) out.push_back({l.text, l.color});
    return out;
}

}  // namespace

class StaticUiFrameCostTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch textBatch_;
    UILayout layout_;
    UIRenderer ui_;
    irr::u32 kW = 800;
    irr::u32 kH = 600;

    // The state renderStaticUI() draws from, defaulted as a fresh session.
    PlayerStatsData stats_;
    TargetInfoData target_;
    eqt::ui::ChatMessageBuffer chatBuffer_{500};  // renderer_init.cpp's size
    ChatPanelState chat_;
    HotbarPanelState hotbar_;
    SpellGemPanelState gems_;
    BuffBarState buffs_;
    CastingBarState casting_;
    GroupPanelState group_;
    PetPanelState pet_;
    InventoryPanelState inv_;
    SpellbookPopupState spellbook_;
    SkillsPopupState skills_;
    LootPanelState loot_;
    TradeskillPanelState tradeskill_;
    MerchantPanelState merchant_;
    CombatAbilityPanelState combat_;
    BankPanelState bank_;
    ItemDisplayPanelState itemDisplay_;
    OptionsPanelState options_;
    XPBarState xp_;
    CursorAttachmentState cursor_;

    void SetUp() override {
        resolution(kW, kH);
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
        ui_.init(driver_, atlas_.get(), &textBatch_);

        // A Wizard's session: named, full bars, one memorised gem, a few hotbar
        // entries -- Testcaster as T459 found it.
        stats_.name = "Testcaster";
        stats_.level = 50;
        stats_.curHP = stats_.maxHP = 1200;
        stats_.curMana = stats_.maxMana = 2400;
        stats_.curEndurance = stats_.maxEndurance = 800;
        gems_.gems[0].spellId = 288;
        gems_.gems[0].gemState = 1;
        gems_.gems[0].spellName = "Minor Shielding";
        hotbar_.slots[0].type = eqt::HotbarSlotType::Spell;
        hotbar_.slots[0].name = "Minor Shielding";
        xp_.progress = 0.4f;
        chat_.messageBuffer = &chatBuffer_;

        // An inventory with real icon ids in it, so the icon sheets' own
        // commands are part of the count exactly as on a real character.
        for (int i : {1, 2, 13, 14, 17}) {
            inv_.equipSlots[i].hasItem = true;
            inv_.equipSlots[i].itemName = "Robe";
            inv_.equipSlots[i].iconId = 600 + i;
        }
        for (int i = 0; i < 4; ++i) {
            inv_.generalSlots[i].hasItem = true;
            inv_.generalSlots[i].itemName = "Bag";
            inv_.generalSlots[i].iconId = 700 + i;
            inv_.generalSlots[i].quantity = (i == 3) ? 20 : 1;
        }
        eqt::state::CurrencyChangedData coins;
        coins.platinum = 8; coins.gold = 18; coins.silver = 8; coins.copper = 16;
        applyCurrencyChanged(inv_, coins);
    }

    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    bool pinsApply() const { return kW == 800 && kH == 600; }

    void fillChat(int n, int firstIndex = 0) {
        for (int i = 0; i < n; ++i) chatBuffer_.addSystemMessage(chatLine(firstIndex + i));
        chatBuffer_.processPending();
    }

    // renderStaticUI()'s draw list, statement for statement, minus the input
    // pass and the paperdoll update (no player entity here).
    void submitStaticUI() {
        renderPlayerStatus(ui_, layout_, stats_);
        renderTargetInfo(ui_, layout_, target_);
        renderChatPanel(ui_, layout_, chat_);
        renderHotbar(ui_, layout_, hotbar_);
        renderSpellGemPanel(ui_, layout_, gems_);
        renderBuffBar(ui_, layout_, buffs_);
        renderCastingBar(ui_, layout_, casting_);
        renderGroupPanel(ui_, layout_, group_);
        renderPetPanel(ui_, layout_, pet_);
        renderInventoryPopup(ui_, layout_, inv_);
        renderSpellbookPopup(ui_, layout_, spellbook_);
        renderSkillsPopup(ui_, layout_, skills_);
        renderLootPopup(ui_, layout_, loot_);
        renderTradeskillPopup(ui_, layout_, tradeskill_);
        renderMerchantPopup(ui_, layout_, merchant_, cursor_);
        renderCombatAbilityPanel(ui_, layout_, combat_);
        renderBankWindow(ui_, layout_, bank_);
        renderItemDisplayPopup(ui_, layout_, itemDisplay_);
        renderOptionsWindow(ui_, layout_, options_);
        renderXPBar(ui_, layout_, xp_);
        renderCursorAttachment(ui_, layout_, cursor_);
    }

    // One content frame of `submit`, after a prime frame and with a repeat --
    // test_ui_atlas_pixels.cpp's documented surfaceless-EGL harness facts.
    // Returns the framebuffer (caller drops) and the LAST frame's flush stats.
    irr::video::IImage* renderFrames(const std::function<void()>& submit,
                                     UIRenderer::FlushStats* stats) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui_.beginFrame();
            submit();
            ui_.endFrame();
            const UIRenderer::FlushStats s = ui_.takeFlushStats();
            if (stats) *stats = s;
            driver_->endScene();
        }
        return driver_->createScreenShot();
    }

    // Pixels that differ between two same-size framebuffers, and how many of
    // the first are not the black clear colour (so a comparison of two empty
    // frames cannot pass as "identical").
    static void comparePixels(irr::video::IImage* a, irr::video::IImage* b,
                              size_t* differing, size_t* painted) {
        *differing = 0;
        *painted = 0;
        const auto d = a->getDimension();
        for (irr::u32 y = 0; y < d.Height; ++y) {
            for (irr::u32 x = 0; x < d.Width; ++x) {
                const irr::u32 ca = a->getPixel(x, y).color;
                if ((ca & 0x00FFFFFF) != 0) ++*painted;
                if (ca != b->getPixel(x, y).color) ++*differing;
            }
        }
    }

    struct FrameSample {
        int64_t submitUs = 0;
        int64_t chatUs = 0;
        UIRenderer::FlushStats flush;
        int64_t finishUs = 0;
    };

    FrameSample renderOneTimedFrame() {
        using Clock = std::chrono::steady_clock;
        auto us = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
        };
        FrameSample s;
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        glFinish();
        const auto t0 = Clock::now();
        ui_.beginFrame();
        submitStaticUI();
        const auto t1 = Clock::now();
        ui_.endFrame();
        const auto t2 = Clock::now();
        glFinish();
        const auto t3 = Clock::now();
        driver_->endScene();
        s.submitUs = us(t0, t1);
        s.flush = ui_.takeFlushStats();
        s.finishUs = us(t2, t3);

        // The chat panel's own submission cost, measured alone (the wrap).
        ui_.beginFrame();
        const auto c0 = Clock::now();
        renderChatPanel(ui_, layout_, chat_);
        s.chatUs = us(c0, Clock::now());
        ui_.beginFrame();  // discard: nothing of this pass is flushed
        textBatch_.begin();
        return s;
    }
};

// ===========================================================================
// B505 -- the chat wrap runs on change, not on every frame
// ===========================================================================

// Before T462 every frame formatted and wrapped every buffered message: ten
// frames over 200 messages was 2000 wraps. Now each message is wrapped once,
// when it arrives; scrolling wraps nothing; a width or timestamp change
// re-wraps what is buffered, once.
TEST_F(StaticUiFrameCostTest, TheChatWrapRunsOncePerNewMessageNotOncePerFrame) {
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) {
            irr::video::IImage* img = renderFrames([&] { renderChatPanel(ui_, layout_, chat_); }, nullptr);
            if (img) img->drop();
        }
    };

    fillChat(200);
    frames(5);  // renderFrames draws 2 content frames per call: 10 frames
    EXPECT_EQ(chat_.messagesWrapped, 200u)
        << "ten frames over 200 unchanged messages must wrap each message once, not once per "
           "frame (the pre-T462 panel wrapped 2000)";

    for (int i = 0; i < 3; ++i) chatBuffer_.addSystemMessage("new " + std::to_string(i));
    frames(3);
    EXPECT_EQ(chat_.messagesWrapped, 203u) << "three new messages are three wraps";

    chat_.scrollOffset = 4;
    frames(2);
    EXPECT_EQ(chat_.messagesWrapped, 203u) << "scrolling re-wraps nothing";

    // Past the 500-message cap: the 400 new ones are wrapped, the 103 evicted
    // from the front are dropped, nothing that stays is wrapped again.
    fillChat(400, 1000);
    frames(2);
    EXPECT_EQ(chat_.messagesWrapped, 603u);
    EXPECT_EQ(chatBuffer_.size(), 500u);

    chat_.showTimestamps = true;
    frames(2);
    EXPECT_EQ(chat_.messagesWrapped, 1103u) << "a timestamp toggle re-wraps the 500 buffered once";
}

// The lines the cache holds are exactly the lines the pre-T462 per-frame wrap
// produced -- through incremental arrivals, front eviction, an unbreakable
// word, a width change, a timestamp toggle and a clear().
TEST_F(StaticUiFrameCostTest, TheCachedLinesAreThePreT462LinesThroughEvictionResizeAndClear) {
    eqt::ui::ChatMessageBuffer small(50);
    ChatWrapCache cache;
    const ChatTextMeasure measure = [&](const std::string& s) { return textBatch_.getTextWidth(s); };
    const irr::s32 areaW = layout_.chatMessages.getWidth() - 18;  // renderChatPanel()'s own width

    auto check = [&](const std::string& what, irr::s32 w, bool ts) {
        cache.sync(small, w, ts, measure);
        const std::vector<ChatWrappedLine> ref = preT462WrapAll(small, w, ts, textBatch_);
        ASSERT_EQ(cache.lineCount(), ref.size()) << what;
        for (size_t i = 0; i < ref.size(); ++i) {
            ASSERT_EQ(cache.line(i).text, ref[i].text) << what << " line " << i;
            ASSERT_EQ(cache.line(i).color.color, ref[i].color.color) << what << " line " << i;
        }
    };

    for (int i = 0; i < 130; ++i) {
        small.addSystemMessage(chatLine(i));
        if (i % 7 == 6) {
            small.processPending();
            check("arrival batch ending " + std::to_string(i), areaW, false);
        }
    }
    small.processPending();
    check("after 130 into a 50-message buffer", areaW, false);

    small.addSystemMessage(std::string(200, 'W'));  // no space to break at
    small.addSystemMessage(" leading space and then enough words to wrap across the chat box width");
    eqt::ui::ChatMessage say;
    say.sender = "Gnoll";
    say.text = "a say with a sender and its own channel colour";
    say.channel = eqt::ui::ChatChannel::Shout;
    small.addMessage(say);
    small.processPending();
    check("edge messages", areaW, false);

    check("narrow", 60, false);
    check("back to the panel width", areaW, false);
    check("timestamps on", areaW, true);

    small.clear();
    small.addSystemMessage("after clear");
    small.processPending();
    check("after clear", areaW, true);
}

// And the chat box draws the same pixels from a warm cache that has lived
// through evictions as from a cold one built in a single pass.
TEST_F(StaticUiFrameCostTest, AWarmChatCachePaintsTheSamePixelsAsAColdOne) {
    if (!pinsApply()) GTEST_SKIP() << "pixel pins are taken at 800x600";
    ChatPanelState warm;
    warm.messageBuffer = &chatBuffer_;
    warm.mouseHover = true;
    for (int batch = 0; batch < 12; ++batch) {
        fillChat(60, batch * 60);
        irr::video::IImage* img = renderFrames([&] { renderChatPanel(ui_, layout_, warm); }, nullptr);
        if (img) img->drop();
    }
    warm.scrollOffset = 7;
    irr::video::IImage* warmShot = renderFrames([&] { renderChatPanel(ui_, layout_, warm); }, nullptr);

    ChatPanelState cold;
    cold.messageBuffer = &chatBuffer_;
    cold.mouseHover = true;
    cold.scrollOffset = 7;
    irr::video::IImage* coldShot = renderFrames([&] { renderChatPanel(ui_, layout_, cold); }, nullptr);

    ASSERT_NE(warmShot, nullptr);
    ASSERT_NE(coldShot, nullptr);
    size_t differing = 0, painted = 0;
    comparePixels(coldShot, warmShot, &differing, &painted);
    EXPECT_GT(painted, 1000u) << "the chat box drew nothing -- the comparison would be vacuous";
    EXPECT_EQ(differing, 0u);
    EXPECT_EQ(cold.messagesWrapped, 500u) << "the cold panel wraps the 500 buffered messages once";
    warmShot->drop();
    coldShot->drop();
}

// ===========================================================================
// B506 -- the open inventory's commands are flushed in texture runs
// ===========================================================================

// The static UI with the inventory open submits 164 draw commands (103 of them
// the popup's, IW_Destroy included since B442b) and 50 text entries. Before T462 the flush issued one driver
// draw call -- with its full 2D state setup -- per command; now it issues at
// most two per maximal same-texture run (the run's first quad sets the state,
// the rest go in one draw).
TEST_F(StaticUiFrameCostTest, TheOpenInventoryFlushIssuesAtMostTwoDrawsPerTextureRun) {
    if (!pinsApply()) GTEST_SKIP() << "count pins are taken at 800x600";
    inv_.activePopup = PopupType::Inventory;
    UIRenderer::FlushStats s;
    irr::video::IImage* img = renderFrames([&] { submitStaticUI(); }, &s);
    if (img) img->drop();

    // The draw LIST is not T462's to change; pinned so batching cannot hide a
    // lost command.
    // B442b: +5 commands (IW_Destroy's rect + its 4-edge frame) and +1 text
    // entry (its label): 159 -> 164 and 49 -> 50.
    EXPECT_EQ(s.commands, 164u);
    EXPECT_EQ(s.textEntries, 50u);
    ASSERT_GT(s.textureRuns, 0u);
    EXPECT_LT(s.textureRuns, s.commands / 4)
        << "the fixture's icons break the atlas run " << s.textureRuns << " times";
    EXPECT_LE(s.driverDraws, 2 * s.textureRuns)
        << s.driverDraws << " driver draws for " << s.commands << " commands in "
        << s.textureRuns << " texture runs -- the flush is still one draw per command";

    inv_.activePopup = PopupType::None;
    img = renderFrames([&] { submitStaticUI(); }, &s);
    if (img) img->drop();
    EXPECT_EQ(s.commands, 61u);
    EXPECT_EQ(s.textEntries, 25u);
    EXPECT_LE(s.driverDraws, 2 * s.textureRuns);
}

// ---------------------------------------------------------------------------
// THE FALLBACK PATH IS THE ONE WILL'S PI RUNS. renderer_init.cpp loads the UI
// atlas only when --eqe1-path is given, and Will's recipe gives none, so on
// his Orange Pi every drawRect/drawSprite was an immediate draw2DRectangle()
// during submission (T462's Pi account, X1: 0 commands reached endFrame(),
// Text Flush 8-12 ms of a 18-26 ms Static UI). These pins cover that path.
// ---------------------------------------------------------------------------
TEST_F(StaticUiFrameCostTest, TheFallbackPathQueuesItsDrawsAndFlushesThemInRuns) {
    if (!pinsApply()) GTEST_SKIP() << "count pins are taken at 800x600";
    ui_.init(driver_, nullptr, &textBatch_);
    inv_.activePopup = PopupType::Inventory;
    UIRenderer::FlushStats s;
    irr::video::IImage* img = renderFrames([&] { submitStaticUI(); }, &s);
    if (img) img->drop();

    EXPECT_GT(s.commands, 100u)
        << "the fallback path drew immediately during submission, so nothing reached the "
           "flush and nothing could be batched";
    ASSERT_GT(s.textureRuns, 0u);
    EXPECT_LT(s.textureRuns, s.commands / 4);
    EXPECT_LE(s.driverDraws, 2 * s.textureRuns)
        << s.driverDraws << " driver draws for " << s.commands << " commands in "
        << s.textureRuns << " runs";
}

// Every text entry was one font draw -- one draw2DImageBatch with its own
// allocations and 2D state setup. The glyphs of every plain entry now go in
// texture runs: a couple of draws for the whole frame's text, on both paths.
TEST_F(StaticUiFrameCostTest, TheTextFlushIsBatchedOnBothPaths) {
    if (!pinsApply()) GTEST_SKIP() << "count pins are taken at 800x600";
    inv_.activePopup = PopupType::Inventory;
    fillChat(120);
    for (bool fallback : {false, true}) {
        ui_.init(driver_, fallback ? nullptr : atlas_.get(), &textBatch_);
        UIRenderer::FlushStats s;
        irr::video::IImage* img = renderFrames([&] { submitStaticUI(); }, &s);
        if (img) img->drop();
        const char* path = fallback ? "fallback path" : "atlas path";
        EXPECT_GT(s.textEntries, 40u) << path;
        EXPECT_LE(s.textDraws, 2u) << path << ": " << s.textDraws << " text draws for "
                                   << s.textEntries << " entries";
    }
}

// The batched flush -- commands AND text -- paints exactly the pixels of the
// pre-T462 per-command, per-entry flush, over the whole 800x600 frame: every
// panel, the open inventory, a hovered slot, the paperdoll blit (an external
// texture inside a run), a scrolled chat box, and two text entries the glyph
// batch must hand back to the font untouched (a line break, a non-ASCII byte).
class StaticUiBatchedPixelTest : public StaticUiFrameCostTest,
                                 public ::testing::WithParamInterface<bool> {};

TEST_P(StaticUiBatchedPixelTest, TheBatchedFlushPaintsExactlyThePixelsOfThePerCommandFlush) {
    if (!pinsApply()) GTEST_SKIP() << "pixel pins are taken at 800x600";
    const bool fallback = GetParam();
    ui_.init(driver_, fallback ? nullptr : atlas_.get(), &textBatch_);
    inv_.activePopup = PopupType::Inventory;
    inv_.hoveredSlot = 3;
    hotbar_.hoveredSlot = 2;
    gems_.hoveredGem = 1;
    irr::core::rect<irr::s32> unused;
    inv_.paperdollTexture = atlas_->icons().lookup(700, unused);  // any non-atlas texture
    ASSERT_NE(inv_.paperdollTexture, nullptr);
    fillChat(120);
    chat_.mouseHover = true;
    chat_.scrollOffset = 3;
    auto submit = [&] {
        submitStaticUI();
        textBatch_.addText("first line\nsecond line", 330, 300, irr::video::SColor(255, 255, 120, 0));
        textBatch_.addText("caf\xc3\xa9 au lait", 330, 340, irr::video::SColor(255, 90, 255, 90));
    };

    ui_.setBatchingEnabled(false);
    UIRenderer::FlushStats perCommand;
    irr::video::IImage* reference = renderFrames(submit, &perCommand);
    ui_.setBatchingEnabled(true);
    UIRenderer::FlushStats batched;
    irr::video::IImage* candidate = renderFrames(submit, &batched);

    ASSERT_NE(reference, nullptr);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(perCommand.commands, batched.commands);
    EXPECT_EQ(perCommand.driverDraws, perCommand.commands) << "the seam must really disable batching";
    size_t differing = 0, painted = 0;
    comparePixels(reference, candidate, &differing, &painted);
    EXPECT_GT(painted, 10000u) << "the static UI drew nothing -- the comparison would be vacuous";
    EXPECT_EQ(differing, 0u) << "the batched flush moved " << differing << " pixels";
    reference->drop();
    candidate->drop();
}

INSTANTIATE_TEST_SUITE_P(AtlasAndFallback, StaticUiBatchedPixelTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& i) {
                             return std::string(i.param ? "FallbackPath" : "AtlasPath");
                         });

// ===========================================================================
// The account. Prints one row per scenario; asserts nothing about time.
// ===========================================================================
TEST_F(StaticUiFrameCostTest, AccountPerStageTimings) {
    if (!accountEnabled()) GTEST_SKIP() << "set WILLEQ_T462_ACCOUNT=1 to run the timing account";

    constexpr int kWarm = 30, kFrames = 300;
    struct Scenario { const char* name; bool inventoryOpen; int chatMessages; };
    const Scenario scenarios[] = {
        {"closed chat=0",   false, 0},
        {"open   chat=0",   true,  0},
        {"closed chat=500", false, 500},
        {"open   chat=500", true,  500},
    };
    std::printf("T462 ACCOUNT (x86 llvmpipe, %ux%u, %d frames, means)\n", kW, kH, kFrames);
    std::printf("%-16s %9s %9s %9s %9s %9s %6s %6s %6s %6s\n", "scenario", "submitUs", "chatUs",
                "cmdFlush", "txtFlush", "glFinish", "cmds", "runs", "draws", "texts");
    for (const auto& sc : scenarios) {
        chatBuffer_.clear();
        fillChat(sc.chatMessages);
        chat_.scrollOffset = 0;
        inv_.activePopup = sc.inventoryOpen ? PopupType::Inventory : PopupType::None;
        for (int i = 0; i < kWarm; ++i) renderOneTimedFrame();
        double submit = 0, chat = 0, cmd = 0, txt = 0, fin = 0;
        size_t cmds = 0, runs = 0, draws = 0, texts = 0;
        for (int i = 0; i < kFrames; ++i) {
            const FrameSample s = renderOneTimedFrame();
            submit += s.submitUs; chat += s.chatUs; fin += s.finishUs;
            cmd += s.flush.commandFlushUs; txt += s.flush.textFlushUs;
            cmds = s.flush.commands; runs = s.flush.textureRuns;
            draws = s.flush.driverDraws; texts = s.flush.textEntries;
        }
        std::printf("%-16s %9.0f %9.0f %9.0f %9.0f %9.0f %6zu %6zu %6zu %6zu\n", sc.name,
                    submit / kFrames, chat / kFrames, cmd / kFrames, txt / kFrames,
                    fin / kFrames, cmds, runs, draws, texts);
    }
}
