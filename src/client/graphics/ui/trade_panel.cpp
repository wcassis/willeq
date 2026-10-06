/*
 * B896 (GitLab #212) -- the trade window and the NPC give window.
 *
 * Its own TU (M00). The geometry is in trade_layout.h (EQUI_TradeWnd.xml) and
 * give_layout.h (EQUI_GiveWnd.xml); this file computes no rect.
 *
 * Behaviour rows cite review/UIGAP-ORACLE/212/trade.tsv (two official
 * Titanium clients, 2026-10-05).
 */

#include "client/graphics/ui/trade_panel.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"
#include "common/name_utils.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

namespace inv = eqt::inventory;

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kWhite(255, 255, 255, 255);
const irr::video::SColor kTitle(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 80, 80, 80);
const irr::video::SColor kQuantity(255, 180, 220, 180);
// MEASURED colours of TRDW_HisName / TRDW_MyName (trade.tsv rows 1, 4, 6;
// A.R09_A_press_Trade.png): a pure red and a pure green, like the oracle's
// label art. The exact RGB is read off the capture's dominant hue, not a
// number in a shipped file.
const irr::video::SColor kNameRed(255, 230, 40, 40);
const irr::video::SColor kNameGreen(255, 40, 220, 40);

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return r.isPointInside(irr::core::vector2di(x, y));
}

irr::video::SColor nameColor(TradeNameColor c) {
    switch (c) {
        case TradeNameColor::Green: return kNameGreen;
        case TradeNameColor::White: return kWhite;
        case TradeNameColor::Red:
        default: return kNameRed;
    }
}

void setSlot(TradeSlotDisplay& s, bool has, const std::string& name, uint32_t icon,
             int32_t qty) {
    s.hasItem = has;
    s.itemName = has ? name : std::string();
    s.iconId = has ? icon : 0;
    s.quantity = has ? qty : 0;
}

}  // namespace

// --- slot arithmetic ---------------------------------------------------------

int16_t tradeOwnSlotId(int i) {
    if (i < 0 || i >= TradePanelState::SLOT_COUNT) return inv::SLOT_INVALID;
    return static_cast<int16_t>(inv::TRADE_BEGIN + i);
}

int tradeOwnSlotCount(const TradePanelState& state) {
    return state.isNpc ? TradePanelState::GIVE_SLOT_COUNT : TradePanelState::SLOT_COUNT;
}

TradeNameColor tradeOwnNameColor(const TradePanelState& state) {
    // Row 4: A pressed Trade -> A's OWN name turned green on A's window.
    return state.ownAccepted ? TradeNameColor::Green : TradeNameColor::Red;
}

TradeNameColor tradePartnerNameColor(const TradePanelState& state) {
    // Row 9 (A.R23): the give window's GVW_NPCName is the XML's white.
    if (state.isNpc) return TradeNameColor::White;
    // Row 4: on B's window the PARTNER's name (Testchar) turned green.
    return state.partnerAccepted ? TradeNameColor::Green : TradeNameColor::Red;
}

HeldItemWorldClick classifyHeldItemWorldClick(uint16_t spawnId, bool isNpc, bool isCorpse) {
    if (spawnId == 0 || isCorpse) return HeldItemWorldClick::Drop;   // 151 rows 31-33
    return isNpc ? HeldItemWorldClick::Give                           // row 9
                 : HeldItemWorldClick::Trade;                         // row 1
}

// --- bridge arms -------------------------------------------------------------

void applyTradeStarted(TradePanelState& state, const eqt::state::TradeStartedData& data,
                       const std::string& ownName) {
    // A fresh session: whatever a previous one left is gone (the game thread
    // clears the 3000.. range at every end, T259, and republishes nothing).
    state = TradePanelState{};
    state.isOpen = true;
    state.isNpc = data.isNpc;
    state.partnerId = data.partnerId;
    state.partnerName = EQT::toDisplayName(data.partnerName);
    state.ownName = EQT::toDisplayName(ownName);
}

void applyTradeItemUpdated(TradePanelState& state, const eqt::state::TradeItemUpdatedData& data) {
    if (data.slot >= TradePanelState::SLOT_COUNT) return;
    TradeSlotDisplay& s = (data.who == 1) ? state.partner[data.slot] : state.own[data.slot];
    // itemId 0 is the publisher's "slot emptied" (TradeManager::
    // handlePartnerItemRemoved -> getPartnerItem() == nullptr).
    setSlot(s, data.itemId != 0, data.itemName, data.iconId, data.quantity);
}

void applyTradeMoneyUpdated(TradePanelState& state, const eqt::state::TradeMoneyUpdatedData& data) {
    uint32_t* row = (data.who == 1) ? state.partnerCoins : state.ownCoins;
    row[0] = data.platinum;
    row[1] = data.gold;
    row[2] = data.silver;
    row[3] = data.copper;
}

