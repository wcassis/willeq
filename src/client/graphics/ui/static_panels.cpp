/*
 * Static UI panel render functions.
 * U03d: Player status + target info as proof of concept.
 */

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/pet_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/inventory/inventory_constants.h"
#include "client/chat/chat_message_buffer.h"
#include "client/chat/chat_text_colors.h"
#include "client/graphics/text_batch.h"
#include "client/state/event_bus.h"  // T156: applyHotbarSlotAssigned
#include "client/hotbar/hotbar_model.h"
#include <fmt/format.h>
#include <chrono>

namespace EQT {
namespace Graphics {

void renderPlayerStatus(UIRenderer& ui, const UILayout& layout,
                        const PlayerStatsData& stats) {
    // Panel background
    ui.drawPanel(layout.playerStatus);

    // Name + level
    if (auto* tb = ui.getTextBatch()) {
        std::string label = fmt::format("{} ({})", stats.name, stats.level);
        tb->addText(label, layout.playerNameLabel.UpperLeftCorner.X + 2,
                    layout.playerNameLabel.UpperLeftCorner.Y + 1,
                    irr::video::SColor(255, 255, 255, 255));
    }

    // HP bar
    float hpPct = stats.maxHP > 0 ? static_cast<float>(stats.curHP) / stats.maxHP : 0.0f;
    ui.drawBarSprite(layout.playerHPBar, hpPct,
        static_cast<uint8_t>(UISprite::BarHP),
        static_cast<uint8_t>(UISprite::BarBackground));

    if (auto* tb = ui.getTextBatch()) {
        std::string hpText = fmt::format("{}/{}", stats.curHP, stats.maxHP);
        tb->addTextCentered(hpText, layout.playerHPBar,
            irr::video::SColor(255, 255, 255, 255));
    }

    // Mana bar
    float manaPct = stats.maxMana > 0 ? static_cast<float>(stats.curMana) / stats.maxMana : 0.0f;
    ui.drawBarSprite(layout.playerManaBar, manaPct,
        static_cast<uint8_t>(UISprite::BarMana),
        static_cast<uint8_t>(UISprite::BarBackground));

    if (auto* tb = ui.getTextBatch()) {
        std::string manaText = fmt::format("{}/{}", stats.curMana, stats.maxMana);
        tb->addTextCentered(manaText, layout.playerManaBar,
            irr::video::SColor(255, 255, 255, 255));
    }

    // Stamina bar
    float stamPct = stats.maxEndurance > 0 ? static_cast<float>(stats.curEndurance) / stats.maxEndurance : 0.0f;
    ui.drawBarSprite(layout.playerStamBar, stamPct,
        static_cast<uint8_t>(UISprite::BarStamina),
        static_cast<uint8_t>(UISprite::BarBackground));

    if (auto* tb = ui.getTextBatch()) {
        std::string stamText = fmt::format("{}/{}", stats.curEndurance, stats.maxEndurance);
        tb->addTextCentered(stamText, layout.playerStamBar,
            irr::video::SColor(255, 255, 255, 255));
    }
}

std::string percentText(uint8_t percent) {
    // NOT "{}%%" -- fmt copies a literal `%` through verbatim, so that form
    // printed the sign twice. See the declaration for the three panels it hit.
    return fmt::format("{}%", percent);
}

void renderTargetInfo(UIRenderer& ui, const UILayout& layout,
                      const TargetInfoData& target) {
    if (target.spawnId == 0) return;  // No target

    // Panel background
    ui.drawPanel(layout.targetInfo);

    // Name + level
    if (auto* tb = ui.getTextBatch()) {
        std::string label = fmt::format("{} ({})", target.name, target.level);
        tb->addText(label, layout.targetNameLabel.UpperLeftCorner.X + 2,
                    layout.targetNameLabel.UpperLeftCorner.Y + 1,
                    irr::video::SColor(255, 255, 255, 255));
    }

    // HP bar
    float hpPct = target.hpPercent / 100.0f;
    ui.drawBarSprite(layout.targetHPBar, hpPct,
        static_cast<uint8_t>(UISprite::BarHP),
        static_cast<uint8_t>(UISprite::BarBackground));

    if (auto* tb = ui.getTextBatch()) {
        std::string hpText = percentText(target.hpPercent);
        tb->addTextCentered(hpText, layout.targetHPBar,
            irr::video::SColor(255, 255, 255, 255));
    }
}

int chatVisibleLines(const UILayout& layout, int lineHeight) {
    if (lineHeight <= 0) lineHeight = 14;
    // Same two edges renderChatPanel() measures between.
    int h = (layout.chatMessages.LowerRightCorner.Y - 1) -
            (layout.chatMessages.UpperLeftCorner.Y + 1);
    int lines = h / lineHeight;
    return lines > 0 ? lines : 1;
}

void renderChatPanel(UIRenderer& ui, const UILayout& layout,
                     ChatPanelState& state) {
    auto* tb = ui.getTextBatch();
    if (!tb) return;

    irr::s32 lineH = tb->getLineHeight();
    if (lineH <= 0) lineH = 14;

    // Panel background — visible when input is active or mouse hovers, otherwise transparent
    if (state.inputActive || state.mouseHover) {
        ui.drawPanel(layout.chatPanel);
    }

    // Process any pending messages from network thread
    if (state.messageBuffer) {
        state.messageBuffer->processPending();
    }

    // Render messages with word wrapping
    if (state.messageBuffer && state.messageBuffer->size() > 0) {
        irr::s32 areaX = layout.chatMessages.UpperLeftCorner.X + 2;
        irr::s32 areaTop = layout.chatMessages.UpperLeftCorner.Y + 1;
        irr::s32 areaBottom = layout.chatMessages.LowerRightCorner.Y - 1;
        irr::s32 areaW = layout.chatMessages.getWidth() - 18;  // 14px scrollbar + 4px padding
        if (areaW <= 0) areaW = 100;

        // T462 (board B505): the wrapped lines (newest at end) come from the
        // panel's wrap cache, which formats and word-wraps only the messages
        // that arrived since the last frame. This used to re-wrap the WHOLE
        // buffer every frame -- the Static UI growth T459 measured on the Pi.
        const uint64_t wrappedBefore = state.wrapCache.messagesWrapped();
        state.wrapCache.sync(*state.messageBuffer, areaW, state.showTimestamps,
                             [tb](const std::string& s) { return tb->getTextWidth(s); });
        state.messagesWrapped += state.wrapCache.messagesWrapped() - wrappedBefore;
        const ChatWrapCache& wrappedLines = state.wrapCache;

        // Calculate visible lines
        int visibleLines = (areaBottom - areaTop) / lineH;
        if (visibleLines <= 0) visibleLines = 1;
        int totalLines = static_cast<int>(wrappedLines.lineCount());

        // Clamp scroll offset
        int maxScroll = totalLines - visibleLines;
        if (maxScroll < 0) maxScroll = 0;
        if (state.scrollOffset > maxScroll) state.scrollOffset = maxScroll;
        if (state.scrollOffset < 0) state.scrollOffset = 0;

        // Draw visible lines from bottom up
        int startIdx = totalLines - visibleLines - state.scrollOffset;
        if (startIdx < 0) startIdx = 0;
        int endIdx = startIdx + visibleLines;
        if (endIdx > totalLines) endIdx = totalLines;

        irr::s32 y = areaTop;
        for (int i = startIdx; i < endIdx; ++i) {
            const ChatWrappedLine& line = wrappedLines.line(static_cast<size_t>(i));
            // B993: the ONE place a chat line's colour is decided -- its
            // [TextColors] row in the player's table, else its own colour.
            const irr::video::SColor color(eqt::ui::resolveChatLineColor(
                state.textColors, line.userColor, line.color.color));
            tb->addText(line.text, areaX, y, color);
            y += lineH;
        }

        // Scrollbar (right edge of chat messages area)
        if (totalLines > visibleLines) {
            irr::s32 sbX = layout.chatMessages.LowerRightCorner.X - 10;
            irr::s32 sbTop = areaTop;
            irr::s32 sbBottom = areaBottom;
            irr::s32 sbH = sbBottom - sbTop;

            // Track background
            ui.drawRect({sbX, sbTop, sbX + 8, sbBottom},
                irr::video::SColor(100, 30, 30, 40));

            // Thumb position and size
            float thumbRatio = static_cast<float>(visibleLines) / totalLines;
            irr::s32 thumbH = std::max(static_cast<irr::s32>(sbH * thumbRatio), static_cast<irr::s32>(10));
            float scrollRatio = (maxScroll > 0) ?
                static_cast<float>(maxScroll - state.scrollOffset) / maxScroll : 0.0f;
            irr::s32 thumbY = sbTop + static_cast<irr::s32>(scrollRatio * (sbH - thumbH));

            ui.drawRect({sbX + 1, thumbY, sbX + 7, thumbY + thumbH},
                irr::video::SColor(180, 120, 120, 140));
        }
    }

    // Input field background (brighter when active)
    ui.drawRect(layout.chatInput, state.inputActive ?
        irr::video::SColor(200, 20, 20, 40) : irr::video::SColor(200, 5, 5, 10));

    // Input field border (highlight when active)
    irr::video::SColor inputBorder = state.inputActive ?
        irr::video::SColor(255, 140, 140, 180) : irr::video::SColor(255, 80, 80, 90);
    irr::s32 ix1 = layout.chatInput.UpperLeftCorner.X;
    irr::s32 iy1 = layout.chatInput.UpperLeftCorner.Y;
    irr::s32 ix2 = layout.chatInput.LowerRightCorner.X;
    irr::s32 iy2 = layout.chatInput.LowerRightCorner.Y;
    (void)iy2;
    ui.drawRect({ix1, iy1, ix2, iy1 + 1}, inputBorder);

    // Input text — scroll horizontally so cursor is always visible
    irr::s32 inputFieldX = layout.chatInput.UpperLeftCorner.X + 2;
    irr::s32 inputFieldY = layout.chatInput.UpperLeftCorner.Y + 2;
    irr::s32 inputFieldW = layout.chatInput.getWidth() - 4;

    if (!state.inputText.empty()) {
        // Measure text up to cursor
        std::string textToCursor = state.inputText.substr(0, state.cursorPos);
        irr::s32 cursorPixelX = tb->getTextWidth(textToCursor);

        // Calculate scroll offset so cursor stays within visible area
        irr::s32 scrollPx = 0;
        if (cursorPixelX > inputFieldW - 4) {
            scrollPx = cursorPixelX - inputFieldW + 4;
        }

        tb->addText(state.inputText, inputFieldX - scrollPx, inputFieldY,
            irr::video::SColor(255, 255, 255, 255));
    }

    // Blinking cursor when chat input is active
    if (state.inputActive) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        if ((ms / 500) % 2 == 0) {
            std::string textToCursor = state.inputText.substr(0, state.cursorPos);
            irr::s32 cursorPixelX = tb->getTextWidth(textToCursor);
            irr::s32 scrollPx = 0;
            if (cursorPixelX > inputFieldW - 4) {
                scrollPx = cursorPixelX - inputFieldW + 4;
            }
            irr::s32 cursorX = inputFieldX + cursorPixelX - scrollPx;
            ui.drawRect({cursorX, inputFieldY, cursorX + 1, inputFieldY + lineH - 2},
                irr::video::SColor(255, 255, 255, 255));
        }
    }
}

void renderInventoryPopup(UIRenderer& ui, const UILayout& layout,
                          const InventoryPanelState& inv) {
    if (inv.activePopup != PopupType::Inventory) return;

    auto* tb = ui.getTextBatch();
    const auto& popup = layout.centerPopup;

    // Panel background
    ui.drawPanel(popup);

    // Title
    if (tb) {
        tb->addText("Inventory", popup.UpperLeftCorner.X + 8, popup.UpperLeftCorner.Y + 4,
            irr::video::SColor(255, 255, 215, 0));
    }

    constexpr irr::s32 SLOT = UILayout::SLOT_SIZE;

    // Equipment slots: EQ-authentic body arrangement (two flanking columns
    // + a bottom weapon row). All geometry comes from the single shared
    // source, InventoryLayout::slotRect() — this function contains no
    // slot-position arithmetic of its own (T57).
    for (int i = 0; i < InventoryPanelState::EQUIP_SLOTS; ++i) {
        irr::core::rect<irr::s32> slotRect = InventoryLayout::slotRect(layout, i);

        // Slot background
        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        // Border (hover highlight or normal)
        uint8_t borderSprite = (inv.hoveredSlot == i)
            ? static_cast<uint8_t>(UISprite::SlotBorderHover)
            : static_cast<uint8_t>(UISprite::SlotBorderNormal);
        ui.drawSprite(slotRect, borderSprite);

        const auto& slot = inv.equipSlots[i];
        // T430 (board B376): the real dragitem art. The oracle draws an
        // A_DragItem decal into every equipment slot; drawItemIcon() returns
        // false only when the sheets are unavailable or the id carries no
        // art, and then the pre-T430 three-character abbreviation stands in
        // exactly as it did.
        const bool drewIcon =
            slot.hasItem && ui.drawItemIcon(slotRect, slot.iconId);
        if (slot.hasItem && !drewIcon && tb) {
            std::string abbrev = slot.itemName.substr(0, 3);
            tb->addText(abbrev, slotRect.UpperLeftCorner.X + 2,
                slotRect.UpperLeftCorner.Y + SLOT / 2 - 6,
                irr::video::SColor(255, 200, 200, 200));
        } else if (!slot.hasItem && tb) {
            // Empty slot: canonical short label from inventory_constants.h
            const char* name = eqt::inventory::getSlotName(static_cast<int16_t>(i));
            std::string abbrev(name, 0, 3);
            tb->addText(abbrev, slotRect.UpperLeftCorner.X + 2,
                slotRect.UpperLeftCorner.Y + SLOT / 2 - 6,
                irr::video::SColor(120, 140, 140, 140));
        }
    }

    // Character paperdoll (T60): fills the center T54/T57 left empty,
    // between the equipment columns and above the weapon row. Rect comes
    // from the same shared geometry source the slots use
    // (InventoryLayout::paperdollRect()) — no independent arithmetic here.
    // The texture is rendered elsewhere (IrrlichtRenderer's
    // CharacterModelView::renderToTexture(), only when dirty); this
    // function only blits whatever texture is current this frame, or
    // draws nothing if the doll hasn't produced one yet (lazy
    // construction, or RTT unavailable — see the plan's Step 5).
    if (inv.paperdollTexture) {
        ui.drawTexture(InventoryLayout::paperdollRect(layout), inv.paperdollTexture);
    }

    // General inventory: row of 8 slots below the weapon row. The row is
    // horizontally centered in the popup (see InventoryLayout), leaving
    // margin on both sides — the label goes in that left margin, vertically
    // centered against the row, rather than above it (there isn't enough
    // vertical gap above the row to fit a label without it overlapping the
    // weapon row).
    if (tb) {
        irr::core::rect<irr::s32> firstGeneral =
            InventoryLayout::slotRect(layout, InventoryPanelState::EQUIP_SLOTS);
        tb->addText("General", popup.UpperLeftCorner.X + 8,
            firstGeneral.UpperLeftCorner.Y + SLOT / 2 - 6,
            irr::video::SColor(255, 180, 180, 180));
    }

    for (int i = 0; i < InventoryPanelState::GENERAL_SLOTS; ++i) {
        int globalIdx = InventoryPanelState::EQUIP_SLOTS + i;
        irr::core::rect<irr::s32> slotRect = InventoryLayout::slotRect(layout, globalIdx);

        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        uint8_t borderSprite = (inv.hoveredSlot == globalIdx)
            ? static_cast<uint8_t>(UISprite::SlotBorderHover)
            : static_cast<uint8_t>(UISprite::SlotBorderNormal);
        ui.drawSprite(slotRect, borderSprite);

        const auto& slot = inv.generalSlots[i];
        // T430 (board B376): same rule as the equipment slots above -- the
        // icon when there is one, the abbreviation when there is not. The
        // stack count is drawn either way and, being submitted after the
        // icon, lands on top of it (UIRenderer replays in submission order).
        const bool drewGeneralIcon =
            slot.hasItem && ui.drawItemIcon(slotRect, slot.iconId);
        if (slot.hasItem && tb) {
            if (!drewGeneralIcon) {
                std::string abbrev = slot.itemName.substr(0, 3);
                tb->addText(abbrev, slotRect.UpperLeftCorner.X + 2,
                    slotRect.UpperLeftCorner.Y + SLOT / 2 - 6,
                    irr::video::SColor(255, 200, 200, 200));
            }
            // Stack count
            if (slot.quantity > 1) {
                std::string qty = fmt::format("{}", slot.quantity);
                tb->addText(qty, slotRect.LowerRightCorner.X - 12,
                    slotRect.LowerRightCorner.Y - 12,
                    irr::video::SColor(255, 255, 255, 0));
            }
        }
    }

    // The purse (T393, board B201). Four rows, platinum/gold/silver/copper
    // top to bottom, in the oracle's own element order and at the oracle's
    // own rect scaled into centerPopup -- all of that geometry lives in
    // InventoryLayout::coinRect()/coinDecalRect(), so this body contains no
    // coin-position arithmetic, the same rule the slots follow.
    //
    // Drawn AFTER the paperdoll blit deliberately: fable's paperdoll rect
    // spans the whole gap between the two equipment columns (the oracle's
    // IW_CharacterView is only 85 of its 428 px wide), so the oracle-scaled
    // coin column lands over the doll's right-hand margin and must sit on
    // top of it. UIRenderer submission order is draw order (T294).
    for (int denom = 0; denom < InventoryLayout::COIN_COUNT; ++denom) {
        const auto row = InventoryLayout::coinRect(layout, denom);
        ui.drawSprite(row, static_cast<uint8_t>(UISprite::SlotBackground));
        // T416 (board B316): NOT drawSprite(SlotBorderNormal). That sprite is
        // a 1px ring inside a 32x32 tile and a coin row is 18px tall, so the
        // tile is MINIFIED and the sampler takes mip level 1, where the ring
        // has averaged into its own transparent interior. Until T411 the atlas
        // drew opaque and the washed-out ring sat on an opaque black tile, so
        // the row still read as a box; with the alpha live the frame collapses
        // to a 17/255 step against the row's interior (measured, T416). The
        // oracle's IW_Money buttons are framed plates
        // (review/T401/oracle/T401_oracle_purse_coinrows_crop4x.png), so the
        // frame is drawn from the uniform 4x4 `white` sprite instead -- every
        // mip of which is still white -- in the same grey the atlas ring
        // carries (slot_border_normal is {80,80,80,255} in ui_atlas_builder).
        ui.drawFrame(row, irr::video::SColor(255, 80, 80, 80));
        // The oracle blits A_PlatinumCoin/A_GoldCoin/A_SilverCoin/
        // A_CopperCoin into the decal box. T398 put those exact pixels in
        // fable's atlas (ui_atlas_builder --uifiles-path lifts the 72x18
        // strip at (90,140) of window_pieces01.tga), so the decal is now the
        // oracle's own coin, not T393's placeholder letter. The UISprite
        // coin entries are in denomination order, hence the plain offset.
        const auto decal = InventoryLayout::coinDecalRect(layout, denom);
        ui.drawSprite(decal, static_cast<uint8_t>(
            static_cast<int>(UISprite::CoinPlatinum) + denom));
        if (!tb) continue;
        tb->addText(coinRowText(inv, denom),
            decal.LowerRightCorner.X + 2, decal.UpperLeftCorner.Y,
            irr::video::SColor(255, 255, 255, 255));
    }

    // B442b (#152, merchant.tsv row 3): the item selected for sale while a
    // merchant is open -- the oracle frames it in yellow.
    if (inv.selectedSlot >= 0 && inv.selectedSlot < InventoryLayout::TOTAL_SLOTS) {
        ui.drawFrame(InventoryLayout::slotRect(layout, inv.selectedSlot),
                     irr::video::SColor(255, 255, 220, 0));
    }

    // B442b (#151 row 34): IW_Destroy. See InventoryLayout::destroyButtonRect()
    // for why its size is the oracle's and its place is not.
    const auto destroy = InventoryLayout::destroyButtonRect(layout);
    ui.drawRect(destroy, inv.destroyHovered ? irr::video::SColor(220, 120, 60, 60)
                                            : irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(destroy, irr::video::SColor(255, 80, 80, 80));
    if (tb) {
        tb->addTextCentered("Destroy", destroy, irr::video::SColor(255, 255, 255, 255));
    }
}

int resolveMemorizeGem(const SpellGemPanelState& state) {
    if (state.memorizeTargetGem >= 0 &&
        state.memorizeTargetGem < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT)) {
        return state.memorizeTargetGem;
    }
    for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) {
        // SpellGemState documents both 0 and SPELL_UNKNOWN as "empty".
        if (state.gems[i].spellId == 0 || state.gems[i].spellId == 0xFFFFFFFFu) {
            return i;
        }
    }
    return -1;
}

