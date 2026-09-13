/*
 * T462 (board B505): the chat panel's wrap cache. See chat_wrap_cache.h.
 */

#include "client/graphics/ui/chat_wrap_cache.h"
#include "client/chat/chat_message_buffer.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

size_t wrapChatText(std::string display, irr::video::SColor color, irr::s32 areaW,
                    const ChatTextMeasure& measure, std::deque<ChatWrappedLine>& out) {
    size_t added = 0;
    // Word-wrap using pixel width measurement (the pre-T462 renderChatPanel()
    // loop, statement for statement).
    while (measure(display) > areaW) {
        // Binary search for the longest substring that fits
        int lo = 1, hi = static_cast<int>(display.size());
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (measure(display.substr(0, mid)) <= areaW)
                lo = mid;
            else
                hi = mid - 1;
        }
        // Find last space within the fitting range for word break
        int breakAt = lo;
        for (int j = lo; j > lo / 2; --j) {
            if (display[j] == ' ') { breakAt = j; break; }
        }
        out.push_back({display.substr(0, breakAt), color});
        ++added;
        display = display.substr(breakAt);
        if (!display.empty() && display[0] == ' ') display = display.substr(1);
    }
    if (!display.empty()) {
        out.push_back({display, color});
        ++added;
    }
    return added;
}

void ChatWrapCache::sync(const eqt::ui::ChatMessageBuffer& buffer, irr::s32 areaW,
                         bool showTimestamps, const ChatTextMeasure& measure) {
    const auto& messages = buffer.getMessages();
    const uint64_t bufEnd = buffer.appendedCount();
    const uint64_t bufFirst = bufEnd - messages.size();

    // A different width or timestamp setting changes every line; a clear()
    // restarts the sequence. Either way, start over.
    if (!valid_ || buffer.clearGeneration() != clearGeneration_ ||
        areaW != areaW_ || showTimestamps != showTimestamps_) {
        lines_.clear();
        linesPerMessage_.clear();
        firstSeq_ = endSeq_ = bufFirst;
        clearGeneration_ = buffer.clearGeneration();
        areaW_ = areaW;
        showTimestamps_ = showTimestamps;
        valid_ = true;
    }

    // Drop the lines of messages the buffer has evicted from its front.
    while (firstSeq_ < bufFirst && !linesPerMessage_.empty()) {
        lines_.erase(lines_.begin(), lines_.begin() + linesPerMessage_.front());
        linesPerMessage_.pop_front();
        ++firstSeq_;
    }
    if (linesPerMessage_.empty()) {
        // Everything cached was evicted (or nothing was cached): anything
        // between the old end and the buffer's new front is gone too.
        firstSeq_ = endSeq_ = std::max(endSeq_, bufFirst);
    }

    // Wrap only the messages appended since the last sync.
    for (uint64_t seq = endSeq_; seq < bufEnd; ++seq) {
        const eqt::ui::ChatMessage& msg = messages[static_cast<size_t>(seq - bufFirst)];
        linesPerMessage_.push_back(static_cast<uint32_t>(wrapChatText(
            eqt::ui::formatMessageForDisplay(msg, showTimestamps),
            irr::video::SColor(msg.color), areaW, measure, lines_)));
        ++messagesWrapped_;
    }
    endSeq_ = bufEnd;
}

} // namespace Graphics
} // namespace EQT
