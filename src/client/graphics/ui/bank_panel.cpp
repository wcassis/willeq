/*
 * T436 (board B429) -- the bank window.
 *
 * Its own TU (M00). The geometry is in
 * include/client/graphics/ui/bank_layout.h, which quotes EQUI_BankWnd.xml;
 * this file computes no rect.
 */

#include "client/graphics/ui/bank_panel.h"

#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"
#include "client/state/event_bus.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kTitle(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 80, 80, 80);
const irr::video::SColor kDisabledText(255, 120, 120, 128);

}  // namespace

// --- bridge arms -----------------------------------------------------------

void applyBankWindowOpened(BankPanelState& state,
                           const eqt::state::WindowOpenedData& data) {
    state.isOpen = true;
    state.npcId = data.npcId;
    state.bankerName = data.npcName;
    state.hoveredSlot = -1;
}

void applyBankWindowClosed(BankPanelState& state,
                           const eqt::state::WindowClosedData& data) {
    (void)data;
    state.isOpen = false;
    state.npcId = 0;
    state.hoveredSlot = -1;
    // The eight cells are NOT cleared: the bank's contents survive the window
    // being shut, and the server re-sends nothing on the next open. Closing
    // is not a reason to forget what is in the vault.
}

void applyBankCurrencyChanged(BankPanelState& state,
                              const eqt::state::BankCurrencyChangedData& data) {
    state.coins[0] = data.platinum;
    state.coins[1] = data.gold;
    state.coins[2] = data.silver;
    state.coins[3] = data.copper;
}

bool applyBankSlotChanged(BankPanelState& state,
                          const eqt::state::InventorySlotChangedData& data) {
    const int idx = static_cast<int>(data.slotId) - BankLayout::EQTYPE_BEGIN;
    if (idx < 0 || idx >= BankPanelState::SLOT_COUNT) return false;
    auto& slot = state.slots[idx];
    slot.hasItem = data.hasItem;
    slot.itemName = data.hasItem ? data.itemName : std::string();
    slot.iconId = data.hasItem ? data.iconId : 0;
    slot.quantity = data.hasItem ? static_cast<int32_t>(data.quantity) : 0;
    return true;
}

// --- draw ------------------------------------------------------------------

void renderBankWindow(UIRenderer& ui, const UILayout& layout,
                      const BankPanelState& state) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    ui.drawPanel(BankLayout::windowRect(layout));

    // --- Style_Titlebar: <Text>Bank</Text> ---
    const auto title = BankLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText("Bank", title.UpperLeftCorner.X + 6,
                    title.UpperLeftCorner.Y + 2, kTitle);
    }

    // --- Style_Closebox ---
    const auto close = BankLayout::closeBoxRect(layout);
    ui.drawRect(close, state.closeHovered
                           ? irr::video::SColor(220, 120, 60, 60)
                           : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);

    // --- BW_BankerName ---
    if (tb) {
        tb->addTextCentered(state.bankerName.empty() ? std::string("Banker")
                                                     : state.bankerName,
                            BankLayout::bankerNameRect(layout), kText);
    }

    // --- BW_BankSlot0..7: A_RecessedBox with the item art at ItemOffset ---
    for (int i = 0; i < BankPanelState::SLOT_COUNT; ++i) {
        const auto cell = BankLayout::slotRect(layout, i);
        ui.drawSprite(cell, static_cast<uint8_t>(UISprite::SlotBackground));
        if (state.hoveredSlot == i) {
            ui.drawRect(cell, irr::video::SColor(60, 255, 255, 255));
        }
        if (!state.slots[i].hasItem) continue;

        const auto art = BankLayout::slotItemRect(layout, i);
        const bool drewIcon = ui.drawItemIcon(art, state.slots[i].iconId);
        if (!tb) continue;
        if (!drewIcon) {
            tb->addText(state.slots[i].itemName.substr(
                            0, std::min<size_t>(3, state.slots[i].itemName.size())),
                        art.UpperLeftCorner.X + 2, art.UpperLeftCorner.Y + 2,
                        kText);
        }
        if (state.slots[i].quantity > 1) {
            tb->addText(std::to_string(state.slots[i].quantity),
                        art.UpperLeftCorner.X + 2, art.UpperLeftCorner.Y + 14,
                        irr::video::SColor(255, 180, 220, 180));
        }
    }

    // --- BW_Money0..3: A_RecessedBox with an 18x18 coin decal ---
    for (int denom = 0; denom < BankPanelState::COIN_COUNT; ++denom) {
        const auto row = BankLayout::coinRect(layout, denom);
        ui.drawSprite(row, static_cast<uint8_t>(UISprite::SlotBackground));
        // T416 (board B316)'s rule: the atlas ring vanishes at this size, so
        // the frame comes from the uniform `white` sprite via drawFrame().
        ui.drawFrame(row, kFrame);
        const auto decal = BankLayout::coinDecalRect(layout, denom);
        ui.drawSprite(decal, static_cast<uint8_t>(
            static_cast<int>(UISprite::CoinPlatinum) + denom));
        if (!tb) continue;
        tb->addText(std::to_string(state.coins[denom]),
                    decal.LowerRightCorner.X + 2, decal.UpperLeftCorner.Y,
                    kText);
    }

    // --- BW_ChangeButton: DRAWN GREYED, NOT HIT-TESTABLE (board B441) ---
    // The oracle's Change opens a coin-denomination dialog fable does not
    // have, and /bank convert needs a from/to/amount triple no single click
    // supplies. T363's rule, made mechanical by T434: a control with no
    // backing setting is greyed and eats no click.
    const auto change = BankLayout::changeButtonRect(layout);
    ui.drawRect(change, irr::video::SColor(160, 40, 40, 46));
    ui.drawFrame(change, kFrame);
    if (tb) tb->addTextCentered("Change", change, kDisabledText);

    // --- BW_DoneButton ---
    const auto done = BankLayout::doneButtonRect(layout);
    ui.drawRect(done, state.doneHovered ? irr::video::SColor(220, 120, 60, 60)
                                        : irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(done, kFrame);
    if (tb) tb->addTextCentered("Done", done, kText);
}

// --- hit tests -------------------------------------------------------------

int hitTestBankSlot(const UILayout& layout, const BankPanelState& state,
                    int mouseX, int mouseY) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    for (int i = 0; i < BankPanelState::SLOT_COUNT; ++i) {
        if (BankLayout::slotRect(layout, i).isPointInside(p)) return i;
    }
    return -1;
}

bool hitTestBankDoneButton(const UILayout& layout, const BankPanelState& state,
                           int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return BankLayout::doneButtonRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool hitTestBankCloseBox(const UILayout& layout, const BankPanelState& state,
                         int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return BankLayout::closeBoxRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool bankContains(const UILayout& layout, const BankPanelState& state,
                  int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return BankLayout::windowRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

}  // namespace Graphics
}  // namespace EQT