void renderSpellGemPanel(UIRenderer& ui, const UILayout& layout,
                         const SpellGemPanelState& state) {
    auto* tb = ui.getTextBatch();
    constexpr irr::s32 SLOT = UILayout::SLOT_SIZE;
    constexpr irr::s32 PAD = UILayout::MARGIN;

    // Panel background
    irr::core::rect<irr::s32> bg(
        layout.spellGems.UpperLeftCorner.X - 2,
        layout.spellGems.UpperLeftCorner.Y - 2,
        layout.spellGems.LowerRightCorner.X + 2,
        layout.spellGems.LowerRightCorner.Y + 2);
    ui.drawSprite(bg, static_cast<uint8_t>(UISprite::PanelBackground));

    // Gem state colors
    auto gemColor = [](uint8_t gemState) -> irr::video::SColor {
        switch (gemState) {
            case 1: return {255, 100, 200, 100};  // Ready — green tint
            case 2: return {255, 100, 100, 255};  // Casting — blue tint
            case 3: return {255, 200, 100, 100};  // Refresh — red tint
            case 4: return {255, 200, 200, 100};  // Memorizing — yellow tint
            default: return {255, 80, 80, 80};    // Empty — gray
        }
    };

    static const char* gemLabels[EQT::VISIBLE_SPELL_GEM_COUNT] = {"A1","A2","A3","A4","A5","A6","A7","A8"};

    for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) {
        irr::s32 x = layout.spellGems.UpperLeftCorner.X;
        irr::s32 y = layout.spellGems.UpperLeftCorner.Y + i * (SLOT + PAD);
        irr::core::rect<irr::s32> slotRect(x, y, x + SLOT, y + SLOT);

        // Slot background with gem state tint
        const auto& gem = state.gems[i];
        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        // State-colored border
        if (gem.spellId != 0 && gem.gemState > 0) {
            ui.drawFrame(slotRect, gemColor(gem.gemState));
        } else {
            uint8_t borderSprite = (state.hoveredGem == i)
                ? static_cast<uint8_t>(UISprite::SlotBorderHover)
                : static_cast<uint8_t>(UISprite::SlotBorderNormal);
            ui.drawSprite(slotRect, borderSprite);
        }

        // T272: memorize destination marker. Drawn on top of whatever border
        // the gem already has, so a full gem picked as the overwrite target
        // is just as visible as an empty one. Default state
        // (memorizeTargetGem == -1) draws nothing.
        if (state.memorizeTargetGem == i) {
            ui.drawFrame(slotRect, irr::video::SColor(255, 255, 215, 0), 2);  // gold
        }

        // Cooldown overlay for refresh state
        if (gem.gemState == 3 && gem.cooldownTotalMs > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - gem.lastUpdateTime).count();
            uint32_t remaining = (elapsed < gem.cooldownRemainingMs)
                ? gem.cooldownRemainingMs - static_cast<uint32_t>(elapsed) : 0;
            float pct = 1.0f - static_cast<float>(remaining) / static_cast<float>(gem.cooldownTotalMs);
            float darkPct = 1.0f - pct;
            uint8_t alpha = static_cast<uint8_t>(darkPct * 160);
            ui.drawRect(slotRect, irr::video::SColor(alpha, 0, 0, 0));
        }

        // Spell name (abbreviated)
        if (gem.spellId != 0 && tb) {
            std::string abbrev = gem.spellName.substr(0, 4);
            tb->addText(abbrev, x + 2, y + SLOT / 2 - 6,
                irr::video::SColor(255, 220, 220, 220));
        }

        // Alt+N key label
        if (tb) {
            tb->addText(gemLabels[i], x + SLOT - 14, y + SLOT - 12,
                irr::video::SColor(150, 180, 180, 100));
        }
    }
}

