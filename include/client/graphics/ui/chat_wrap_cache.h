/*
 * T462 (board B505): the chat panel's word-wrap, done on CHANGE instead of
 * every frame.
 *
 * renderChatPanel() used to format and word-wrap EVERY message in the chat
 * buffer (500 on the live client) on EVERY frame, measuring each candidate
 * break with TextBatch::getTextWidth(), and only then pick the ~9 lines it
 * draws. On the Orange Pi that grew the Static UI section from 11.9 ms to
 * 22.0 ms over a five-minute session (T459, P1), and on x86 llvmpipe a 500
 * message buffer cost ~10 ms of the frame (T462's account).
 *
 * The cache keeps the wrapped lines of every buffered message, keyed by the
 * message's position in the buffer's append sequence. Each sync():
 *   - rebuilds everything when the width, the timestamp setting or the
 *     buffer's clear generation changed;
 *   - otherwise drops the lines of messages the buffer has evicted from its
 *     front and wraps only the messages appended since the last sync.
 * So a frame with no new chat does no formatting and no measuring at all.
 *
 * wrapChatText() is the pre-T462 loop, verbatim, so the lines -- and the
 * pixels -- are unchanged; tests/test_static_ui_frame_cost.cpp pins both.
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace eqt { namespace ui { class ChatMessageBuffer; } }

namespace EQT {
namespace Graphics {

struct ChatWrappedLine {
    std::string text;
    irr::video::SColor color;
};

// Pixel width of a string in the font the panel draws with.
using ChatTextMeasure = std::function<irr::s32(const std::string&)>;

/**
 * Word-wrap one formatted message into `out` (appended). The pre-T462
 * renderChatPanel() loop, unchanged: while the text is wider than `areaW`,
 * binary-search the longest prefix that fits, break at the last space in its
 * back half if there is one, drop one leading space from the remainder.
 * Returns the number of lines appended.
 */
size_t wrapChatText(std::string display, irr::video::SColor color, irr::s32 areaW,
                    const ChatTextMeasure& measure, std::deque<ChatWrappedLine>& out);

class ChatWrapCache {
public:
    /** Bring the cached lines in line with `buffer`. O(messages changed). */
    void sync(const eqt::ui::ChatMessageBuffer& buffer, irr::s32 areaW,
              bool showTimestamps, const ChatTextMeasure& measure);

    size_t lineCount() const { return lines_.size(); }
    const ChatWrappedLine& line(size_t i) const { return lines_[i]; }

    /**
     * Test seam: how many messages this cache has formatted and wrapped since
     * it was constructed. A frame with no chat change adds zero.
     */
    uint64_t messagesWrapped() const { return messagesWrapped_; }

private:
    std::deque<ChatWrappedLine> lines_;       // every cached message's lines, oldest first
    std::deque<uint32_t> linesPerMessage_;    // lines_ contributed by each cached message
    uint64_t firstSeq_ = 0;                   // append sequence of the oldest cached message
    uint64_t endSeq_ = 0;                     // one past the newest cached message
    uint64_t clearGeneration_ = 0;
    irr::s32 areaW_ = 0;
    bool showTimestamps_ = false;
    bool valid_ = false;
    uint64_t messagesWrapped_ = 0;
};

} // namespace Graphics
} // namespace EQT
