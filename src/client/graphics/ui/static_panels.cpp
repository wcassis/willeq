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

} // namespace Graphics
} // namespace EQT