// T277: the buff bar's geometry, shared by the renderer and the hit-test so
// a click can never land on a different slot than the one drawn under it.
namespace {
constexpr irr::s32 kBuffIcon = 20;  // Small buff icons
constexpr irr::s32 kBuffPad = 2;

// True when the bar draws at all -- an empty bar has no background and no
// icons, so nothing there is clickable.
bool buffBarHasContent(const BuffBarState& state) {
    for (int i = 0; i < BuffBarState::MAX_BUFFS; ++i) {
        if (state.buffs[i].spellId != 0) return true;
    }
    return false;
}
}  // namespace

// T368 (B101): the Alt+B toggle ANDed with the content gate. One body for
// the renderer and the hit-test, so a closed bar is unclickable for the same
// reason it is invisible.
bool buffBarIsVisible(const BuffBarState& state) {
    return state.isOpen && buffBarHasContent(state);
}

int hitTestBuffBar(const UILayout& layout, const BuffBarState& state,
                   int mouseX, int mouseY) {
    if (!buffBarIsVisible(state)) return -1;

    irr::s32 x = layout.buffBar.UpperLeftCorner.X + kBuffPad;
    irr::s32 y = layout.buffBar.UpperLeftCorner.Y + kBuffPad;

    for (int i = 0; i < BuffBarState::MAX_BUFFS; ++i) {
        if (state.buffs[i].spellId == 0) continue;
        if (x + kBuffIcon > layout.buffBar.LowerRightCorner.X - kBuffPad) break;

        if (mouseX >= x && mouseX < x + kBuffIcon &&
            mouseY >= y && mouseY < y + kBuffIcon) {
            return i;
        }
        x += kBuffIcon + kBuffPad;
    }
    return -1;
}

