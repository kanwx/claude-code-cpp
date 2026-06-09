#pragma once

#include "claude/core/ApiTypes.hpp"
#include <vector>

namespace claude {

/// Unified rendering model for all content displayed during a streaming session.
/// ContentBlock is a recursive tree: ToolGroup nodes hold child blocks;
/// leaf nodes carry text, tool metadata, or error information.
struct ContentBlock {
    enum Type {
        UserMessage,    // User input text
        AnswerText,     // Assistant text response
        ThinkingBlock,  // Extended thinking content
        ToolProgress,   // Tool in-progress indicator
        ToolResult,     // Single tool result
        ToolGroup,      // Grouped tool use + result pair
        ErrorMessage    // Error output
    };

    Type type = UserMessage;

    String text;           // Primary text content (for UserMessage, AnswerText, ThinkingBlock, ErrorMessage)
    String detailText;     // Secondary detail (e.g. thinking signature, error context)
    String toolName;       // Tool name (for ToolProgress, ToolResult, ToolGroup)
    String activity;       // Activity description (for ToolProgress)

    ToolResultSummary summary;  // Structured summary (for ToolResult, ToolGroup)

    String rawResultPath;  // Path to raw result output file

    bool expanded = false; // Whether this block is expanded in the UI
    bool dimmed = false;   // Whether this block is rendered dimmed

    // Children must be declared after all other members to allow recursive type
    std::vector<ContentBlock> children;
};

} // namespace claude
