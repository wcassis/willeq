#pragma once

#include "ui_settings.h"
#include <irrlicht.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace eqt {
namespace ui {

// Callback when input is submitted (Enter pressed)
using InputSubmitCallback = std::function<void(const std::string& text)>;

class ChatInputField {
public:
    ChatInputField();
    ~ChatInputField() = default;

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui,
                const irr::core::recti& bounds);

    // Input handling - returns true if input was consumed
    bool handleKeyPress(irr::EKEY_CODE key, wchar_t character, bool shift, bool ctrl);

    // Focus management
    void setFocused(bool focused);
    bool isFocused() const { return focused_; }

    // Text manipulation
    void insertChar(wchar_t c);
    void insertText(const std::string& text);
    void deleteCharBefore();  // Backspace
    void deleteCharAfter();   // Delete key
    void clear();

    // Cursor movement
    void moveCursorLeft(bool word = false);
    void moveCursorRight(bool word = false);
    void moveCursorHome();
    void moveCursorEnd();

    // History
    void historyUp();
    void historyDown();
    void addToHistory(const std::string& text);
    void clearHistory();

    // Text access
    const std::string& getText() const { return text_; }
    void setText(const std::string& text);

    // Callbacks
    void setSubmitCallback(InputSubmitCallback callback) { submitCallback_ = callback; }

    // Cursor blink timing
    void update(uint32_t currentTimeMs);

    // Prompt character (shown before input)
    void setPrompt(const std::string& prompt) { prompt_ = prompt; }
    const std::string& getPrompt() const { return prompt_; }

private:
    // Submit the current text
    void submit();

    // Find word boundaries for Ctrl+Left/Right
    size_t findPrevWordBoundary() const;
    size_t findNextWordBoundary() const;

    // Text state
    std::string text_;
    size_t cursorPos_ = 0;
    std::string prompt_ = "> ";

    // Focus state
    bool focused_ = false;

    // Cursor blink
    uint32_t lastBlinkTime_ = 0;
    bool cursorVisible_ = true;
    static constexpr uint32_t BLINK_INTERVAL_MS = 500;

    // History
    std::vector<std::string> history_;
    size_t historyPos_ = 0;
    std::string savedCurrentText_;  // Saved text when browsing history
    bool browsingHistory_ = false;
    static constexpr size_t MAX_HISTORY = 100;

    // Callbacks
    InputSubmitCallback submitCallback_;

    // Color accessors - read from UISettings
    irr::video::SColor getBackgroundColor() const { return UISettings::instance().chatInput().background; }
    irr::video::SColor getTextColor() const { return UISettings::instance().chatInput().text; }
    irr::video::SColor getCursorColor() const { return UISettings::instance().chatInput().cursor; }
    irr::video::SColor getPromptColor() const { return UISettings::instance().chatInput().prompt; }
};

} // namespace ui
} // namespace eqt