void renderBuffBar(UIRenderer& ui, const UILayout& layout,
                   const BuffBarState& state) {
    auto* tb = ui.getTextBatch();
    constexpr irr::s32 ICON = kBuffIcon;
    constexpr irr::s32 PAD = kBuffPad;

    if (!buffBarIsVisible(state)) return;

    // Background
    ui.drawSprite(layout.buffBar, static_cast<uint8_t>(UISprite::PanelBackground));

    irr::s32 x = layout.buffBar.UpperLeftCorner.X + PAD;
    irr::s32 y = layout.buffBar.UpperLeftCorner.Y + PAD;
    auto now = std::chrono::steady_clock::now();

    for (int i = 0; i < BuffBarState::MAX_BUFFS; ++i) {
        const auto& buff = state.buffs[i];
        if (buff.spellId == 0) continue;

        // Check if we've run past the bar width
        if (x + ICON > layout.buffBar.LowerRightCorner.X - PAD) break;

        irr::core::rect<irr::s32> iconRect(x, y, x + ICON, y + ICON);

        // Colored background based on beneficial/detrimental (simplified: always blue-ish)
        ui.drawRect(iconRect, irr::video::SColor(200, 30, 50, 120));
        ui.drawFrame(iconRect, irr::video::SColor(255, 80, 100, 180));

        // Abbreviated spell name
        if (tb) {
            std::string abbrev = buff.spellName.substr(0, 2);
            tb->addText(abbrev, x + 2, y + 2,
                irr::video::SColor(255, 220, 220, 255));
        }

        // Duration text below icon
        if (tb && buff.ticksLeft > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - buff.updateTime).count();
            int totalSec = static_cast<int>(buff.ticksLeft * 6) - static_cast<int>(elapsed);
            if (totalSec < 0) totalSec = 0;
            int minutes = totalSec / 60;
            std::string dur = (minutes > 0) ? fmt::format("{}m", minutes) : fmt::format("{}s", totalSec);
            tb->addText(dur, x, y + ICON + 1,
                irr::video::SColor(200, 180, 180, 180));
        }

        x += ICON + PAD;
    }
}

