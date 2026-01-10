#pragma once

#include "window_base.h"
#include "chat_message_buffer.h"
#include "chat_input_field.h"
#include "command_autocomplete.h"
#include "ui_settings.h"
#include <memory>
#include <functional>
#include <set>

namespace eqt {
namespace ui {

class CommandRegistry;

// Callback when user submits a command or message
using ChatSubmitCallback = std::function<void(const std::string& text)>;

// Callback when user clicks a link in chat
using LinkClickCallback = std::function<void(const MessageLink& link)>;

class ChatWindow : public WindowBase {
public:
    ChatWindow();
    ~ChatWindow() override = default;

    // Initialize with screen dimensions (for default positioning)
    void init(int screenWidth, int screenHeight);

    // Screen resize
    void onResize(int screenWidth, int screenHeight);

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling - returns true if input was consumed
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;
    bool handleMouseWheel(float delta);
    bool handleKeyPress(irr::EKEY_CODE key, wchar_t character, bool shift, bool ctrl);

    // Focus the input field
    void focusInput();
    void unfocusInput();
    bool isInputFocused() const { return inputField_.isFocused(); }

    // Insert text into input field (e.g., "/" to start command)
    void insertText(const std::string& text);

    // Add messages
    void addMessage(ChatMessage msg);
    void addSystemMessage(const std::string& text, ChatChannel channel = ChatChannel::System);

    // Get the message buffer (for external message routing)
    ChatMessageBuffer* getMessageBuffer() { return &messageBuffer_; }

    // Set callback for when user submits input
    void setSubmitCallback(ChatSubmitCallback callback);

    // Set command registry for auto-completion
    void setCommandRegistry(CommandRegistry* registry);

    // Set entity name provider for player name auto-completion
    void setEntityNameProvider(EntityNameProvider provider);

    // Set callback for when user clicks a link in chat
    void setLinkClickCallback(LinkClickCallback callback);

    // Find link at screen position (returns nullptr if none)
    const MessageLink* getLinkAtPosition(int x, int y) const;

    // Update (for cursor blink, etc.)
    void update(uint32_t currentTimeMs);

    // Scroll control
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void scrollToBottom();
    bool isScrolledToBottom() const;

    // Timestamp display
    void setShowTimestamps(bool show) { showTimestamps_ = show; }
    bool getShowTimestamps() const { return showTimestamps_; }
    void toggleTimestamps() { showTimestamps_ = !showTimestamps_; }

    // Channel filtering
    void setChannelEnabled(ChatChannel channel, bool enabled);
    bool isChannelEnabled(ChatChannel channel) const;
    void toggleChannel(ChatChannel channel);
    void enableAllChannels();
    void disableAllChannels();

    // Window sizing constraints
    void setMinSize(int width, int height);
    void setMaxSize(int width, int height);

    // Settings persistence
    void saveSettings(const std::string& filename = "config/chat_settings.json");
    void loadSettings(const std::string& filename = "config/chat_settings.json");

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui) override;

private:
    // Calculate visible line count based on window height
    int calculateVisibleLines() const;

    // Render the message area
    void renderMessages(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui,
                        const irr::core::recti& messageArea);

    // Scrollbar rendering and hit testing
    void renderScrollbar(irr::video::IVideoDriver* driver);
    irr::core::recti getScrollbarTrackBounds() const;
    irr::core::recti getScrollbarThumbBounds() const;
    bool isOnScrollbar(int x, int y) const;
    bool isOnScrollUpButton(int x, int y) const;
    bool isOnScrollDownButton(int x, int y) const;
    int getMaxScrollOffset() const;

    // Check if point is on resize edge
    bool isOnResizeEdge(int x, int y) const;

    // Apply size constraints
    void constrainSize();

    // Components
    ChatMessageBuffer messageBuffer_;
    ChatInputField inputField_;
    CommandAutoComplete autoComplete_;

    // Track last input text to detect changes (for resetting auto-complete)
    std::string lastInputText_;

    // Scroll state
    int scrollOffset_ = 0;  // Number of lines scrolled up from bottom
    int visibleLines_ = 5;

    // Layout accessors - read from UISettings
    int getInputFieldHeight() const { return UISettings::instance().chat().inputFieldHeight; }
    int getScrollbarWidth() const { return UISettings::instance().chat().scrollbarWidth; }
    int getScrollButtonHeight() const { return UISettings::instance().chat().scrollButtonHeight; }
    int getResizeEdgeWidth() const { return UISettings::instance().chat().resizeEdgeWidth; }

    // Scrollbar state
    bool draggingScrollbar_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;
    bool scrollUpHovered_ = false;
    bool scrollDownHovered_ = false;

    // Resize state
    bool resizing_ = false;
    bool resizeRight_ = false;
    bool resizeTop_ = false;
    irr::core::vector2di resizeStartPos_;
    irr::core::recti resizeStartBounds_;

    // Size constraints
    int minWidth_ = 200;
    int minHeight_ = 80;
    int maxWidth_ = 1920;
    int maxHeight_ = 600;

    // Callbacks
    ChatSubmitCallback submitCallback_;
    LinkClickCallback linkClickCallback_;

    // Screen dimensions (for resize constraints)
    int screenWidth_ = 1920;
    int screenHeight_ = 1080;

    // Display options
    bool showTimestamps_ = false;

    // Channel filter (channels to display - all enabled by default)
    std::set<ChatChannel> enabledChannels_;
    void initDefaultChannels();

    // Rendered link tracking for click detection
    struct RenderedLink {
        irr::core::recti bounds;      // Screen rectangle of the link
        MessageLink link;              // Copy of link data
        size_t messageIndex;           // Index in visible messages list
    };
    std::vector<RenderedLink> renderedLinks_;

    // Performance: cached wrapped lines per message
    // Key is message index, value is the wrapped lines
    struct CachedWrappedMessage {
        std::vector<std::wstring> lines;
        irr::video::SColor color;
        bool hasLinks;
        const ChatMessage* msg;  // Pointer to original message (for link rendering)
    };
    mutable std::vector<CachedWrappedMessage> wrappedLineCache_;
    mutable int wrappedLineCacheWidth_ = -1;  // Width used for caching
    mutable size_t wrappedLineCacheMessageCount_ = 0;  // Number of messages when cache was built
    mutable bool wrappedLineCacheShowTimestamps_ = false;  // Timestamp setting when cache was built

    // Invalidate wrapped line cache (called on resize)
    void invalidateWrappedLineCache() { wrappedLineCacheWidth_ = -1; }

    // Hovered link tracking for visual feedback
    int hoveredLinkIndex_ = -1;  // Index into renderedLinks_, -1 means none
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;

    // Helper to render a message with links highlighted
    void renderMessageWithLinks(irr::video::IVideoDriver* driver,
                                irr::gui::IGUIFont* font,
                                const ChatMessage& msg,
                                const std::string& formattedText,
                                int x, int y, int maxWidth,
                                size_t messageIndex);

    // Link color accessors - read from UISettings
    irr::video::SColor getLinkColorNpc() const { return UISettings::instance().chat().linkColorNpc; }
    irr::video::SColor getLinkColorItem() const { return UISettings::instance().chat().linkColorItem; }
};

} // namespace ui
} // namespace eqt
