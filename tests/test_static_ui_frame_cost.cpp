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
// The timing account (StaticUiFrameCostAccount.*) runs only with
// WILLEQ_T462_ACCOUNT=1 in the environment: wall-clock numbers are evidence
// for a Review, not something a ctest run under -j6 load can assert.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
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

    void fillChat(int n) {
        for (int i = 0; i < n; ++i) chatBuffer_.addSystemMessage(chatLine(i));
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

    struct FrameSample {
        int64_t submitUs = 0;
        int64_t chatUs = 0;
        UIRenderer::FlushStats flush;
        int64_t finishUs = 0;
    };

    FrameSample renderOneFrame() {
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

// ---------------------------------------------------------------------------
// The account. Prints one row per scenario; asserts nothing about time.
// ---------------------------------------------------------------------------
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
    std::printf("T462 ACCOUNT (x86 llvmpipe, %dx%d, %d frames, means)\n", kW, kH, kFrames);
    std::printf("%-16s %9s %9s %9s %9s %9s %6s %6s %6s\n", "scenario", "submitUs", "chatUs",
                "cmdFlush", "txtFlush", "glFinish", "cmds", "draws", "texts");
    for (const auto& sc : scenarios) {
        chatBuffer_.clear();
        fillChat(sc.chatMessages);
        chat_.scrollOffset = 0;
        inv_.activePopup = sc.inventoryOpen ? PopupType::Inventory : PopupType::None;
        for (int i = 0; i < kWarm; ++i) renderOneFrame();
        double submit = 0, chat = 0, cmd = 0, txt = 0, fin = 0;
        size_t cmds = 0, draws = 0, texts = 0;
        for (int i = 0; i < kFrames; ++i) {
            const FrameSample s = renderOneFrame();
            submit += s.submitUs; chat += s.chatUs; fin += s.finishUs;
            cmd += s.flush.commandFlushUs; txt += s.flush.textFlushUs;
            cmds = s.flush.commands; draws = s.flush.driverDraws; texts = s.flush.textEntries;
        }
        std::printf("%-16s %9.0f %9.0f %9.0f %9.0f %9.0f %6zu %6zu %6zu\n", sc.name,
                    submit / kFrames, chat / kFrames, cmd / kFrames, txt / kFrames,
                    fin / kFrames, cmds, draws, texts);
    }
}
