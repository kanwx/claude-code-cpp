#pragma once

#include "claude/core/ApiTypes.hpp"
#include "claude/stream/MessageTypes.hpp"
#include <vector>

namespace claude {

/// Unified rendering model for all content displayed during a streaming session.
/// ContentBlock is a recursive tree: ToolGroup nodes hold child blocks;
/// leaf nodes carry text, tool metadata, or error information.
struct ContentBlock {
    enum Type {
        UserMessage,      // User input text
        AnswerText,       // Assistant text response
        ThinkingBlock,    // Extended thinking content
        ToolProgress,     // Tool in-progress indicator
        ToolResult,       // Single tool result
        ToolGroup,        // Grouped tool use + result pair
        AgentProgress,    // Sub-agent/parallel task progress
        ErrorMessage,     // Error output
        SystemMessage,    // System-level notice (hook summaries, memory saved, etc.)
        CompactBoundary,  // "─── Earlier conversation compacted ───" divider
        CollapsedGroup,   // Collapsed read/search group (displays summary, expands to children)
        TurnDuration      // Inline "Pondered in 23s · 4.2K tokens · $0.08"
    };

    Type type = UserMessage;

    static const char* typeName(Type t) {
        switch (t) {
            case UserMessage:     return "UserMessage";
            case AnswerText:      return "AnswerText";
            case ThinkingBlock:   return "ThinkingBlock";
            case ToolProgress:    return "ToolProgress";
            case ToolResult:      return "ToolResult";
            case ToolGroup:       return "ToolGroup";
            case AgentProgress:   return "AgentProgress";
            case ErrorMessage:    return "ErrorMessage";
            case SystemMessage:   return "SystemMessage";
            case CompactBoundary: return "CompactBoundary";
            case CollapsedGroup:  return "CollapsedGroup";
            case TurnDuration:    return "TurnDuration";
        }
        return "Unknown";
    }

    String text;           // Primary text content (for UserMessage, AnswerText, ThinkingBlock, ErrorMessage, SystemMessage)
    String detailText;     // Secondary detail (e.g. thinking signature, error context)
    String toolName;       // Tool name (for ToolProgress, ToolResult, ToolGroup, AgentProgress, CollapsedGroup)
    String activity;       // Activity description (for ToolProgress)
    String toolCallId;     // API tool call ID (for ToolProgress→ToolResult matching)

    ToolResultSummary summary;  // Structured summary (for ToolResult, ToolGroup, CollapsedGroup)

    String rawResultPath;  // Path to raw result output file

    bool expanded = false; // Whether this block is expanded in the UI
    bool dimmed = false;   // Whether this block is rendered dimmed
    bool isFirst = false;  // True for the first AnswerText block in a response (gets ⏺ prefix)

    // ---- Extended fields (Phase 1) ----

    String uuid;                          // Stable identity for React-key-like matching
    uint64_t stableId = 0;                // monotonic ID for incremental diff matching
    SystemMessageSubtype systemSubtype = SystemMessageSubtype::Informational;
    ToolResultStatus resultStatus = ToolResultStatus::Success;
    UserInputType userInputType = UserInputType::Text;
    std::vector<String> toolUseIds;       // IDs of tool_use blocks in a CollapsedGroup
    int snippedCount = 0;                 // For MicroCompactBoundary
    bool hasContentAfter = false;         // Drives past/present tense in collapsed groups

    // Children must be declared after all other members to allow recursive type
    std::vector<ContentBlock> children;
};

} // namespace claude
