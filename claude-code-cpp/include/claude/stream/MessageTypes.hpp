#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <chrono>
#include "claude/core/ApiTypes.hpp"

namespace claude {

// ========== UI-level enums ==========

/// Top-level role for a UI message (distinct from API MessageRole in ApiTypes.hpp)
enum class UIMessageRole {
    User,
    Assistant,
    System,
    Attachment,
    Progress
};

/// System message subtypes — each gets its own rendering
enum class SystemMessageSubtype {
    Informational,
    ApiError,
    ApiMetrics,
    CompactBoundary,
    MicroCompactBoundary,
    TurnDuration,
    MemorySaved,
    StopHookSummary,
    AgentsKilled,
    BridgeStatus,
    LocalCommand,
    ScheduledTaskFire,
    PermissionRetry,
    AwaySummary
};

/// Tool result status — drives color and icon in result display
enum class ToolResultStatus {
    Success,
    Error,
    Rejected,
    Cancelled,
    InProgress
};

/// User input type — drives per-type rendering of user messages
enum class UserInputType {
    Text,
    Command,   // /slash command
    Bash,      // !bash command
    Memory,    // auto-memory input
    Image,     // image attachment
    Plan,      // plan mode input
    ToolResult // user message wrapping a tool result
};

// ========== UI Message struct ==========

/// Rich UI-level message that sits between API messages and ContentBlock tree.
/// This is the intermediate model the normalization pipeline operates on.
struct UIMessage {
    String uuid;                     // Deterministic UUID (stable across renders)
    UIMessageRole role = UIMessageRole::User;

    // ---- User-specific fields ----
    UserInputType userType = UserInputType::Text;
    String userText;                 // Raw user input text
    String commandName;              // For /command type
    String bashInput;                // For !bash type
    String bashStdout;
    String bashStderr;
    int bashExitCode = 0;
    double bashDurationMs = 0.0;
    String imagePath;                // For image type

    // Tool result (when userType == ToolResult)
    String toolUseId;                // Links to the tool_use that produced this
    String toolName;
    String toolResultContent;        // Raw result text
    ToolResultStatus toolResultStatus = ToolResultStatus::Success;
    ToolResultSummary toolResultSummary;
    bool isBackgroundNotification = false;

    // ---- Assistant-specific fields ----
    struct AssistantContentBlock {
        enum Type { Text, Thinking, RedactedThinking, ToolUse };
        Type type = Text;
        String text;            // For Text blocks
        String thinking;        // For Thinking blocks
        String signature;       // For RedactedThinking blocks
        String toolUseId;       // For ToolUse blocks
        String toolName;
        String toolInput;       // JSON string
    };
    std::vector<AssistantContentBlock> assistantBlocks;
    bool isApiErrorMessage = false;

    // ---- System-specific fields ----
    SystemMessageSubtype systemSubtype = SystemMessageSubtype::Informational;
    String systemText;
    int snippedMessageCount = 0;     // For micro-compact boundaries
    String compactDirection;         // "above" or "below"

    // ---- Metadata ----
    enum class Origin { Human, System, SubAgent, Compaction, MCP, Unknown };
    Origin origin = Origin::Human;
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    int apiRound = 0;
    String parentAgentId;            // For sub-agent messages
    bool isMeta = false;             // Hidden from default view
    bool isVirtual = false;          // Synthetic (not from API)
    bool isVisibleInTranscriptOnly = false;
    bool isCompactSummary = false;
    bool hasContentAfter = false;    // Set by pipeline; drives past/present tense

