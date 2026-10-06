/*
 * B442b (#151 rows 31-35) -- the confirmation dialog. See
 * include/client/graphics/ui/confirmation_dialog.h; the geometry is
 * confirmation_layout.h, which quotes EQUI_ConfirmationDialog.xml. This TU
 * computes no rect.
 */

#include "client/graphics/ui/confirmation_dialog.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/confirmation_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/state/event_bus.h"

#include <sstream>

namespace EQT {
namespace Graphics {

namespace {

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return x >= r.UpperLeftCorner.X && x < r.LowerRightCorner.X &&
           y >= r.UpperLeftCorner.Y && y < r.LowerRightCorner.Y;
}

const irr::video::SColor kText(255, 255, 255, 255);   // the buttons' <TextColor>
const irr::video::SColor kFrame(255, 80, 80, 80);

int measure(const std::string& s, const TextBatch* tb) {
    if (tb && tb->isInitialized()) return tb->getTextWidth(s);
    return static_cast<int>(s.size()) * 6;
}

void drawButton(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
                const char* label, bool hovered) {
    ui.drawRect(r, hovered ? irr::video::SColor(230, 90, 80, 60)
                           : irr::video::SColor(220, 60, 55, 48));
    ui.drawFrame(r, kFrame);
    if (tb) tb->addTextCentered(label, r, kText);
}

}  // namespace

void ConfirmationDialogState::open(uint8_t askerPurpose, const std::string& prompt, Buttons b) {
    *this = ConfirmationDialogState{};
    isOpen = !prompt.empty();
    purpose = askerPurpose;
    text = prompt;
    buttons = b;
}

ConfirmationClick processConfirmationClick(const UILayout& layout,
                                           ConfirmationDialogState& state,
                                           int mouseX, int mouseY, bool leftClicked) {
    state.yesHovered = state.noHovered = state.okHovered = false;
    if (!state.isOpen) return ConfirmationClick::None;
    if (!inside(ConfirmationLayout::windowRect(layout), mouseX, mouseY)) {
        return ConfirmationClick::None;
    }
    if (state.buttons == ConfirmationDialogState::Buttons::YesNo) {
        if (inside(ConfirmationLayout::yesRect(layout), mouseX, mouseY)) {
            state.yesHovered = true;
            return leftClicked ? ConfirmationClick::Yes : ConfirmationClick::Consumed;
        }
        if (inside(ConfirmationLayout::noRect(layout), mouseX, mouseY)) {
            state.noHovered = true;
            return leftClicked ? ConfirmationClick::No : ConfirmationClick::Consumed;
        }
    } else if (inside(ConfirmationLayout::okRect(layout), mouseX, mouseY)) {
        state.okHovered = true;
        return leftClicked ? ConfirmationClick::Ok : ConfirmationClick::Consumed;
    }
    return ConfirmationClick::Consumed;
}

void applyConfirmationRequested(ConfirmationDialogState& state,
                                const eqt::state::ConfirmationRequestedData& data) {
    // A second request while one is up replaces it: the official dialog is
    // one window, and the newer question is the one the player just asked.
    state.open(data.purpose, data.text, ConfirmationDialogState::Buttons::YesNo);
}

std::vector<std::string> wrapConfirmationText(const std::string& text, int width,
                                              const TextBatch* tb) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate, tb) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void renderConfirmationDialog(UIRenderer& ui, const UILayout& layout,
                              const ConfirmationDialogState& state) {
    if (!state.isOpen) return;
    auto* tb = ui.getTextBatch();

    ui.drawPanel(ConfirmationLayout::windowRect(layout));
    // Style_Titlebar true with no <Text>: an empty bar (measured, G41).
    ui.drawRect(ConfirmationLayout::titleRect(layout), irr::video::SColor(200, 26, 26, 34));

    // CD_TextOutput: WDT_Inner, the composed prompt word-wrapped.
    const auto box = ConfirmationLayout::textRect(layout);
    ui.drawRect(box, irr::video::SColor(220, 18, 18, 24));
    ui.drawFrame(box, kFrame);
    if (tb) {
        const int lineH = tb->getLineHeight() > 0 ? tb->getLineHeight() : 14;
        irr::s32 y = box.UpperLeftCorner.Y + 3;
        for (const auto& l : wrapConfirmationText(state.text, box.getWidth() - 8, tb)) {
            if (y + lineH > box.LowerRightCorner.Y) break;
            tb->addText(l, box.UpperLeftCorner.X + 4, y, kText);
            y += lineH;
        }
    }

    if (state.buttons == ConfirmationDialogState::Buttons::YesNo) {
        drawButton(ui, tb, ConfirmationLayout::yesRect(layout), "Yes", state.yesHovered);
        drawButton(ui, tb, ConfirmationLayout::noRect(layout), "No", state.noHovered);
    } else {
        drawButton(ui, tb, ConfirmationLayout::okRect(layout), "Ok", state.okHovered);
    }
}

}  // namespace Graphics
}  // namespace EQT
