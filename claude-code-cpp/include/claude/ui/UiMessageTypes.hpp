#pragma once

#include "../core/Types.hpp"
#include <chrono>
#include <vector>
#include <string>
#include <atomic>

namespace claude {

// ========== Content Block Types ==========
// Mirror the Anthropic API content block structure.
// Each block represents one atomic piece of content within a message.

struct TextBlock {
    String text;
};

struct ToolUseBlock {
    String toolId;      // Unique ID for tool_use/tool_result pairing
    String toolName;
    String input;       // JSON string of arguments
};

struct ToolResultBlock {
    String toolUseId;   // Pairs with ToolUseBlock::toolId
    String toolName;    // Denormalized for rendering convenience
    String result;
    bool isError = false;
};

struct ThinkingBlock {
    String text;
    bool collapsed = true;   // UI expand/collapse state
};

struct RedactedThinkingBlock {
    // Placeholder for redacted thinking content
};

// ========== Collapsed Tool Group ==========
// Represents a group of consecutive read/search tools that are collapsed
// into a single summary line in the UI.

struct CollapsedToolGroup {
    int searchCount = 0;
    int readCount = 0;
    int listCount = 0;
    int bashCount = 0;
    int writeCount = 0;
    int editCount = 0;
    int memoryCount = 0;
    int hookCount = 0;
    int mcpCallCount = 0;
    std::vector<String> readFilePaths;
    std::vector<String> searchPatterns;
    String latestHint;
    std::chrono::steady_clock::time_point hintSetTime = std::chrono::steady_clock::now();
    std::vector<size_t> toolIndices;   // Indices into the message list for expanded view
    bool active = false;               // Still receiving tool calls

    String summaryText() const {
        std::vector<String> parts;
        if (readCount > 0) parts.push_back(active ? ("Reading " + std::to_string(readCount) + " file" + (readCount != 1 ? "s" : "")) : ("Read " + std::to_string(readCount) + " file" + (readCount != 1 ? "s" : "")));
        if (searchCount > 0) parts.push_back(active ? ("Searching " + std::to_string(searchCount) + " pattern" + (searchCount != 1 ? "s" : "")) : ("Searched " + std::to_string(searchCount) + " pattern" + (searchCount != 1 ? "s" : "")));
        if (listCount > 0) parts.push_back(active ? ("Listing " + std::to_string(listCount) + " director" + (listCount != 1 ? "ies" : "y")) : ("Listed " + std::to_string(listCount) + " director" + (listCount != 1 ? "ies" : "y")));
        if (writeCount > 0) parts.push_back(active ? ("Writing " + std::to_string(writeCount) + " file" + (writeCount != 1 ? "s" : "")) : ("Wrote " + std::to_string(writeCount) + " file" + (writeCount != 1 ? "s" : "")));
        if (editCount > 0) parts.push_back(active ? ("Editing " + std::to_string(editCount) + " file" + (editCount != 1 ? "s" : "")) : ("Edited " + std::to_string(editCount) + " file" + (editCount != 1 ? "s" : "")));
        if (bashCount > 0) parts.push_back(active ? ("Running " + std::to_string(bashCount) + " command" + (bashCount != 1 ? "s" : "")) : ("Ran " + std::to_string(bashCount) + " command" + (bashCount != 1 ? "s" : "")));
        if (mcpCallCount > 0) parts.push_back(active ? ("Calling " + std::to_string(mcpCallCount) + " MCP tool" + (mcpCallCount != 1 ? "s" : "")) : ("Called " + std::to_string(mcpCallCount) + " MCP tool" + (mcpCallCount != 1 ? "s" : "")));
        if (memoryCount > 0) parts.push_back(active ? ("Saving " + std::to_string(memoryCount) + " memor" + (memoryCount != 1 ? "ies" : "y")) : ("Saved " + std::to_string(memoryCount) + " memor" + (memoryCount != 1 ? "ies" : "y")));
        if (hookCount > 0) parts.push_back(active ? ("Running " + std::to_string(hookCount) + " hook" + (hookCount != 1 ? "s" : "")) : ("Ran " + std::to_string(hookCount) + " hook" + (hookCount != 1 ? "s" : "")));
        if (parts.empty()) return "[0 tool uses]";
        String result = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) result += ", " + parts[i];
        if (active) result += "...";
        return result;
    }
};

