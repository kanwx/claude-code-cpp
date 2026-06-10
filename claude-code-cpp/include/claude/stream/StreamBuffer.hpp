#pragma once

#include "claude/stream/TypedStreamEvent.hpp"
#include "claude/stream/DisplayEvent.hpp"
#include "claude/stream/IncrementalBlockParser.hpp"
#include "claude/core/ApiTypes.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <chrono>

namespace claude {

class StreamBuffer {
public:
    void feed(TypedStreamEvent&& event);
    void feed(StreamToolEvent&& event);
    void setDisplayCallback(std::function<void(DisplayEvent&&)> cb);
    void flush();

private:
    std::mutex mutex_;
    std::function<void(DisplayEvent&&)> onDisplay_;
    IncrementalBlockParser blockParser_;

    // Text buffer
    String textAccumulator_;
    static constexpr size_t FLUSH_THRESHOLD = 256;
    void flushTextBuffer(bool isComplete);

    // Tool call tracker
    struct PendingToolCall {
        String callId;
        String toolName;
        String activity;
        StreamToolEventType state = StreamToolEventType::Queued;
    };
    std::map<String, PendingToolCall> pendingTools_;

    // Thinking bypass
    String thinkingAccumulator_;
    std::chrono::steady_clock::time_point lastThinkingEmit_;
    static constexpr auto THINKING_MIN_INTERVAL = std::chrono::milliseconds(50);
    static constexpr size_t THINKING_MIN_CHARS = 256;
    size_t thinkingCharsSinceEmit_ = 0;
    void maybeEmitThinkingUpdate(bool force = false);

    // Internal state
    bool answerStarted_ = false;
    bool inThinkingTag_ = false;  // Track unclosed thinking tags across chunks<arg_key>text</arg_key><arg_value> tags across chunks
    std::chrono::steady_clock::time_point answerStartTime_;

    void emit(DisplayEvent&& event);
};

} // namespace claude