void applyTradeAcceptStateChanged(TradePanelState& state,
                                  const eqt::state::TradeAcceptStateChangedData& data) {
    state.ownAccepted = data.ownAccepted;
    state.partnerAccepted = data.partnerAccepted;
}

void applyTradeEnded(TradePanelState& state) {
    // Row 7 (completion) and row 8 (cancel): the window closes on both sides.
    state = TradePanelState{};
}

bool applyTradeSlotChanged(TradePanelState& state, const eqt::state::InventorySlotChangedData& data) {
    if (!inv::isTradeSlot(data.slotId)) return false;
    setSlot(state.own[data.slotId - inv::TRADE_BEGIN], data.hasItem, data.itemName, data.iconId,
            static_cast<int32_t>(data.quantity));
    return true;
}

// --- draw --------------------------------------------------------------------

namespace {

void drawCell(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& cell,
              const TradeSlotDisplay& s, bool hovered) {
    ui.drawSprite(cell, static_cast<uint8_t>(UISprite::SlotBackground));
    if (hovered) ui.drawRect(cell, irr::video::SColor(60, 255, 255, 255));
    if (!s.hasItem) return;
    const auto art = TradeLayout::itemArtRect(cell);
    const bool drewIcon = ui.drawItemIcon(art, s.iconId);
    if (!tb) return;
    if (!drewIcon) {
        tb->addText(s.itemName.substr(0, std::min<size_t>(3, s.itemName.size())),
                    art.UpperLeftCorner.X + 2, art.UpperLeftCorner.Y + 2, kText);
    }
    if (s.quantity > 1) {
        tb->addText(std::to_string(s.quantity), art.UpperLeftCorner.X + 2,
                    art.UpperLeftCorner.Y + 22, kQuantity);
    }
}

void drawCoinRow(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& row,
                 int denom, uint32_t amount, bool hovered) {
    ui.drawSprite(row, static_cast<uint8_t>(UISprite::SlotBackground));
    ui.drawFrame(row, kFrame);
    if (hovered) ui.drawRect(row, irr::video::SColor(50, 255, 255, 255));
    const auto decal = TradeLayout::coinDecalRect(row);
    ui.drawSprite(decal, static_cast<uint8_t>(static_cast<int>(UISprite::CoinPlatinum) + denom));
    if (tb) {
        tb->addText(std::to_string(amount), decal.LowerRightCorner.X + 6,
                    decal.UpperLeftCorner.Y + 2, kWhite);
    }
}

void drawButton(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
                const char* label, bool hovered) {
    ui.drawRect(r, hovered ? irr::video::SColor(220, 90, 110, 60)
                           : irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(r, kFrame);
    if (tb) tb->addTextCentered(label, r, kWhite);
}

void drawChrome(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& window,
                const irr::core::rect<irr::s32>& title, const irr::core::rect<irr::s32>& close,
                const char* text, bool closeHovered) {
    ui.drawPanel(window);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addTextCentered(text, title, kTitle);
    }
    ui.drawRect(close, closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                    : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);
}

void renderTrade(UIRenderer& ui, const UILayout& layout, const TradePanelState& s) {
    TextBatch* tb = ui.getTextBatch();
    drawChrome(ui, tb, TradeLayout::windowRect(layout), TradeLayout::titleRect(layout),
               TradeLayout::closeBoxRect(layout), "Trade", s.closeHovered);

    // TRDW_HisName / TRDW_MyName, AlignCenter, red until that side accepts.
    if (tb) {
        tb->addTextCentered(s.partnerName, TradeLayout::hisNameRect(layout),
                            nameColor(tradePartnerNameColor(s)));
        tb->addTextCentered(s.ownName, TradeLayout::myNameRect(layout),
                            nameColor(tradeOwnNameColor(s)));
    }
    for (int i = 0; i < TradePanelState::SLOT_COUNT; ++i) {
        drawCell(ui, tb, TradeLayout::hisSlotRect(layout, i), s.partner[i], false);
        drawCell(ui, tb, TradeLayout::mySlotRect(layout, i), s.own[i], s.hoveredOwnSlot == i);
    }
    for (int d = 0; d < TradePanelState::COIN_COUNT; ++d) {
        drawCoinRow(ui, tb, TradeLayout::hisCoinRect(layout, d), d, s.partnerCoins[d], false);
        drawCoinRow(ui, tb, TradeLayout::myCoinRect(layout, d), d, s.ownCoins[d],
                    s.hoveredCoin == d);
    }
    drawButton(ui, tb, TradeLayout::tradeButtonRect(layout), "Trade", s.acceptHovered);
    drawButton(ui, tb, TradeLayout::cancelButtonRect(layout), "Cancel", s.cancelHovered);
}

void renderGive(UIRenderer& ui, const UILayout& layout, const TradePanelState& s) {
    TextBatch* tb = ui.getTextBatch();
    drawChrome(ui, tb, GiveLayout::windowRect(layout), GiveLayout::titleRect(layout),
               GiveLayout::closeBoxRect(layout), "Give", s.closeHovered);
    if (tb) {
        tb->addTextCentered(s.partnerName, GiveLayout::nameRect(layout),
                            nameColor(tradePartnerNameColor(s)));
    }
    for (int i = 0; i < tradeOwnSlotCount(s); ++i) {
        drawCell(ui, tb, GiveLayout::slotRect(layout, i), s.own[i], s.hoveredOwnSlot == i);
    }
    for (int d = 0; d < TradePanelState::COIN_COUNT; ++d) {
        drawCoinRow(ui, tb, GiveLayout::coinRect(layout, d), d, s.ownCoins[d], s.hoveredCoin == d);
    }
    drawButton(ui, tb, GiveLayout::giveButtonRect(layout), "Give", s.acceptHovered);
    drawButton(ui, tb, GiveLayout::cancelButtonRect(layout), "Cancel", s.cancelHovered);
}

}  // namespace