// ========== Message ID Generator ==========

class MessageIdGenerator {
public:
    static String next() {
        static std::atomic<uint64_t> counter{0};
        uint64_t id = counter.fetch_add(1, std::memory_order_relaxed);
        return "msg_" + std::to_string(id);
    }
};

// ========== Display Message ==========
// The output of normalization. Each DisplayMessage corresponds to one
// visual row/unit in the transcript. One API assistant message may
// produce multiple DisplayMessages after normalization.

struct DisplayMessage {
    enum class Type {
        UserPrompt,           // User text input
        AssistantText,        // Single text block from assistant
        AssistantThinking,    // Thinking block (expandable)
        AssistantToolUse,     // Tool use invocation
        UserToolResult,       // Tool result (paired to tool_use)
        SystemInfo,           // System informational message
        SystemError,          // Error message
        TurnDuration,         // "Did X for 5s" summary
        PermissionPrompt,     // Permission request UI
        CompactBoundary,      // Context compaction marker
        HookSummary,          // Hook progress summary
        CollapsedReadSearch,  // Grouped read/search tools (collapsed)
        GroupedToolUse,       // Multiple same-type tool uses merged into one row
        AgentProgress,        // Sub-agent/parallel task progress tree
        UserToolSuccess,      // Tool result — success
        UserToolError,        // Tool result — error
        UserToolRejected,     // Tool result — user rejected permission
        UserToolCanceled,     // Tool result — user canceled
        AssistantRedactedThinking, // Redacted thinking block (no content)
    };

    Type type;

    // Content — which fields are valid depends on type
    String text;                       // UserPrompt, AssistantText, SystemInfo, SystemError, TurnDuration
    ToolUseBlock toolUse;              // AssistantToolUse
    ToolResultBlock toolResult;        // UserToolResult
    ThinkingBlock thinking;            // AssistantThinking
    CollapsedToolGroup collapsedGroup; // CollapsedReadSearch
    std::vector<ToolUseRenderData> groupedTools;  // GroupedToolUse

    // Permission prompt fields (only valid when type == PermissionPrompt)
    String permissionToolName;
    String permissionActivity;
    int permissionSelectedIndex = 0;

    // Stable message ID for height caching and scroll anchoring
    String messageId;

    // Timestamp
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

    // UI state (mutable, only touched on UI thread)
    bool expanded = false;             // For thinking/collapsed groups
    int cachedHeight = -1;             // For virtual scroll, -1 = unknown

    // Factory helpers
    static DisplayMessage userPrompt(const String& text) {
        DisplayMessage msg;
        msg.type = Type::UserPrompt;
        msg.text = text;
        return msg;
    }

    static DisplayMessage assistantText(const String& text) {
        DisplayMessage msg;
        msg.type = Type::AssistantText;
        msg.text = text;
        return msg;
    }

    static DisplayMessage assistantThinking(const String& text, bool collapsed = true) {
        DisplayMessage msg;
        msg.type = Type::AssistantThinking;
        msg.thinking = {text, collapsed};
        msg.expanded = !collapsed;
        return msg;
    }

    static DisplayMessage assistantToolUse(const ToolUseBlock& block) {
        DisplayMessage msg;
        msg.type = Type::AssistantToolUse;
        msg.toolUse = block;
        return msg;
    }

    static DisplayMessage userToolResult(const ToolResultBlock& block) {
        DisplayMessage msg;
        msg.type = Type::UserToolResult;
        msg.toolResult = block;
        return msg;
    }

    static DisplayMessage systemInfo(const String& text) {
        DisplayMessage msg;
        msg.type = Type::SystemInfo;
        msg.text = text;
        return msg;
    }

