#include "client/graphics/ui/chat_input_field.h"
#include "client/input/hotkey_manager.h"
#include <algorithm>
#include <cctype>

namespace eqt {
namespace ui {

ChatInputField::ChatInputField() {
}

void ChatInputField::render(irr::video::IVideoDriver* driver,
                            irr::gui::IGUIEnvironment* gui,
                            const irr::core::recti& bounds) {
    // Draw background
    driver->draw2DRectangle(getBackgroundColor(), bounds);

    // Get font
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // Calculate text position (with some padding)
    int textX = bounds.UpperLeftCorner.X + 4;
    int textY = bounds.UpperLeftCorner.Y + (bounds.getHeight() - 10) / 2;  // Approximate font height of 10

    // Draw prompt
    std::wstring promptW(prompt_.begin(), prompt_.end());
    irr::core::dimension2du promptDim = font->getDimension(promptW.c_str());
    font->draw(promptW.c_str(),
               irr::core::recti(textX, textY, textX + promptDim.Width, textY + promptDim.Height),
               getPromptColor());

    textX += promptDim.Width;

    // Draw text
    std::wstring textW(text_.begin(), text_.end());
    if (!textW.empty()) {
        font->draw(textW.c_str(),
                   irr::core::recti(textX, textY, bounds.LowerRightCorner.X - 4, textY + 12),
                   getTextColor());
    }

    // Draw cursor if focused and visible
    if (focused_ && cursorVisible_) {
        // Calculate cursor X position
        std::wstring beforeCursor(text_.begin(), text_.begin() + cursorPos_);
        irr::core::dimension2du beforeDim = font->getDimension(beforeCursor.c_str());
        int cursorX = textX + beforeDim.Width;

        // Draw cursor line
        driver->draw2DLine(
            irr::core::vector2di(cursorX, textY),
            irr::core::vector2di(cursorX, textY + 10),
            getCursorColor());
    }
}

bool ChatInputField::handleKeyPress(irr::EKEY_CODE key, wchar_t character, bool shift, bool ctrl) {
    if (!focused_) return false;

    // Use HotkeyManager for SubmitInput and CancelInput
    auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
    bool alt = false;
    auto action = hotkeyMgr.getAction(key, ctrl, shift, alt, eqt::input::HotkeyMode::Global);

    if (action.has_value()) {
        if (action.value() == eqt::input::HotkeyAction::SubmitInput) {
            submit();
            return true;
        }
    }

    // ESC clears and unfocuses (check key directly since CancelInput may conflict with ClearTarget)
    if (key == irr::KEY_ESCAPE) {
        clear();
        setFocused(false);
        return true;
    }

    switch (key) {
        case irr::KEY_BACK:
            deleteCharBefore();
            return true;

        case irr::KEY_DELETE:
            deleteCharAfter();
            return true;

        case irr::KEY_LEFT:
            moveCursorLeft(ctrl);
            return true;

        case irr::KEY_RIGHT:
            moveCursorRight(ctrl);
            return true;

        case irr::KEY_HOME:
            moveCursorHome();
            return true;

        case irr::KEY_END:
            moveCursorEnd();
            return true;

        case irr::KEY_UP:
            historyUp();
            return true;

        case irr::KEY_DOWN:
            historyDown();
            return true;

        default:
            // Handle character input
            if (character >= 32 && character < 127) {  // Printable ASCII
                insertChar(character);
                return true;
            }
            break;
    }

    return false;
}

void ChatInputField::setFocused(bool focused) {
    focused_ = focused;
    if (focused) {
        cursorVisible_ = true;
        lastBlinkTime_ = 0;  // Will be set on next update
    }
}

void ChatInputField::insertChar(wchar_t c) {
    browsingHistory_ = false;
    text_.insert(cursorPos_, 1, static_cast<char>(c));
    cursorPos_++;
}

void ChatInputField::insertText(const std::string& text) {
    browsingHistory_ = false;
    text_.insert(cursorPos_, text);
    cursorPos_ += text.length();
}

void ChatInputField::deleteCharBefore() {
    browsingHistory_ = false;
    if (cursorPos_ > 0) {
        text_.erase(cursorPos_ - 1, 1);
        cursorPos_--;
    }
}

void ChatInputField::deleteCharAfter() {
    browsingHistory_ = false;
    if (cursorPos_ < text_.length()) {
        text_.erase(cursorPos_, 1);
    }
}

void ChatInputField::clear() {
    text_.clear();
    cursorPos_ = 0;
    browsingHistory_ = false;
}

void ChatInputField::moveCursorLeft(bool word) {
    if (cursorPos_ == 0) return;

    if (word) {
        cursorPos_ = findPrevWordBoundary();
    } else {
        cursorPos_--;
    }
}

void ChatInputField::moveCursorRight(bool word) {
    if (cursorPos_ >= text_.length()) return;

    if (word) {
        cursorPos_ = findNextWordBoundary();
    } else {
        cursorPos_++;
    }
}

void ChatInputField::moveCursorHome() {
    cursorPos_ = 0;
}

void ChatInputField::moveCursorEnd() {
    cursorPos_ = text_.length();
}

void ChatInputField::historyUp() {
    if (history_.empty()) return;

    if (!browsingHistory_) {
        // Start browsing - save current text
        savedCurrentText_ = text_;
        historyPos_ = history_.size();
        browsingHistory_ = true;
    }

    if (historyPos_ > 0) {
        historyPos_--;
        text_ = history_[historyPos_];
        cursorPos_ = text_.length();
    }
}

void ChatInputField::historyDown() {
    if (!browsingHistory_) return;

    historyPos_++;
    if (historyPos_ >= history_.size()) {
        // Restore saved text
        text_ = savedCurrentText_;
        cursorPos_ = text_.length();
        browsingHistory_ = false;
    } else {
        text_ = history_[historyPos_];
        cursorPos_ = text_.length();
    }
}

void ChatInputField::addToHistory(const std::string& text) {
    if (text.empty()) return;

    // Don't add duplicates of the last entry
    if (!history_.empty() && history_.back() == text) return;

    history_.push_back(text);

    // Limit history size
    while (history_.size() > MAX_HISTORY) {
        history_.erase(history_.begin());
    }
}

void ChatInputField::clearHistory() {
    history_.clear();
    historyPos_ = 0;
    browsingHistory_ = false;
}

void ChatInputField::setText(const std::string& text) {
    text_ = text;
    cursorPos_ = text_.length();
    browsingHistory_ = false;
}

void ChatInputField::update(uint32_t currentTimeMs) {
    if (!focused_) return;

    // Initialize blink time if needed
    if (lastBlinkTime_ == 0) {
        lastBlinkTime_ = currentTimeMs;
    }

    // Toggle cursor visibility
    if (currentTimeMs - lastBlinkTime_ >= BLINK_INTERVAL_MS) {
        cursorVisible_ = !cursorVisible_;
        lastBlinkTime_ = currentTimeMs;
    }
}

void ChatInputField::submit() {
    if (text_.empty()) return;

    // Add to history
    addToHistory(text_);

    // Call callback
    if (submitCallback_) {
        submitCallback_(text_);
    }

    // Clear input
    clear();
}

size_t ChatInputField::findPrevWordBoundary() const {
    if (cursorPos_ == 0) return 0;

    size_t pos = cursorPos_ - 1;

    // Skip any spaces before cursor
    while (pos > 0 && std::isspace(text_[pos])) {
        pos--;
    }

    // Find start of word
    while (pos > 0 && !std::isspace(text_[pos - 1])) {
        pos--;
    }

    return pos;
}

size_t ChatInputField::findNextWordBoundary() const {
    if (cursorPos_ >= text_.length()) return text_.length();

    size_t pos = cursorPos_;

    // Skip current word
    while (pos < text_.length() && !std::isspace(text_[pos])) {
        pos++;
    }

    // Skip spaces
    while (pos < text_.length() && std::isspace(text_[pos])) {
        pos++;
    }

    return pos;
}

} // namespace ui
} // namespace eqt
