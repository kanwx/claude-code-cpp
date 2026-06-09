#pragma once

#include "claude/core/ApiTypes.hpp"
#include "claude/core/Types.hpp"

namespace claude {

// ========== Stream Event Types ==========

/// Typed stream event type for SSE event routing.
/// Each value corresponds to a specific kind of event in the streaming pipeline.
enum class StreamEventType {
    TextDelta,
    ThinkingDelta,
    InputJsonDelta,
    ToolUseStart,
    ToolUseComplete,
    TextBlockStart,
    TextBlockStop,
    ThinkingBlockStart,
    ThinkingBlockStop,
    StreamStart,
    StreamEnd,
    UsageUpdate,
    Error
};

// ========== Usage Info ==========

/// Lightweight usage info carried by stream events.
struct UsageInfo {
    int64_t promptTokens       = 0;
    int64_t completionTokens   = 0;
    int64_t cacheReadTokens    = 0;
    int64_t cacheCreationTokens = 0;
};

// ========== Typed Stream Event ==========

/// A discriminated (tagged) stream event used for SSE event routing.
/// Uses designated initializers for ergonomic construction.
struct TypedStreamEvent {
    StreamEventType type   = StreamEventType::TextDelta;
    String         text    = {};
    int            blockIndex = -1;
    ToolCall       toolCall = {};
    UsageInfo      usage    = {};
    String         stopReason = {};
    String         error    = {};
};

} // namespace claude
