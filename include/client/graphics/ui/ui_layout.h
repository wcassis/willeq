/*
 * UILayout — fixed screen region definitions for the static layout UI.
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

    // --- Top-left: Player status ---
    irr::core::rect<irr::s32> playerStatus;     // Overall region
    irr::core::rect<irr::s32> playerHPBar;
    irr::core::rect<irr::s32> playerManaBar;
    irr::core::rect<irr::s32> playerStamBar;
    irr::core::rect<irr::s32> playerNameLabel;

    // --- Top-right: Target info ---
    irr::core::rect<irr::s32> targetInfo;
    irr::core::rect<irr::s32> targetHPBar;
    irr::core::rect<irr::s32> targetNameLabel;

    // --- Bottom-left: Hotbar (B1025-H2) + chat panel ---
    // The stock hotbar window (EQUI_HotButtonWnd.xml, bottom-LEFT) and the
    // chat panel that sits to its right. The hotbar's geometry lives in
    // hotbar_layout.h (HotbarLayout::windowRect); this member is the window
    // rect computed for this screen, so the always-on panels' input guards
    // and the chat's own placement share one rect.
    irr::core::rect<irr::s32> hotbar;
    irr::core::rect<irr::s32> chatPanel;
    irr::core::rect<irr::s32> chatMessages;
    irr::core::rect<irr::s32> chatInput;
    irr::core::rect<irr::s32> chatTabs;

    // --- Bottom-right: Spell gems ---
    irr::core::rect<irr::s32> spellGems;

    // --- Above chat: Buff bar ---
    irr::core::rect<irr::s32> buffBar;

    // --- Center-bottom: Casting bar ---
    irr::core::rect<irr::s32> castingBar;

    // --- Center: Popup area (inventory, spellbook, vendor, etc.) ---
    irr::core::rect<irr::s32> centerPopup;

    // --- Left side below player status: Group panel ---
    irr::core::rect<irr::s32> groupPanel;

    // --- Below group: Pet panel ---
    irr::core::rect<irr::s32> petPanel;

    // --- XP bar (very bottom, full width) ---
    irr::core::rect<irr::s32> xpBar;

    /**
     * Compute all regions from screen dimensions.
     */
    void computeLayout(irr::s32 width, irr::s32 height) {
        screenW = width;
        screenH = height;

        // Player status: top-left
        irr::s32 psX = MARGIN;
        irr::s32 psY = MARGIN;
        playerNameLabel = {psX, psY, psX + BAR_WIDTH, psY + BAR_HEIGHT};
        playerHPBar     = {psX, psY + BAR_HEIGHT + 2, psX + BAR_WIDTH, psY + BAR_HEIGHT * 2 + 2};
        playerManaBar   = {psX, psY + BAR_HEIGHT * 2 + 4, psX + BAR_WIDTH, psY + BAR_HEIGHT * 3 + 4};
        playerStamBar   = {psX, psY + BAR_HEIGHT * 3 + 6, psX + BAR_WIDTH, psY + BAR_HEIGHT * 4 + 6};
        playerStatus    = {psX, psY, psX + BAR_WIDTH, playerStamBar.LowerRightCorner.Y + MARGIN};

        // Target info: top-right
        irr::s32 tX = width - MARGIN - BAR_WIDTH;
        irr::s32 tY = MARGIN;
        targetNameLabel = {tX, tY, tX + BAR_WIDTH, tY + BAR_HEIGHT};
        targetHPBar     = {tX, tY + BAR_HEIGHT + 2, tX + BAR_WIDTH, tY + BAR_HEIGHT * 2 + 2};
        targetInfo      = {tX, tY, tX + BAR_WIDTH, targetHPBar.LowerRightCorner.Y + MARGIN};

        // XP bar: very bottom, full width
        xpBar = {0, height - 8, width, height};

        // Hotbar: the stock window, bottom-LEFT (B1025-H2). Geometry is the
        // oracle's own, in hotbar_layout.h; this rect is where it lands on
        // this screen (clamped for the shortest supported heights).
        hotbar = HotbarLayout::windowRect(width, height);

        // Spell gems: bottom-right, above XP bar
        irr::s32 gemsW = SLOT_SIZE;
        // T483: EQT::VISIBLE_SPELL_GEM_COUNT gems, the official skin's eight.
        const irr::s32 gemCount = static_cast<irr::s32>(EQT::VISIBLE_SPELL_GEM_COUNT);
        irr::s32 gemsH = SLOT_SIZE * gemCount + MARGIN * (gemCount - 1);
        irr::s32 gemsX = width - MARGIN - gemsW;
        irr::s32 gemsY = hotbar.UpperLeftCorner.Y - MARGIN - gemsH;
        spellGems = {gemsX, gemsY, gemsX + gemsW, gemsY + gemsH};

        // Chat panel: to the RIGHT of the hotbar window (B1025-H2). The oracle
        // puts the hotbar bottom-LEFT and the chat beside it; fable's chat was
        // bottom-LEFT, so it moves right to make room. Bottom-anchored like
        // the hotbar, same 300x160 size as before.
        irr::s32 chatW = 300;
        irr::s32 chatH = 160;
        irr::s32 chatX = hotbar.LowerRightCorner.X + MARGIN;
        if (chatX + chatW > width - MARGIN) chatX = MARGIN;
        irr::s32 chatY = height - MARGIN - chatH;
        chatTabs     = {chatX, chatY, chatX + chatW, chatY + BAR_HEIGHT + 2};
        chatMessages = {chatX, chatTabs.LowerRightCorner.Y, chatX + chatW, chatY + chatH - BAR_HEIGHT - 4};
        chatInput    = {chatX, chatMessages.LowerRightCorner.Y + 2, chatX + chatW, chatY + chatH};
        chatPanel    = {chatX, chatY, chatX + chatW, chatY + chatH};

        // Buff bar: above the chat panel -- the panel table's own "Above
        // chat". B1025-H2 sat it above the hotbar window instead, which at
        // 640x480 (buff top 146) collided with the pet panel (bottom 206);
        // PetButtonsT419 pins the clearance. Above the chat it clears the
        // pet panel at every supported resolution (top = height - 208).
        irr::s32 buffW = 300;
        irr::s32 buffH = SLOT_SIZE + MARGIN * 2;
        buffBar = {chatX, chatY - MARGIN - buffH, chatX + buffW, chatY - MARGIN};

        // Casting bar: center-bottom, above the hotbar window's TOP and clear
        // of the window's right edge (the window is 96 wide at the bottom-left
        // now, so a centered 200 bar no longer clears it).
        irr::s32 castW = 200;
        irr::s32 castH = BAR_HEIGHT + 4;
        irr::s32 castX = (hotbar.LowerRightCorner.X + width) / 2;
        if (castX + castW > width - MARGIN) castX = width - MARGIN - castW;
        castingBar = {castX, hotbar.UpperLeftCorner.Y - MARGIN - castH,
                      castX + castW, hotbar.UpperLeftCorner.Y - MARGIN};

        // Center popup: middle of screen
        irr::s32 popW = 400;
        irr::s32 popH = 350;
        centerPopup = {(width - popW) / 2, (height - popH) / 2,
                       (width + popW) / 2, (height + popH) / 2};

        // Group panel: beside the hotbar window, below player status. The
        // column used to sit at x=MARGIN, where it overlaps the window's
        // top-left corner (the window is 96 wide and reaches y=220 at 480px
        // tall), so it starts just clear of the window's right edge.
        irr::s32 gpX = hotbar.LowerRightCorner.X + MARGIN;
        irr::s32 gpY = playerStatus.LowerRightCorner.Y + MARGIN;
        irr::s32 gpW = BAR_WIDTH;
        irr::s32 gpH = 5 * (BAR_HEIGHT + MARGIN) + MARGIN;
        groupPanel = {gpX, gpY, gpX + gpW, gpY + gpH};

        // Pet panel: below group. Name row + HP bar + the command grid
        // (T419/B352 -- the grid used to be drawn past the bottom edge).
        // At 640x480 this bottom edge lands at y=224 with the buff bar's top
        // at y=228, so the taller panel still clears it.
        irr::s32 ppY = groupPanel.LowerRightCorner.Y + MARGIN;
        petPanel = {gpX, ppY, gpX + gpW,
                    ppY + BAR_HEIGHT * 2 + MARGIN + PET_BUTTON_ROWS * PET_BUTTON_H};
    }
};

} // namespace Graphics
} // namespace EQT