    // ---- Convenience checks ----
    bool isCompactBoundary() const {
        return role == UIMessageRole::System &&
               (systemSubtype == SystemMessageSubtype::CompactBoundary ||
                systemSubtype == SystemMessageSubtype::MicroCompactBoundary);
    }
    bool isToolResultMessage() const {
        return role == UIMessageRole::User && userType == UserInputType::ToolResult;
    }
    bool hasThinkingContent() const {
        for (auto& b : assistantBlocks) {
            if (b.type == AssistantContentBlock::Thinking ||
                b.type == AssistantContentBlock::RedactedThinking) return true;
        }
        return false;
    }
    bool isToolUseRequest() const {
        if (role != UIMessageRole::Assistant) return false;
        for (auto& b : assistantBlocks) {
            if (b.type == AssistantContentBlock::ToolUse) return true;
        }
        return false;
    }
};

// ========== Message Pipeline types ==========

/// Lookup tables built by the normalization pipeline for O(1) rendering queries.
struct MessageLookups {
    // tool_use_id → tool_name
    std::unordered_map<String, String> toolUseIdToToolName;
    // Set of tool_use_ids that have received a result
    std::unordered_set<String> resolvedToolUseIds;
    // Set of tool_use_ids whose result is an error
    std::unordered_set<String> erroredToolUseIds;
    // Set of tool_use_ids still running
    std::unordered_set<String> inProgressToolUseIds;
    // tool_use_id → set of sibling tool_use_ids in same message
    std::unordered_map<String, std::unordered_set<String>> siblingToolUseIds;
    // tool_use_id → user message that contains the tool_result
    std::unordered_map<String, String> toolResultByToolUseId;  // → uuid
};

/// Accumulator used during collapseReadSearchGroups pass.
struct GroupAccumulator {
    /// High-level group kind — determines when to flush and start a new group.
    enum class GroupKind {
        None,             // Not yet assigned
        ExplorationGroup, // Read, Grep, Glob, LS (read-only exploration)
        BashGroup,        // Bash (non-search, state-modifying)
        WebGroup,         // WebSearch, WebFetch
    };

    enum Category {
        Search,       // Grep, Glob with patterns
        FileRead,     // Read
        FileList,     // LS / Glob without patterns
        Bash,         // Non-search bash commands
        WebSearch,    // WebSearch
        WebFetch,     // WebFetch
        MemorySearch, // Auto-managed memory reads
        MemoryWrite,  // Auto-managed memory writes
        MCP           // MCP tool calls
    };

    GroupKind kind = GroupKind::None;

    static GroupKind toGroupKind(Category cat) {
        switch (cat) {
            case Search:
            case FileRead:
            case FileList:     return GroupKind::ExplorationGroup;
            case Bash:         return GroupKind::BashGroup;
            case WebSearch:
            case WebFetch:     return GroupKind::WebGroup;
            default:           return GroupKind::None;
        }
    }

    std::vector<size_t> blockIndices;  // indices into the UIMessage vector

    int searchCount = 0;
    std::set<String> readFilePaths;    // deduplicated file paths
    int readOperationCount = 0;        // fallback when no file paths
    int listCount = 0;
    int bashCount = 0;
    std::map<String, String> bashCommands;  // toolUseId → command
    int webSearchCount = 0;
    int webFetchCount = 0;
    int memorySearchCount = 0;
    std::set<String> memoryReadFilePaths;
    int memoryWriteCount = 0;
    // MCP tracking
    std::map<String, int> mcpServerCounts;  // server_name → count
    int mcpTotalCount = 0;

    std::set<String> toolUseIds;       // all tool_use_ids in this group
    String latestDisplayHint;          // last file path, command, or pattern

    bool isEmpty() const {
        return searchCount == 0 && readFilePaths.empty() && readOperationCount == 0 &&
               listCount == 0 && bashCount == 0 && webSearchCount == 0 &&
               webFetchCount == 0 && memorySearchCount == 0 && memoryWriteCount == 0 &&
               mcpTotalCount == 0;
    }

    void reset() {
        kind = GroupKind::None;
        blockIndices.clear();
        searchCount = 0;
        readFilePaths.clear();
        readOperationCount = 0;
        listCount = 0;
        bashCount = 0;
        bashCommands.clear();
        webSearchCount = 0;
        webFetchCount = 0;
        memorySearchCount = 0;
        memoryReadFilePaths.clear();
        memoryWriteCount = 0;
        mcpServerCounts.clear();
        mcpTotalCount = 0;
        toolUseIds.clear();
        latestDisplayHint.clear();
    }

    /// Read count: prefer deduplicated file paths; fall back to operation count
    int readCount() const {
        return readFilePaths.empty() ? readOperationCount : static_cast<int>(readFilePaths.size());
    }
};

} // namespace claude