// T413 (board B296): CastingStateChangedData::castTimeRemainingMs and
// ::castTimeTotalMs were filled by SpellState on every casting transition and
// read by nobody -- the bridge's CastingStateChanged arm was a LOG_TRACE
// whose comment still pointed at the deleted WindowManager. They now drive
// the same gauge SpellCastVisualStarted opens.
void applyCastingStateChanged(CastingBarState& state,
                              const eqt::state::CastingStateChangedData& data) {
    if (!data.isCasting) {
        state.isCasting = false;
        state.remainingMs = 0;
        state.totalMs = 0;
        return;
    }
    state.isCasting = true;
    state.remainingMs = data.castTimeRemainingMs;
    state.totalMs = data.castTimeTotalMs;
    // The name comes from SpellCastVisualStarted, which carries it; this
    // event does not, so an existing name is never cleared here.
    if (state.castTimeMs == 0) state.castTimeMs = data.castTimeTotalMs;
}

void applySpellMemorizing(SpellbookPopupState& state,
                          const eqt::state::SpellMemorizingData& data) {
    if (!data.isMemorizing) {
        state.isMemorizing = false;
        state.memorizeProgressMs = 0;
        state.memorizeTotalMs = 0;
        return;
    }
    state.isMemorizing = true;
    state.memorizeGemSlot = data.gemSlot;
    state.memorizeProgressMs = data.progressMs;
    state.memorizeTotalMs = data.totalMs;
}

