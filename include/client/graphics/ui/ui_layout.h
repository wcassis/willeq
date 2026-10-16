/*
 * UILayout — fixed screen region definitions for the static layout UI.
 *
 * B1075 (#395): WHERE each window sits is the layout table
 * (ui_layout_table.h); this struct resolves its anchors for one screen.
 *
 * U03a: All UI element positions are computed from screen dimensions.
 * Regions use anchor-based positioning (top-left, bottom-center, etc.)
 * so they scale to any resolution. Call computeLayout(width, height)
 * at init and on resize.
 */

#pragma once

#include <irrlicht.h>

#include "common/packets/common.h"  // T483: the gem counts
#include "client/graphics/ui/hotbar_layout.h"  // B1025-H2: the stock window rect
#include "client/graphics/ui/ui_layout_table.h"  // B1075: the anchors + co-open sets

namespace EQT {
namespace Graphics {

struct UILayout {
    // Screen dimensions
    irr::s32 screenW = 800;
    irr::s32 screenH = 600;

    // Margins
    static constexpr irr::s32 MARGIN = 4;
    static constexpr irr::s32 BAR_HEIGHT = 14;
    static constexpr irr::s32 BAR_WIDTH = 160;
    static constexpr irr::s32 SLOT_SIZE = 32;

    // T419 (board B352): the pet command grid's row height. The panel is
    // sized from it so the ten indicators land INSIDE the frame -- before
    // this the row was drawn 34px down a 40px panel and hung out of it.
    // See include/client/graphics/ui/pet_layout.h for the oracle quote and
    // for why the grid is 5x2 rather than the XML's 2x5.
    static constexpr irr::s32 PET_BUTTON_H = 10;
    static constexpr irr::s32 PET_BUTTON_ROWS = 2;

    // B1075 (#395): fable's centre popup box -- the 400x350 every 'C' window
    // of the layout table is drawn into (the T415 scale). Its SIZE is the
    // windows' content and does not change; the table moves the box.
    static constexpr irr::s32 POPUP_W = 400;
    static constexpr irr::s32 POPUP_H = 350;

    // B1075: every anchor below is named per window in ui_layout_table.h
    // (kUILayoutTable); the comment on each member is its UIAnchor.

    // --- TopLeft: Player status ---
    irr::core::rect<irr::s32> playerStatus;     // Overall region
    irr::core::rect<irr::s32> playerHPBar;
    irr::core::rect<irr::s32> playerManaBar;
    irr::core::rect<irr::s32> playerStamBar;
    irr::core::rect<irr::s32> playerNameLabel;

    // --- UnderPlayerColumn: Target info ---
    irr::core::rect<irr::s32> targetInfo;
    irr::core::rect<irr::s32> targetHPBar;
    irr::core::rect<irr::s32> targetNameLabel;

    // --- BottomCentre: Hotbar (B1025-H2 window, B1075 placement) ---
    // The stock hotbar window's content (EQUI_HotButtonWnd.xml) laid out in
    // `hotbarColumns` columns -- the table's orientation (kHotbarOrientations),
    // HotbarLayout::slotRect(hotbar, i, hotbarColumns) for the cells.
    irr::core::rect<irr::s32> hotbar;
    irr::s32 hotbarColumns = HotbarLayout::kStockColumns;

    // --- BottomLeft: Chat panel ---
    irr::core::rect<irr::s32> chatPanel;
    irr::core::rect<irr::s32> chatMessages;
    irr::core::rect<irr::s32> chatInput;
    irr::core::rect<irr::s32> chatTabs;

    // --- RightEdgeAboveHotbar: Spell gems ---
    irr::core::rect<irr::s32> spellGems;

    // --- BesidePlayerTop: Buff bar ---
    irr::core::rect<irr::s32> buffBar;

    // --- AboveHotbar: Casting bar ---
    irr::core::rect<irr::s32> castingBar;

    // --- CentreSlot: the one-at-a-time centre popups (spellbook, skills,
    // tradeskill container), centred in the middle band ---
    irr::core::rect<irr::s32> centerPopup;

    // --- PairRight: the inventory, right half of the side-by-side pair ---
    irr::core::rect<irr::s32> inventoryPopup;

    // --- PairLeft: the window used WITH the inventory (merchant and loot
    // fill it; bank, trade and give are right-aligned in it at their own
    // oracle sizes via placePairLeft()) ---
    irr::core::rect<irr::s32> partnerPopup;

    // --- UnderPlayerColumn: Group panel, then the Pet panel ---
    irr::core::rect<irr::s32> groupPanel;
    irr::core::rect<irr::s32> petPanel;

    // --- FullWidthBottom: XP bar ---
    irr::core::rect<irr::s32> xpBar;