void renderTradeWindow(UIRenderer& ui, const UILayout& layout, const TradePanelState& state) {
    if (!state.isOpen) return;
    if (state.isNpc) {
        renderGive(ui, layout, state);
    } else {
        renderTrade(ui, layout, state);
    }
}

// --- hit test ----------------------------------------------------------------

TradeHit hitTestTradeWindow(const UILayout& layout, const TradePanelState& state,
                            int mouseX, int mouseY) {
    TradeHit hit;
    if (!state.isOpen) return hit;

    if (state.isNpc) {
        if (!inside(GiveLayout::windowRect(layout), mouseX, mouseY)) return hit;
        hit.part = TradePart::Body;
        if (inside(GiveLayout::closeBoxRect(layout), mouseX, mouseY)) {
            hit.part = TradePart::CloseBox;
        } else if (inside(GiveLayout::giveButtonRect(layout), mouseX, mouseY)) {
            hit.part = TradePart::Accept;
        } else if (inside(GiveLayout::cancelButtonRect(layout), mouseX, mouseY)) {
            hit.part = TradePart::Cancel;
        } else {
            for (int i = 0; i < tradeOwnSlotCount(state); ++i) {
                if (inside(GiveLayout::slotRect(layout, i), mouseX, mouseY)) {
                    hit.part = TradePart::OwnSlot;
                    hit.index = i;
                    return hit;
                }
            }
            for (int d = 0; d < TradePanelState::COIN_COUNT; ++d) {
                if (inside(GiveLayout::coinRect(layout, d), mouseX, mouseY)) {
                    hit.part = TradePart::OwnCoin;
                    hit.index = d;
                    return hit;
                }
            }
        }
        return hit;
    }

    if (!inside(TradeLayout::windowRect(layout), mouseX, mouseY)) return hit;
    hit.part = TradePart::Body;
    if (inside(TradeLayout::closeBoxRect(layout), mouseX, mouseY)) {
        hit.part = TradePart::CloseBox;
        return hit;
    }
    if (inside(TradeLayout::tradeButtonRect(layout), mouseX, mouseY)) {
        hit.part = TradePart::Accept;
        return hit;
    }
    if (inside(TradeLayout::cancelButtonRect(layout), mouseX, mouseY)) {
        hit.part = TradePart::Cancel;
        return hit;
    }
    for (int i = 0; i < tradeOwnSlotCount(state); ++i) {
        if (inside(TradeLayout::mySlotRect(layout, i), mouseX, mouseY)) {
            hit.part = TradePart::OwnSlot;
            hit.index = i;
            return hit;
        }
        if (inside(TradeLayout::hisSlotRect(layout, i), mouseX, mouseY)) {
            hit.part = TradePart::PartnerSlot;
            hit.index = i;
            return hit;
        }
    }
    for (int d = 0; d < TradePanelState::COIN_COUNT; ++d) {
        if (inside(TradeLayout::myCoinRect(layout, d), mouseX, mouseY)) {
            hit.part = TradePart::OwnCoin;
            hit.index = d;
            return hit;
        }
        if (inside(TradeLayout::hisCoinRect(layout, d), mouseX, mouseY)) {
            hit.part = TradePart::PartnerCoin;
            hit.index = d;
            return hit;
        }
    }
    return hit;
}

}  // namespace Graphics
}  // namespace EQT