irr::core::rect<irr::s32> spellbookMemorizeGaugeRect(const UILayout& layout) {
    // EQUI_SpellBookWnd.xml, quoted in the header: gauge (204,312) 100x8
    // inside a 510x360 screen.
    constexpr int kOracleX = 204, kOracleY = 312;
    constexpr int kOracleW = 100, kOracleH = 8;
    constexpr int kOracleWinW = 510, kOracleWinH = 360;

    const auto& popup = layout.centerPopup;
    const irr::s32 x = popup.UpperLeftCorner.X + kOracleX * popup.getWidth() / kOracleWinW;
    const irr::s32 y = popup.UpperLeftCorner.Y + kOracleY * popup.getHeight() / kOracleWinH;
    const irr::s32 w = kOracleW * popup.getWidth() / kOracleWinW;
    const irr::s32 h = kOracleH * popup.getHeight() / kOracleWinH;
    return {x, y, x + w, y + h};
}

void renderCastingBar(UIRenderer& ui, const UILayout& layout,
                      const CastingBarState& state) {
    if (!state.isCasting) return;

    auto* tb = ui.getTextBatch();
    float progress = state.getProgress();

    // Background
    ui.drawSprite(layout.castingBar, static_cast<uint8_t>(UISprite::BarBackground));

    // Fill
    if (progress > 0.0f) {
        irr::s32 fillW = static_cast<irr::s32>(layout.castingBar.getWidth() * progress);
        irr::core::rect<irr::s32> fillRect(
            layout.castingBar.UpperLeftCorner.X,
            layout.castingBar.UpperLeftCorner.Y,
            layout.castingBar.UpperLeftCorner.X + fillW,
            layout.castingBar.LowerRightCorner.Y);
        ui.drawSprite(fillRect, static_cast<uint8_t>(UISprite::BarCasting));
    }

    // Border
    ui.drawFrame(layout.castingBar, irr::video::SColor(255, 80, 140, 200));

    // Spell name centered
    if (tb) {
        tb->addTextCentered(state.spellName, layout.castingBar,
            irr::video::SColor(255, 255, 255, 255));
    }
}

// T368 (B101): the Alt+P toggle is the whole gate -- see the header. (T382
// re-measured the key itself against the oracle; B170.)
bool groupPanelIsVisible(const GroupPanelState& state) {
    return state.isOpen;
}

// T382 (B170): the rect renderGroupPanel actually paints.
//
// MEASURED at the oracle, T377: with NO group the official client paints ONLY
// its title bar -- a single strip reading "Group", no member rows, no empty
// body, no reserved height (visible in every review/T377/oracle/*.png; the
// clearest is T377_oracle_group_window_nogroup_open_1024x768.png). T368 gave
// fable the full titled FRAME instead, five member rows tall and empty.
//
// The collapsed height is the title band the panel's own layout already
// reserves -- BAR_HEIGHT + MARGIN is exactly where renderGroupPanel puts the
// first member row -- so the strip and the full panel share one origin and
// one width, and a group joining simply grows the frame downward.
//
// Split out as a free function because it is the static-panel test seam: the
// shape is assertable with no Irrlicht device.
irr::core::rect<irr::s32> groupPanelFrameRect(const UILayout& layout,
                                              const GroupPanelState& state) {
    irr::core::rect<irr::s32> frame = layout.groupPanel;
    if (!state.inGroup) {
        frame.LowerRightCorner.Y = frame.UpperLeftCorner.Y
            + UILayout::BAR_HEIGHT + UILayout::MARGIN;
    }
    return frame;
}

void renderGroupPanel(UIRenderer& ui, const UILayout& layout,
                      const GroupPanelState& state) {
    if (!groupPanelIsVisible(state)) return;

    auto* tb = ui.getTextBatch();
    constexpr irr::s32 BAR_H = UILayout::BAR_HEIGHT;
    constexpr irr::s32 PAD = UILayout::MARGIN;

    ui.drawPanel(groupPanelFrameRect(layout, state));

    if (tb) {
        tb->addText("Group", layout.groupPanel.UpperLeftCorner.X + 4,
            layout.groupPanel.UpperLeftCorner.Y + 2,
            irr::video::SColor(255, 255, 215, 0));
    }

    irr::s32 x = layout.groupPanel.UpperLeftCorner.X + 4;
    irr::s32 y = layout.groupPanel.UpperLeftCorner.Y + BAR_H + PAD;
    irr::s32 barW = layout.groupPanel.getWidth() - 8;

    // T382 (B170): with no group the TITLE BAR and its title are all that
    // draws -- the frame above was already collapsed to that strip. The member
    // array is not consulted at all: a disband leaves stale rows in it until
    // the next GroupUpdate, and an open window must not show them.
    if (!state.inGroup) return;

    for (int i = 0; i < GroupPanelState::MAX_MEMBERS; ++i) {
        const auto& m = state.members[i];
        if (m.name.empty()) continue;

        // Name
        if (tb) {
            irr::video::SColor nameCol = m.inZone
                ? irr::video::SColor(255, 200, 200, 200)
                : irr::video::SColor(255, 120, 120, 120);
            tb->addText(m.name, x, y, nameCol);
        }
        y += BAR_H;

        // HP bar
        irr::core::rect<irr::s32> hpRect(x, y, x + barW, y + BAR_H - 2);
        float hpPct = m.hpPercent / 100.0f;
        ui.drawBar(hpRect, hpPct,
            irr::video::SColor(255, 40, 180, 40),
            irr::video::SColor(200, 15, 15, 20));

        if (tb) {
            std::string hpText = percentText(m.hpPercent);
            tb->addTextCentered(hpText, hpRect,
                irr::video::SColor(255, 255, 255, 255));
        }

        y += BAR_H + PAD;
    }
}

// T368 (B101): the P toggle ANDed with the auto-driven hasPet gate.
bool petPanelIsVisible(const PetPanelState& state) {
    return state.isOpen && state.hasPet;
}

