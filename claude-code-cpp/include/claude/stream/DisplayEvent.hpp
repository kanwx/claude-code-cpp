#pragma once

#include <string>
#include <cstdint>
#include <utility>
#include "claude/core/ApiTypes.hpp"

namespace claude {

/// Type of event emitted by the stream buffer to the UI renderer.
enum class DisplayEventType {
    TextParagraph,   // Completed paragraph of assistant text
    TextPartial,     // Partial (streaming) text delta
    ThinkingBlock,   // Extended-thinking content
    ToolProgress,    // Tool progress indicator (Running/Waiting/Permission/Classifier)
    ToolResult,      // Tool execution result with summary
    ToolGroup,       // Logical grouping boundary for a tool call/result pair
    AnswerStart,     // Marks the beginning of an assistant answer
    AnswerEnd,       // Marks the end of an assistant answer
    TurnMetadata,    // Token usage, cost, model info for the turn
    SystemNotice,    // System-level notice message
    Tombstone,       // Placeholder for content that was compacted/removed
    Error            // Error event
};

/// Per-turn metadata carried by the TurnMetadata display event.
struct TurnMetadata {
    String modelName;
    int64_t contextUsed = 0;
    int64_t contextTotal = 0;
    int64_t inputTokens = 0;
    int64_t outputTokens = 0;
    String durationStr;
    String costStr;
    bool isStreaming = false;
};

/// Single event pushed from the stream buffer to the display layer.
struct DisplayEvent {
    DisplayEventType type = DisplayEventType::TextParagraph;

    // TextParagraph / TextPartial / ThinkingBlock / Tombstone / Error
    String text;

    // ThinkingBlock
    String thinkingText;

    // ToolProgress / ToolResult / ToolGroup
    String toolCallId;
    String toolName;

    // ToolProgress — activity string ("Running…", "Waiting…", etc.)
    String activity;

    // ToolResult
    ToolResultSummary summary;

    // ToolResult — path to raw result file (optional)
    String rawResultPath;

    // TurnMetadata
    TurnMetadata metadata;

    // SystemNotice
    String noticeText;

    // ---- Convenience constructors ----

    static DisplayEvent textParagraph(String t) {
        return DisplayEvent{.type = DisplayEventType::TextParagraph, .text = std::move(t)};
    }
    static DisplayEvent textPartial(String t) {
        return DisplayEvent{.type = DisplayEventType::TextPartial, .text = std::move(t)};
    }
    static DisplayEvent thinkingBlock(String t) {
        return DisplayEvent{.type = DisplayEventType::ThinkingBlock, .thinkingText = std::move(t)};
    }
    static DisplayEvent toolProgress(String callId, String name, String act) {
        return DisplayEvent{.type = DisplayEventType::ToolProgress,
                            .toolCallId = std::move(callId),
                            .toolName = std::move(name),
                            .activity = std::move(act)};
    }
    static DisplayEvent toolResult(String callId, String name,
                                   ToolResultSummary summ, String rawPath = "") {
        return DisplayEvent{.type = DisplayEventType::ToolResult,
                            .toolCallId = std::move(callId),
                            .toolName = std::move(name),
                            .summary = std::move(summ),
                            .rawResultPath = std::move(rawPath)};
    }
    static DisplayEvent toolGroup(String callId, String name) {
        return DisplayEvent{.type = DisplayEventType::ToolGroup,
                            .toolCallId = std::move(callId),
                            .toolName = std::move(name)};
    }
    static DisplayEvent answerStart() {
        return DisplayEvent{.type = DisplayEventType::AnswerStart};
    }
    static DisplayEvent answerEnd() {
        return DisplayEvent{.type = DisplayEventType::AnswerEnd};
    }
    static DisplayEvent turnMeta(TurnMetadata meta) {
        return DisplayEvent{.type = DisplayEventType::TurnMetadata, .metadata = std::move(meta)};
    }
    static DisplayEvent systemNotice(String notice) {
        return DisplayEvent{.type = DisplayEventType::SystemNotice, .noticeText = std::move(notice)};
    }
    static DisplayEvent tombstone(String t) {
        return DisplayEvent{.type = DisplayEventType::Tombstone, .text = std::move(t)};
    }
    static DisplayEvent error(String t) {
        return DisplayEvent{.type = DisplayEventType::Error, .text = std::move(t)};
    }
};

} // namespace claude
