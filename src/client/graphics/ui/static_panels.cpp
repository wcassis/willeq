/*
 * Static UI panel render functions.
 * U03d: Player status + target info as proof of concept.
 */

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/chat_message_buffer.h"
#include "client/graphics/text_batch.h"
#include <fmt/format.h>

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
        std::string hpText = fmt::format("{}%%", target.hpPercent);
        tb->addTextCentered(hpText, layout.targetHPBar,
            irr::video::SColor(255, 255, 255, 255));
    }
}

void renderChatPanel(UIRenderer& ui, const UILayout& layout,
                     ChatPanelState& state) {
    auto* tb = ui.getTextBatch();
    if (!tb) return;

    // Panel background
    ui.drawPanel(layout.chatPanel);

    // Process any pending messages from network thread
    if (state.messageBuffer) {
        state.messageBuffer->processPending();
    }

    // Render messages
    if (state.messageBuffer && state.messageBuffer->size() > 0) {
        const auto& messages = state.messageBuffer->getMessages();
        irr::s32 lineH = tb->getLineHeight();
        if (lineH <= 0) lineH = 14;

        irr::s32 areaX = layout.chatMessages.UpperLeftCorner.X + 2;
        irr::s32 areaTop = layout.chatMessages.UpperLeftCorner.Y + 1;
        irr::s32 areaBottom = layout.chatMessages.LowerRightCorner.Y - 1;
        irr::s32 areaW = layout.chatMessages.getWidth() - 4;
        (void)areaW;  // Will use for word wrapping in future

        // Calculate how many lines fit
        int visibleLines = (areaBottom - areaTop) / lineH;
        if (visibleLines <= 0) return;

        // Draw from bottom up, newest messages at bottom
        int totalMessages = static_cast<int>(messages.size());
        int startIdx = totalMessages - visibleLines - state.scrollOffset;
        if (startIdx < 0) startIdx = 0;
        int endIdx = startIdx + visibleLines;
        if (endIdx > totalMessages) endIdx = totalMessages;

        irr::s32 y = areaTop;
        for (int i = startIdx; i < endIdx; ++i) {
            const auto& msg = messages[i];
            std::string display = eqt::ui::formatMessageForDisplay(msg, state.showTimestamps);
            tb->addText(display, areaX, y, msg.color);
            y += lineH;
        }
    }

    // Input field background
    ui.drawRect(layout.chatInput, irr::video::SColor(200, 5, 5, 10));

    // Input field border
    irr::video::SColor inputBorder(255, 80, 80, 90);
    irr::s32 ix1 = layout.chatInput.UpperLeftCorner.X;
    irr::s32 iy1 = layout.chatInput.UpperLeftCorner.Y;
    irr::s32 ix2 = layout.chatInput.LowerRightCorner.X;
    irr::s32 iy2 = layout.chatInput.LowerRightCorner.Y;
    ui.drawRect({ix1, iy1, ix2, iy1 + 1}, inputBorder);

    // Input text
    if (!state.inputText.empty()) {
        tb->addText(state.inputText,
            layout.chatInput.UpperLeftCorner.X + 2,
            layout.chatInput.UpperLeftCorner.Y + 2,
            irr::video::SColor(255, 255, 255, 255));
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
    constexpr irr::s32 PAD = 2;
    irr::s32 startX = popup.UpperLeftCorner.X + 8;
    irr::s32 startY = popup.UpperLeftCorner.Y + 22;

    // Equipment slots: 2 columns of 11 rows on the left side
    for (int i = 0; i < InventoryPanelState::EQUIP_SLOTS; ++i) {
        int col = i / 11;
        int row = i % 11;
        irr::s32 x = startX + col * (SLOT + PAD);
        irr::s32 y = startY + row * (SLOT + PAD);
        irr::core::rect<irr::s32> slotRect(x, y, x + SLOT, y + SLOT);

        // Slot background
        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        // Border (hover highlight or normal)
        uint8_t borderSprite = (inv.hoveredSlot == i)
            ? static_cast<uint8_t>(UISprite::SlotBorderHover)
            : static_cast<uint8_t>(UISprite::SlotBorderNormal);
        ui.drawSprite(slotRect, borderSprite);

        // Item name (abbreviated) — placeholder until icon atlas
        const auto& slot = inv.equipSlots[i];
        if (slot.hasItem && tb) {
            // Show first 3 chars of item name
            std::string abbrev = slot.itemName.substr(0, 3);
            tb->addText(abbrev, x + 2, y + SLOT / 2 - 6,
                irr::video::SColor(255, 200, 200, 200));
        }
    }

    // General inventory: row of 8 slots below equipment
    irr::s32 genY = startY + 11 * (SLOT + PAD) + 8;
    if (tb) {
        tb->addText("General", startX, genY - 14,
            irr::video::SColor(255, 180, 180, 180));
    }

    for (int i = 0; i < InventoryPanelState::GENERAL_SLOTS; ++i) {
        irr::s32 x = startX + i * (SLOT + PAD);
        irr::s32 y = genY;
        irr::core::rect<irr::s32> slotRect(x, y, x + SLOT, y + SLOT);

        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        int globalIdx = InventoryPanelState::EQUIP_SLOTS + i;
        uint8_t borderSprite = (inv.hoveredSlot == globalIdx)
            ? static_cast<uint8_t>(UISprite::SlotBorderHover)
            : static_cast<uint8_t>(UISprite::SlotBorderNormal);
        ui.drawSprite(slotRect, borderSprite);

        const auto& slot = inv.generalSlots[i];
        if (slot.hasItem && tb) {
            std::string abbrev = slot.itemName.substr(0, 3);
            tb->addText(abbrev, x + 2, y + SLOT / 2 - 6,
                irr::video::SColor(255, 200, 200, 200));
            // Stack count
            if (slot.quantity > 1) {
                std::string qty = fmt::format("{}", slot.quantity);
                tb->addText(qty, x + SLOT - 12, y + SLOT - 12,
                    irr::video::SColor(255, 255, 255, 0));
            }
        }
    }
}

void renderHotbar(UIRenderer& ui, const UILayout& layout,
                  const HotbarPanelState& state) {
    auto* tb = ui.getTextBatch();
    constexpr irr::s32 SLOT = UILayout::SLOT_SIZE;
    constexpr irr::s32 PAD = UILayout::MARGIN;

    // Panel background (slightly wider than slots for border)
    irr::core::rect<irr::s32> bg(
        layout.hotbar.UpperLeftCorner.X - 2,
        layout.hotbar.UpperLeftCorner.Y - 2,
        layout.hotbar.LowerRightCorner.X + 2,
        layout.hotbar.LowerRightCorner.Y + 2);
    ui.drawSprite(bg, static_cast<uint8_t>(UISprite::PanelBackground));

    // Key labels
    static const char* keyLabels[10] = {"1","2","3","4","5","6","7","8","9","0"};

    for (int i = 0; i < HotbarPanelState::SLOT_COUNT; ++i) {
        irr::s32 x = layout.hotbar.UpperLeftCorner.X + i * (SLOT + PAD);
        irr::s32 y = layout.hotbar.UpperLeftCorner.Y;
        irr::core::rect<irr::s32> slotRect(x, y, x + SLOT, y + SLOT);

        // Slot background
        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        // Border
        uint8_t borderSprite = (state.hoveredSlot == i)
            ? static_cast<uint8_t>(UISprite::SlotBorderHover)
            : static_cast<uint8_t>(UISprite::SlotBorderNormal);
        ui.drawSprite(slotRect, borderSprite);

        const auto& slot = state.slots[i];

        // Cooldown overlay (darken the slot)
        if (slot.isOnCooldown()) {
            float darkPct = 1.0f - slot.getCooldownProgress();
            uint8_t alpha = static_cast<uint8_t>(darkPct * 160);
            ui.drawRect(slotRect, irr::video::SColor(alpha, 0, 0, 0));
        }

        // Slot content: abbreviated name (placeholder for icon)
        if (slot.type != 0 && tb) {
            std::string abbrev = slot.name.substr(0, 4);
            irr::s32 tw = tb->getTextWidth(abbrev);
            tb->addText(abbrev, x + (SLOT - tw) / 2, y + SLOT / 2 - 6,
                irr::video::SColor(255, 220, 220, 220));
        }

        // Key label (bottom-right corner)
        if (tb) {
            tb->addText(keyLabels[i], x + SLOT - 8, y + SLOT - 12,
                irr::video::SColor(180, 200, 200, 100));
        }
    }
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

    static const char* gemLabels[8] = {"A1","A2","A3","A4","A5","A6","A7","A8"};

    for (int i = 0; i < SpellGemPanelState::GEM_COUNT; ++i) {
        irr::s32 x = layout.spellGems.UpperLeftCorner.X;
        irr::s32 y = layout.spellGems.UpperLeftCorner.Y + i * (SLOT + PAD);
        irr::core::rect<irr::s32> slotRect(x, y, x + SLOT, y + SLOT);

        // Slot background with gem state tint
        const auto& gem = state.gems[i];
        ui.drawSprite(slotRect, static_cast<uint8_t>(UISprite::SlotBackground));

        // State-colored border
        if (gem.spellId != 0 && gem.gemState > 0) {
            irr::video::SColor borderCol = gemColor(gem.gemState);
            ui.drawRect({x, y, x + SLOT, y + 1}, borderCol);
            ui.drawRect({x, y + SLOT - 1, x + SLOT, y + SLOT}, borderCol);
            ui.drawRect({x, y, x + 1, y + SLOT}, borderCol);
            ui.drawRect({x + SLOT - 1, y, x + SLOT, y + SLOT}, borderCol);
        } else {
            uint8_t borderSprite = (state.hoveredGem == i)
                ? static_cast<uint8_t>(UISprite::SlotBorderHover)
                : static_cast<uint8_t>(UISprite::SlotBorderNormal);
            ui.drawSprite(slotRect, borderSprite);
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

void renderBuffBar(UIRenderer& ui, const UILayout& layout,
                   const BuffBarState& state) {
    auto* tb = ui.getTextBatch();
    constexpr irr::s32 ICON = 20;  // Small buff icons
    constexpr irr::s32 PAD = 2;

    // Count active buffs
    int activeCount = 0;
    for (int i = 0; i < BuffBarState::MAX_BUFFS; ++i) {
        if (state.buffs[i].spellId != 0) activeCount++;
    }
    if (activeCount == 0) return;

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
        ui.drawRect({x, y, x + ICON, y + 1}, irr::video::SColor(255, 80, 100, 180));
        ui.drawRect({x, y + ICON - 1, x + ICON, y + ICON}, irr::video::SColor(255, 80, 100, 180));
        ui.drawRect({x, y, x + 1, y + ICON}, irr::video::SColor(255, 80, 100, 180));
        ui.drawRect({x + ICON - 1, y, x + ICON, y + ICON}, irr::video::SColor(255, 80, 100, 180));

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
    irr::video::SColor border(255, 80, 140, 200);
    irr::s32 x1 = layout.castingBar.UpperLeftCorner.X;
    irr::s32 y1 = layout.castingBar.UpperLeftCorner.Y;
    irr::s32 x2 = layout.castingBar.LowerRightCorner.X;
    irr::s32 y2 = layout.castingBar.LowerRightCorner.Y;
    ui.drawRect({x1, y1, x2, y1 + 1}, border);
    ui.drawRect({x1, y2 - 1, x2, y2}, border);
    ui.drawRect({x1, y1, x1 + 1, y2}, border);
    ui.drawRect({x2 - 1, y1, x2, y2}, border);

    // Spell name centered
    if (tb) {
        tb->addTextCentered(state.spellName, layout.castingBar,
            irr::video::SColor(255, 255, 255, 255));
    }
}

void renderGroupPanel(UIRenderer& ui, const UILayout& layout,
                      const GroupPanelState& state) {
    if (!state.inGroup) return;

    auto* tb = ui.getTextBatch();
    constexpr irr::s32 BAR_H = UILayout::BAR_HEIGHT;
    constexpr irr::s32 PAD = UILayout::MARGIN;

    ui.drawPanel(layout.groupPanel);

    if (tb) {
        tb->addText("Group", layout.groupPanel.UpperLeftCorner.X + 4,
            layout.groupPanel.UpperLeftCorner.Y + 2,
            irr::video::SColor(255, 255, 215, 0));
    }

    irr::s32 x = layout.groupPanel.UpperLeftCorner.X + 4;
    irr::s32 y = layout.groupPanel.UpperLeftCorner.Y + BAR_H + PAD;
    irr::s32 barW = layout.groupPanel.getWidth() - 8;

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
            std::string hpText = fmt::format("{}%%", m.hpPercent);
            tb->addTextCentered(hpText, hpRect,
                irr::video::SColor(255, 255, 255, 255));
        }

        y += BAR_H + PAD;
    }
}

void renderPetPanel(UIRenderer& ui, const UILayout& layout,
                    const PetPanelState& state) {
    if (!state.hasPet) return;

    auto* tb = ui.getTextBatch();
    constexpr irr::s32 BAR_H = UILayout::BAR_HEIGHT;
    constexpr irr::s32 PAD = UILayout::MARGIN;

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
        std::string hpText = fmt::format("{}%%", state.hpPercent);
        tb->addTextCentered(hpText, hpRect, irr::video::SColor(255, 255, 255, 255));
    }
    y += BAR_H + PAD;

    // Command button indicators (compact row of abbreviations)
    static const char* btnLabels[] = {"Sit","Stp","Reg","Fol","Grd","Tnt","Hld","GH","Foc","SH"};
    if (tb) {
        irr::s32 bx = x;
        for (int i = 0; i < PetPanelState::BUTTON_COUNT && i < 10; ++i) {
            irr::video::SColor col = state.buttonStates[i]
                ? irr::video::SColor(255, 100, 255, 100)   // Active — green
                : irr::video::SColor(255, 120, 120, 120);  // Inactive — gray
            tb->addText(btnLabels[i], bx, y, col);
            bx += tb->getTextWidth(btnLabels[i]) + 4;
            if (bx > layout.petPanel.LowerRightCorner.X - 10) break;
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

} // namespace Graphics
} // namespace EQT
