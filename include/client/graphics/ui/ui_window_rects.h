/*
 * windowRectFor() -- B1075 (#395): the screen rect each row of the layout
 * table (ui_layout_table.h) resolves to, through the SAME function the
 * window's own render body and hit test use. Nothing here computes a
 * position; every arm forwards to the UILayout member or the window's own
 * `<Window>Layout::windowRect()`, which in turn reads its anchor from the
 * table. tests/test_ui_layout_coopen.cpp asserts the required co-open sets
 * over these rects, so a window whose code stopped reading its anchor would
 * be caught there.
 *
 * Header-only (every layout header it includes is), so a headless test can
 * use it without linking a UI translation unit.
 */

#pragma once

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_layout_table.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/combat_ability_layout.h"
#include "client/graphics/ui/confirmation_layout.h"
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/friends_layout.h"
#include "client/graphics/ui/give_layout.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/item_display_layout.h"
#include "client/graphics/ui/merchant_layout.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/quantity_layout.h"
#include "client/graphics/ui/tip_layout.h"
#include "client/graphics/ui/tracking_layout.h"
#include "client/graphics/ui/trade_layout.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/train_layout.h"

namespace EQT {
namespace Graphics {

inline irr::core::rect<irr::s32> windowRectFor(UIWindow w, const UILayout& l) {
    switch (w) {
        case UIWindow::PlayerStatus:    return l.playerStatus;
        case UIWindow::Target:          return l.targetInfo;
        case UIWindow::Group:           return l.groupPanel;
        case UIWindow::Pet:             return l.petPanel;
        case UIWindow::Buffs:           return l.buffBar;
        case UIWindow::Chat:            return l.chatPanel;
        case UIWindow::Hotbar:          return l.hotbar;
        case UIWindow::SpellGems:       return l.spellGems;
        case UIWindow::CastingBar:      return l.castingBar;
        case UIWindow::XpBar:           return l.xpBar;
        case UIWindow::CombatAbilities: return CombatAbilityLayout::windowRect(l);
        case UIWindow::Inventory:       return InventoryLayout::windowRect(l);
        case UIWindow::Spellbook:       return l.centerPopup;
        case UIWindow::Skills:          return l.centerPopup;
        case UIWindow::Tradeskill:      return TradeskillLayout::windowRect(l);
        case UIWindow::Merchant:        return MerchantLayout::windowRect(l);
        case UIWindow::Loot:            return l.partnerPopup;
        case UIWindow::Bank:            return BankLayout::windowRect(l);
        case UIWindow::Trade:           return TradeLayout::windowRect(l);
        case UIWindow::Give:            return GiveLayout::windowRect(l);
        case UIWindow::Container:       return ContainerLayout::windowRect(l);
        case UIWindow::ItemDisplay:     return ItemDisplayLayout::windowRect(l);
        case UIWindow::Options:         return OptionsLayout::windowRect(l);
        case UIWindow::Quantity:        return QuantityLayout::windowRect(l);
        case UIWindow::Confirmation:    return ConfirmationLayout::windowRect(l);
        case UIWindow::Trainer:         return TrainLayout::windowRect(l);
        case UIWindow::Tracking:        return TrackingLayout::windowRect(l);
        case UIWindow::Tip:             return TipLayout::windowRect(l);
        case UIWindow::Friends:         return FriendsLayout::windowRect(l);
        case UIWindow::Count:           break;
    }
    return {0, 0, 0, 0};
}

}  // namespace Graphics
}  // namespace EQT