    static DisplayMessage systemError(const String& text) {
        DisplayMessage msg;
        msg.type = Type::SystemError;
        msg.text = text;
        return msg;
    }

    static DisplayMessage turnDuration(const String& text) {
        DisplayMessage msg;
        msg.type = Type::TurnDuration;
        msg.text = text;
        return msg;
    }

    static DisplayMessage permissionPrompt(const String& toolName, const String& activity) {
        DisplayMessage msg;
        msg.type = Type::PermissionPrompt;
        msg.permissionToolName = toolName;
        msg.permissionActivity = activity;
        return msg;
    }

    static DisplayMessage collapsedReadSearch(CollapsedToolGroup group) {
        DisplayMessage msg;
        msg.type = Type::CollapsedReadSearch;
        msg.collapsedGroup = std::move(group);
        return msg;
    }

    static DisplayMessage groupedToolUse(std::vector<ToolUseRenderData> tools) {
        DisplayMessage msg;
        msg.type = Type::GroupedToolUse;
        msg.groupedTools = std::move(tools);
        return msg;
    }

    static DisplayMessage makeAgentProgress(const String& agentType,
                                             const String& description,
                                             int toolUses,
                                             long tokens,
                                             bool running) {
        DisplayMessage msg;
        msg.type = Type::AgentProgress;
        msg.text = description;
        msg.toolUse.toolName = agentType;
        msg.expanded = running;  // Reuse expanded for running state
        msg.toolResult.result = std::to_string(tokens) + " tokens";
        return msg;
    }

    static DisplayMessage userToolSuccess(const String& toolUseId,
                                           const String& toolName,
                                           const String& result) {
        DisplayMessage msg;
        msg.type = Type::UserToolSuccess;
        msg.messageId = MessageIdGenerator::next();
        msg.toolResult.toolUseId = toolUseId;
        msg.toolResult.toolName = toolName;
        msg.toolResult.result = result;
        msg.toolResult.isError = false;
        return msg;
    }

    static DisplayMessage userToolError(const String& toolUseId,
                                         const String& toolName,
                                         const String& result) {
        DisplayMessage msg;
        msg.type = Type::UserToolError;
        msg.messageId = MessageIdGenerator::next();
        msg.toolResult.toolUseId = toolUseId;
        msg.toolResult.toolName = toolName;
        msg.toolResult.result = result;
        msg.toolResult.isError = true;
        return msg;
    }

    static DisplayMessage userToolRejected(const String& toolUseId,
                                            const String& toolName) {
        DisplayMessage msg;
        msg.type = Type::UserToolRejected;
        msg.messageId = MessageIdGenerator::next();
        msg.toolResult.toolUseId = toolUseId;
        msg.toolResult.toolName = toolName;
        msg.toolResult.result = "Rejected";
        return msg;
    }

    static DisplayMessage userToolCanceled(const String& toolUseId,
                                            const String& toolName) {
        DisplayMessage msg;
        msg.type = Type::UserToolCanceled;
        msg.messageId = MessageIdGenerator::next();
        msg.toolResult.toolUseId = toolUseId;
        msg.toolResult.toolName = toolName;
        msg.toolResult.result = "Canceled";
        return msg;
    }

    static DisplayMessage assistantRedactedThinking() {
        DisplayMessage msg;
        msg.type = Type::AssistantRedactedThinking;
        msg.messageId = MessageIdGenerator::next();
        return msg;
    }
};

// ========== Normalization & Pairing ==========

/// Pair tool_use and tool_result messages by their tool IDs.
/// Scans the message list and sets ToolResultBlock::toolUseId references.
void buildToolPairing(std::vector<DisplayMessage>& messages);

/// Estimate the rendered height (in terminal rows) of a message.
/// Used by virtual scrolling for messages whose height hasn't been measured yet.
int estimateMessageHeight(const DisplayMessage& msg, int terminalWidth);

/// Check if a tool name is a read/search tool that should be collapsed.
bool isReadSearchTool(const String& toolName);

} // namespace claude