void renderPetPanel(UIRenderer& ui, const UILayout& layout,
                    const PetPanelState& state) {
    if (!petPanelIsVisible(state)) return;

    auto* tb = ui.getTextBatch();
    constexpr irr::s32 BAR_H = UILayout::BAR_HEIGHT;

    ui.drawPanel(layout.petPanel);

    irr::s32 x = layout.petPanel.UpperLeftCorner.X + 4;
    irr::s32 y = layout.petPanel.UpperLeftCorner.Y + 2;
    irr::s32 barW = layout.petPanel.getWidth() - 8;

    // Pet name + level
    if (tb) {
        std::string label = fmt::format("{} ({})", state.name, state.level);
        tb->addText(label, x, y, irr::video::SColor(255, 200, 255, 200));
    }
    y += BAR_H;

    // HP bar
    irr::core::rect<irr::s32> hpRect(x, y, x + barW, y + BAR_H - 2);
    float hpPct = state.hpPercent / 100.0f;
    ui.drawBar(hpRect, hpPct,
        irr::video::SColor(255, 40, 180, 40),
        irr::video::SColor(200, 15, 15, 20));

    if (tb) {
        std::string hpText = percentText(state.hpPercent);
        tb->addTextCentered(hpText, hpRect, irr::video::SColor(255, 255, 255, 255));
    }

    // T419 (board B352): the command indicators are a GRID inside the panel,
    // not a run of text past its bottom edge. Every rect comes from
    // PetLayout::buttonRect() -- which quotes EQUI_PetInfoWindow.xml and
    // records the two deviations from it -- so the draw and the panel's own
    // height are computed from one set of constants and the tenth button
    // ("SH", SpellHold) can no longer be silently dropped for want of room.
    static const char* btnLabels[] = {"Sit","Stp","Reg","Fol","Grd",
                                      "Tnt","Hld","GH","Foc","SH"};
    static_assert(PetPanelState::BUTTON_COUNT == PetLayout::BUTTON_COUNT,
                  "the pet grid must hold every PetButton");
    for (int i = 0; i < PetPanelState::BUTTON_COUNT; ++i) {
        const auto r = PetLayout::buttonRect(layout, i);
        ui.drawRect(r, state.buttonStates[i]
                           ? irr::video::SColor(160, 30, 70, 30)
                           : irr::video::SColor(120, 28, 28, 34));
        if (tb) {
            tb->addTextCentered(btnLabels[i], r,
                state.buttonStates[i]
                    ? irr::video::SColor(255, 100, 255, 100)   // Active — green
                    : irr::video::SColor(255, 120, 120, 120)); // Inactive — gray
        }
    }
}

void renderSpellbookPopup(UIRenderer& ui, const UILayout& layout,
                          const SpellbookPopupState& state) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    const auto& popup = layout.centerPopup;

    ui.drawPanel(popup);

    // Title + page indicator
    if (tb) {
        std::string title = fmt::format("Spellbook - Page {}/{}", state.currentPage + 1, state.pageCount());
        tb->addText(title, popup.UpperLeftCorner.X + 8, popup.UpperLeftCorner.Y + 4,
            irr::video::SColor(255, 255, 215, 0));
    }

    constexpr irr::s32 LINE_H = 28;
    constexpr irr::s32 PAD = UILayout::MARGIN;
    irr::s32 startX = popup.UpperLeftCorner.X + 8;
    irr::s32 startY = popup.UpperLeftCorner.Y + 24;
    irr::s32 slotW = popup.getWidth() - 16;

    // Draw spells for current page
    int firstIdx = state.currentPage * SpellbookPopupState::SPELLS_PER_PAGE;
    int lastIdx = std::min(firstIdx + SpellbookPopupState::SPELLS_PER_PAGE,
                           static_cast<int>(state.spells.size()));

    for (int i = firstIdx; i < lastIdx; ++i) {
        int row = i - firstIdx;
        irr::s32 y = startY + row * (LINE_H + PAD);
        irr::core::rect<irr::s32> slotRect(startX, y, startX + slotW, y + LINE_H);

        // Slot background
        bool hovered = (state.hoveredSlot == row);
        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));
        if (hovered) {
            ui.drawRect({startX, y, startX + slotW, y + 1}, irr::video::SColor(255, 255, 215, 0));
            ui.drawRect({startX, y + LINE_H - 1, startX + slotW, y + LINE_H}, irr::video::SColor(255, 255, 215, 0));
        }

        const auto& spell = state.spells[i];

        if (tb) {
            // Level
            std::string lvlStr = fmt::format("L{}", spell.level);
            tb->addText(lvlStr, startX + 4, y + 6,
                irr::video::SColor(255, 150, 150, 150));

            // Spell name
            tb->addText(spell.name, startX + 30, y + 6,
                irr::video::SColor(255, 220, 220, 255));
        }
    }

    // T413 (board B296): the memorisation gauge, at the oracle's own place --
    // EQUI_SpellBookWnd.xml's <Gauge item="SBW_Memorize_Gauge"> (EQType 9),
    // FillTint (200,0,200). Drawn only while a memorise is in flight, which
    // is exactly when the oracle's gauge is non-empty.
    if (state.isMemorizing) {
        const auto gauge = spellbookMemorizeGaugeRect(layout);
        ui.drawBar(gauge, state.memorizeProgress(),
                   irr::video::SColor(255, 200, 0, 200),
                   irr::video::SColor(180, 20, 20, 20));
    }

    // Page navigation hints
    if (tb) {
        irr::s32 navY = popup.LowerRightCorner.Y - 20;
        if (state.currentPage > 0) {
            tb->addText("< Prev", startX, navY,
                irr::video::SColor(255, 180, 180, 255));
        }
        if (state.currentPage < state.pageCount() - 1) {
            std::string next = "Next >";
            irr::s32 nw = tb->getTextWidth(next);
            tb->addText(next, popup.LowerRightCorner.X - 8 - nw, navY,
                irr::video::SColor(255, 180, 180, 255));
        }
    }
}