    /** PairLeft at a window's own size: right-aligned to the inventory, top-aligned with it. */
    irr::core::rect<irr::s32> placePairLeft(irr::s32 w, irr::s32 h) const {
        irr::s32 x = partnerPopup.LowerRightCorner.X - w;
        irr::s32 y = partnerPopup.UpperLeftCorner.Y;
        return clampOnScreen(x, y, w, h);
    }

    /** ScreenCentre at a window's own size, clamped wholly on screen. */
    irr::core::rect<irr::s32> placeScreenCentre(irr::s32 w, irr::s32 h) const {
        return clampOnScreen((screenW - w) / 2, (screenH - h) / 2, w, h);
    }

    irr::core::rect<irr::s32> clampOnScreen(irr::s32 x, irr::s32 y,
                                            irr::s32 w, irr::s32 h) const {
        if (x + w > screenW) x = screenW - w;
        if (y + h > screenH) y = screenH - h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        return {x, y, x + w, y + h};
    }

    /**
     * Compute all regions from screen dimensions, from the anchors named in
     * ui_layout_table.h. B1075 moved the HUD to the WoW-style arrangement
     * Will confirmed on #395 (note 5180); every window keeps its size.
     */
    void computeLayout(irr::s32 width, irr::s32 height) {
        screenW = width;
        screenH = height;

        // TopLeft: Player status
        irr::s32 psX = MARGIN;
        irr::s32 psY = MARGIN;
        playerNameLabel = {psX, psY, psX + BAR_WIDTH, psY + BAR_HEIGHT};
        playerHPBar     = {psX, psY + BAR_HEIGHT + 2, psX + BAR_WIDTH, psY + BAR_HEIGHT * 2 + 2};
        playerManaBar   = {psX, psY + BAR_HEIGHT * 2 + 4, psX + BAR_WIDTH, psY + BAR_HEIGHT * 3 + 4};
        playerStamBar   = {psX, psY + BAR_HEIGHT * 3 + 6, psX + BAR_WIDTH, psY + BAR_HEIGHT * 4 + 6};
        playerStatus    = {psX, psY, psX + BAR_WIDTH, playerStamBar.LowerRightCorner.Y + MARGIN};

        // UnderPlayerColumn: Target info, under the player frame
        irr::s32 tX = MARGIN;
        irr::s32 tY = playerStatus.LowerRightCorner.Y + MARGIN;
        targetNameLabel = {tX, tY, tX + BAR_WIDTH, tY + BAR_HEIGHT};
        targetHPBar     = {tX, tY + BAR_HEIGHT + 2, tX + BAR_WIDTH, tY + BAR_HEIGHT * 2 + 2};
        targetInfo      = {tX, tY, tX + BAR_WIDTH, targetHPBar.LowerRightCorner.Y + MARGIN};

        // UnderPlayerColumn: Group panel, under the target
        irr::s32 gpX = MARGIN;
        irr::s32 gpY = targetInfo.LowerRightCorner.Y + MARGIN;
        irr::s32 gpW = BAR_WIDTH;
        irr::s32 gpH = 5 * (BAR_HEIGHT + MARGIN) + MARGIN;
        groupPanel = {gpX, gpY, gpX + gpW, gpY + gpH};

        // UnderPlayerColumn: Pet panel, under the group. Name row + HP bar +
        // the command grid (T419/B352 -- the grid used to be drawn past the
        // bottom edge).
        irr::s32 ppY = groupPanel.LowerRightCorner.Y + MARGIN;
        petPanel = {gpX, ppY, gpX + gpW,
                    ppY + BAR_HEIGHT * 2 + MARGIN + PET_BUTTON_ROWS * PET_BUTTON_H};

        // BesidePlayerTop: Buff bar, the top strip right of the player frame
        irr::s32 buffW = 300;
        irr::s32 buffH = SLOT_SIZE + MARGIN * 2;
        irr::s32 buffX = playerStatus.LowerRightCorner.X + MARGIN;
        buffBar = {buffX, MARGIN, buffX + buffW, MARGIN + buffH};

        // FullWidthBottom: XP bar
        xpBar = {0, height - 8, width, height};

        // BottomLeft: Chat panel, 300x160
        irr::s32 chatW = 300;
        irr::s32 chatH = 160;
        irr::s32 chatX = MARGIN;
        irr::s32 chatY = height - MARGIN - chatH;
        chatTabs     = {chatX, chatY, chatX + chatW, chatY + BAR_HEIGHT + 2};
        chatMessages = {chatX, chatTabs.LowerRightCorner.Y, chatX + chatW, chatY + chatH - BAR_HEIGHT - 4};
        chatInput    = {chatX, chatMessages.LowerRightCorner.Y + 2, chatX + chatW, chatY + chatH};
        chatPanel    = {chatX, chatY, chatX + chatW, chatY + chatH};

        // BottomCentre: Hotbar. The first orientation in the table that fits
        // between the chat and the right edge; centred on the screen, pushed
        // right of the chat when centring would cover it. Its top keeps the
        // stock client's bottom-anchored rule (defaults.ini [HotButtonWnd]:
        // top = screenHeight - 30 - window height).
        const irr::s32 hbMinX = chatPanel.LowerRightCorner.X + MARGIN;
        hotbarColumns = HotbarLayout::kStockColumns;
        for (int cols : kHotbarOrientations) {
            if (hbMinX + HotbarLayout::windowW(cols) <= width - MARGIN) {
                hotbarColumns = cols;
                break;
            }
        }
        {
            const irr::s32 hbW = HotbarLayout::windowW(hotbarColumns);
            const irr::s32 hbH = HotbarLayout::windowH(hotbarColumns);
            irr::s32 hbX = (width - hbW) / 2;
            if (hbX < hbMinX) hbX = hbMinX;
            hotbar = clampOnScreen(hbX, height - 30 - hbH, hbW, hbH);
        }

        // RightEdgeAboveHotbar: Spell gems, the right edge, ending at the
        // hotbar's top (test_ninth_spell_gem pins that end).
        irr::s32 gemsW = SLOT_SIZE;
        // T483: EQT::VISIBLE_SPELL_GEM_COUNT gems, the official skin's eight.
        const irr::s32 gemCount = static_cast<irr::s32>(EQT::VISIBLE_SPELL_GEM_COUNT);
        irr::s32 gemsH = SLOT_SIZE * gemCount + MARGIN * (gemCount - 1);
        irr::s32 gemsX = width - MARGIN - gemsW;
        irr::s32 gemsY = hotbar.UpperLeftCorner.Y - MARGIN - gemsH;
        spellGems = {gemsX, gemsY, gemsX + gemsW, gemsY + gemsH};

        // AboveHotbar: Casting bar, centred over the hotbar window.
        irr::s32 castW = 200;
        irr::s32 castH = BAR_HEIGHT + 4;
        irr::s32 castX = (hotbar.UpperLeftCorner.X + hotbar.LowerRightCorner.X - castW) / 2;
        castingBar = {castX, hotbar.UpperLeftCorner.Y - MARGIN - castH,
                      castX + castW, hotbar.UpperLeftCorner.Y - MARGIN};

        // The middle band: right of the player column, left of the gem
        // column, under the buff strip, above the chat and the casting bar.
        const irr::s32 bandL = groupPanel.LowerRightCorner.X + MARGIN;
        const irr::s32 bandR = spellGems.UpperLeftCorner.X - MARGIN;
        const irr::s32 bandT = buffBar.LowerRightCorner.Y + MARGIN;
        irr::s32 bandB = castingBar.UpperLeftCorner.Y - MARGIN;
        if (chatPanel.UpperLeftCorner.Y - MARGIN < bandB)
            bandB = chatPanel.UpperLeftCorner.Y - MARGIN;
        irr::s32 popY = bandT + (bandB - bandT - POPUP_H) / 2;
        if (popY < bandT) popY = bandT;
        if (popY + POPUP_H > height) popY = height - POPUP_H;
        if (popY < 0) popY = 0;

        // CentreSlot: centred in the band (clamped to the screen when the
        // band is narrower than the box).
        {
            irr::s32 x = bandL + (bandR - bandL - POPUP_W) / 2;
            if (bandR - bandL < POPUP_W) x = (width - POPUP_W) / 2;
            centerPopup = clampOnScreen(x, popY, POPUP_W, POPUP_H);
        }

        // PairLeft + PairRight: side by side, MARGIN apart, centred in the
        // band. A screen too narrow for the pair inside the band (800x600:
        // the pair is 804 wide) centres it on the screen instead, closing
        // the gap to what the width allows (zero at 800).
        {
            irr::s32 gap = MARGIN;
            irr::s32 pairW = POPUP_W * 2 + gap;
            irr::s32 x;
            if (bandR - bandL >= pairW) {
                x = bandL + (bandR - bandL - pairW) / 2;
            } else {
                if (width < pairW) gap = width - POPUP_W * 2;
                if (gap < 0) gap = 0;
                pairW = POPUP_W * 2 + gap;
                x = (width - pairW) / 2;
                if (x < 0) x = 0;
            }
            partnerPopup = clampOnScreen(x, popY, POPUP_W, POPUP_H);
            inventoryPopup = clampOnScreen(x + POPUP_W + gap, popY, POPUP_W, POPUP_H);
        }
    }
};

} // namespace Graphics
} // namespace EQT