int skillsVisibleRows(const UILayout& layout) {
    // The popup's content box: below the title row, above the bottom
    // scroll-position line -- the same `getHeight() - 40` the row loop used
    // before this helper existed.
    int contentH = layout.centerPopup.getHeight() - 40;
    int rows = contentH / (SkillsPopupState::LINE_H + SkillsPopupState::ROW_PAD);
    return rows > 0 ? rows : 1;
}

void renderSkillsPopup(UIRenderer& ui, const UILayout& layout,
                        const SkillsPopupState& state) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    const auto& popup = layout.centerPopup;

    ui.drawPanel(popup);

    if (tb) {
        tb->addText("Skills", popup.UpperLeftCorner.X + 8, popup.UpperLeftCorner.Y + 4,
            irr::video::SColor(255, 255, 215, 0));
    }

    // T313: LINE_H/PAD were file-local constexprs here and duplicated
    // verbatim in StaticUIInput's row hit test; they now live on
    // SkillsPopupState, and the row count comes from the shared
    // skillsVisibleRows() so the drawn window, the hit-testable window and
    // the scroll clamp cannot disagree.
    constexpr irr::s32 LINE_H = SkillsPopupState::LINE_H;
    constexpr irr::s32 PAD = SkillsPopupState::ROW_PAD;
    irr::s32 startX = popup.UpperLeftCorner.X + 8;
    irr::s32 startY = popup.UpperLeftCorner.Y + 24;
    irr::s32 contentW = popup.getWidth() - 16;
    int visibleLines = skillsVisibleRows(layout);

    int totalSkills = static_cast<int>(state.skills.size());
    int startIdx = state.scrollOffset;
    int endIdx = std::min(startIdx + visibleLines, totalSkills);

    for (int i = startIdx; i < endIdx; ++i) {
        int row = i - startIdx;
        irr::s32 y = startY + row * (LINE_H + PAD);

        const auto& skill = state.skills[i];

        // Highlight hovered row
        if (state.hoveredRow == row) {
            irr::core::rect<irr::s32> rowRect(startX, y, startX + contentW, y + LINE_H);
            ui.drawRect(rowRect, irr::video::SColor(60, 255, 255, 255));
        }

        if (tb) {
            // Skill name (left-aligned)
            tb->addText(skill.name, startX, y + 2,
                irr::video::SColor(255, 200, 200, 200));

            // Value / Max (right-aligned)
            std::string valStr = fmt::format("{}/{}", skill.value, skill.maxValue);
            irr::s32 valW = tb->getTextWidth(valStr);
            tb->addText(valStr, startX + contentW - valW, y + 2,
                irr::video::SColor(255, 180, 220, 180));
        }
    }

    // Scroll indicator
    if (totalSkills > visibleLines && tb) {
        irr::s32 navY = popup.LowerRightCorner.Y - 18;
        std::string scrollInfo = fmt::format("{}-{} of {}", startIdx + 1, endIdx, totalSkills);
        tb->addTextCentered(scrollInfo, {startX, navY, startX + contentW, navY + 14},
            irr::video::SColor(200, 150, 150, 150));
    }
}

void renderXPBar(UIRenderer& ui, const UILayout& layout,
                 const XPBarState& state) {
    // Background
    ui.drawSprite(layout.xpBar, static_cast<uint8_t>(UISprite::BarBackground));

    // Fill
    if (state.progress > 0.0f) {
        irr::s32 fillW = static_cast<irr::s32>(layout.xpBar.getWidth() * state.progress);
        irr::core::rect<irr::s32> fillRect(
            layout.xpBar.UpperLeftCorner.X,
            layout.xpBar.UpperLeftCorner.Y,
            layout.xpBar.UpperLeftCorner.X + fillW,
            layout.xpBar.LowerRightCorner.Y);
        ui.drawSprite(fillRect, static_cast<uint8_t>(UISprite::BarXP));
    }
}


// T156: the single HotbarSlotAssigned -> panel-state hop. See the declaration
// in static_panels.h for why it is a free function.
void applyHotbarSlotAssigned(HotbarPanelState& state,
                             const eqt::state::HotbarSlotAssignedData& data) {
    if (data.index < 0 || data.index >= HotbarPanelState::SLOT_COUNT) return;
    if (data.page < 0 || data.page >= eqt::HotbarModel::PAGE_COUNT ||
        data.currentPage < 0 || data.currentPage >= eqt::HotbarModel::PAGE_COUNT) return;
    if (state.currentPage != data.currentPage) {
        for (auto& slot : state.slots) {
            slot.cooldownDurationMs = 0;
            slot.cooldownEndTime = {};
        }
    }
    state.currentPage = data.currentPage;
    if (data.page != data.currentPage) return;
    auto& slot = state.slots[data.index];
    if (slot.type != data.type || slot.id != data.id || slot.emoteText != data.emoteText) {
        slot.cooldownDurationMs = 0;
        slot.cooldownEndTime = {};
    }
    slot.type = data.type;
    slot.id = data.id;
    slot.emoteText = data.emoteText;
    slot.name = data.name;
    slot.iconId = data.iconId;
}

// ---------------------------------------------------------------------------
// T393 (board B201): the purse
// ---------------------------------------------------------------------------

void applyCurrencyChanged(InventoryPanelState& state,
                          const eqt::state::CurrencyChangedData& data) {
    // Absolute counts. Every one of the five publishers (OP_MoneyUpdate in
    // core/everquest.cpp, the corpse-coin grant in commands/combat_actions.cpp,
    // the merchant in merchant/merchant_trade.cpp, and the two stat pushes in
    // bridge/) reads the whole purse off PlayerState and sends it, so this is
    // a set, never an add.
    state.coins[InventoryLayout::COIN_PLATINUM] = data.platinum;
    state.coins[InventoryLayout::COIN_GOLD]     = data.gold;
    state.coins[InventoryLayout::COIN_SILVER]   = data.silver;
    state.coins[InventoryLayout::COIN_COPPER]   = data.copper;
}

std::string coinRowText(const InventoryPanelState& state, int denom) {
    if (denom < 0 || denom >= InventoryLayout::COIN_COUNT) return std::string();
    return fmt::format("{}", state.coins[denom]);
}

} // namespace Graphics
} // namespace EQT
