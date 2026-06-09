#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace claude {

using String = std::string;   // standalone inclusion
using Json = nlohmann::json;  // standalone inclusion

// ========== Enums ==========

/// API Provider enum (matching original TS)
enum class APIProvider {
    FirstParty,  // Anthropic official
    Bedrock,     // AWS Bedrock
    Vertex,      // GCP Vertex AI
    Foundry      // Azure Foundry
};

/// Provider to string
inline String providerToString(APIProvider p) {
    switch (p) {
        case APIProvider::FirstParty: return "firstParty";
        case APIProvider::Bedrock: return "bedrock";
        case APIProvider::Vertex: return "vertex";
        case APIProvider::Foundry: return "foundry";
    }
    return "firstParty";
}

/// Provider name for API client
inline String providerToClientName(APIProvider p) {
    switch (p) {
        case APIProvider::FirstParty: return "anthropic";
        case APIProvider::Bedrock: return "bedrock";
        case APIProvider::Vertex: return "vertex";
        case APIProvider::Foundry: return "foundry";
    }
    return "anthropic";
}

/// Cache scope
enum class CacheScope {
    Global,  // Global cache (first-party only)
    Org,     // Organization-level cache
    None     // No cache
};

/// Message role
enum class MessageRole {
    System,
    User,
    Assistant,
    ToolResult
};

/// Stream chunk type
enum class StreamChunkType {
    MessageStart,
    ContentBlockStart,
    ContentBlockDelta,
    ContentBlockStop,
    MessageDelta,
    MessageStop,
    Error
};

/// Tool event phase
enum class ToolEventPhase {
    Start,  // Tool starts execution
    End     // Tool execution ended
};

/// Tool progress state for streaming display
enum class ToolProgress {
    None,
    Running,        // "Running…"
    Waiting,        // "  ⎿  Waiting…"
    Permission,     // "Waiting for permission…"
    Classifier,     // "Auto classifier checking…"
};

// ========== Structs ==========

/// Cache control
struct CacheControl {
    String type = "ephemeral";
    std::optional<String> ttl;        // "5m" or "1h"
    std::optional<CacheScope> scope;  // cache scope

    Json toJson() const {
        Json j = {{"type", type}};
        if (ttl) j["ttl"] = *ttl;
        if (scope && *scope == CacheScope::Global) {
            j["scope"] = "global";
        }
        return j;
    }
};

/// Text block parameter (for system prompt)
struct TextBlockParam {
    String type = "text";
    String text;
    std::optional<CacheControl> cache_control;

    Json toJson() const {
        Json j = {{"type", type}, {"text", text}};
        if (cache_control) {
            j["cache_control"] = cache_control->toJson();
        }
        return j;
    }
};

/// Tool call request
struct ToolCall {
    String id;           // Call ID
    String name;         // Tool name
    String arguments;    // JSON parameter string
};

/// Tool response
struct ToolResponse {
    String callId;       // Corresponding call ID
    String toolName;     // Tool name
    String content;      // Response content
    bool isError = false;
    bool isCancelled = false;
    bool isRejected = false;
};

/// Structured tool result summary for UI rendering
struct ToolResultSummary {
    String primaryText;     // "Read 42 lines", "Found 5 files", "Done"
    bool primaryBold = false;
    String secondaryText;   // "across 3 files", "to path/file"
    String expandHint;      // "[Ctrl+O to expand]" or ""
    String errorText;       // for error states
    bool isDim = false;
    bool isError = false;

    static ToolResultSummary success(const String& primary, bool bold = false,
                                      const String& secondary = "",
                                      const String& expand = "") {
        return {primary, bold, secondary, expand, "", false, false};
    }
    static ToolResultSummary dim(const String& primary) {
        return {primary, false, "", "", "", true, false};
    }
    static ToolResultSummary error(const String& text) {
        return {"", false, "", "", text, false, true};
    }

    /// Check if this summary has any renderable content
    bool empty() const {
        return primaryText.empty() && errorText.empty();
    }
};

/// Data for rendering a tool use/result pair (used by grouped rendering)
struct ToolUseRenderData {
    String toolUseId;
    String toolName;
    String arguments;     // JSON string
    String result;
    bool isError = false;
    bool isCancelled = false;
    bool isRejected = false;
    bool isInProgress = false;
    ToolResultSummary displaySummary;  // structured summary for collapsed rendering
};

/// Message
struct Message {
    MessageRole role;
    String content;
    std::vector<ToolCall> toolCalls;
    std::vector<ToolResponse> toolResults;

    // Extended Thinking (Anthropic)
    std::optional<String> thinking;    // Thinking content
    std::optional<String> signature;   // Signature (for verification)
    // Redacted thinking blocks — must be included verbatim in subsequent API requests.
    // Each entry is a JSON object: {"type":"redacted_thinking","data":"..."}
    std::vector<Json> redactedThinking;

    // Metadata
    std::map<String, String> metadata;

    // Timestamp (for microcompact time judgment)
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

    // API round this message belongs to (0-based, set by AgentLoop)
    int apiRound = 0;

    // Helper methods
    static Message system(String content) {
        return {MessageRole::System, std::move(content)};
    }

    static Message user(String content) {
        return {MessageRole::User, std::move(content)};
    }

    static Message assistant(String content, std::vector<ToolCall> calls = {}) {
        return {MessageRole::Assistant, std::move(content), std::move(calls)};
    }

    static Message toolResult(std::vector<ToolResponse> results) {
        return {MessageRole::ToolResult, {}, {}, std::move(results)};
    }

    bool hasToolCalls() const { return !toolCalls.empty(); }
    bool isToolResult() const { return role == MessageRole::ToolResult; }
    bool hasThinking() const { return thinking.has_value() && !thinking->empty(); }
};

/// Usage statistics
struct Usage {
    long promptTokens = 0;
    long completionTokens = 0;
    long totalTokens = 0;

    // Cache tokens (Anthropic)
    long cacheCreationTokens = 0;
    long cacheReadTokens = 0;
    long cacheDeletedInputTokens = 0;

    // Cache breakdown (from cache_creation breakdown in streaming)
    long cacheEphemeral5m = 0;
    long cacheEphemeral1h = 0;

    // Stream idle watchdog
    int stallCount = 0;
    double totalStallTimeMs = 0.0;

    Usage& operator+=(const Usage& other) {
        promptTokens += other.promptTokens;
        completionTokens += other.completionTokens;
        totalTokens += other.totalTokens;
        cacheCreationTokens += other.cacheCreationTokens;
        cacheReadTokens += other.cacheReadTokens;
        cacheDeletedInputTokens += other.cacheDeletedInputTokens;
        cacheEphemeral5m += other.cacheEphemeral5m;
        cacheEphemeral1h += other.cacheEphemeral1h;
        stallCount += other.stallCount;
        totalStallTimeMs += other.totalStallTimeMs;
        return *this;
    }

    // Effective input tokens (minus cache)
    long effectiveInputTokens() const {
        return promptTokens - cacheReadTokens;
    }
};

/// Stream response chunk
struct StreamChunk {
    StreamChunkType type;
    String content;      // Text content
    ToolCall toolCall;   // Tool call (if any)
    Usage usage;         // Usage (usually at end)
    String error;        // Error message
};

/// Tool event
struct ToolEvent {
    ToolEventPhase phase;
    String toolName;
    String arguments;
    String result;
    String toolId;                     // ID for pairing tool_use with tool_result
    bool isError = false;              // Whether the result is an error
    std::optional<String> error;
};

/// Stream tool event type (for new 5-layer pipeline)
enum class StreamToolEventType {
    Queued,
    Started,
    Progress,
    Completed,
    Error,
    Rejected,
    Cancelled,
};

/// Stream tool event (for new 5-layer pipeline)
struct StreamToolEvent {
    StreamToolEventType type = StreamToolEventType::Queued;
    String toolCallId;
    String toolName;
    String activity;
    ToolResultSummary summary;
    String rawResultPath;
    bool isParallel = false;
    double durationMs = 0;
};

/// Model settings
struct ModelConfig {
    String provider = "openai";       // openai | anthropic
    String model = "gpt-4o";          // Model name
    String baseUrl;                   // API address
    String apiKey;                    // API key
    int maxTokens = 4096;             // Max output tokens
    double temperature = 1.0;         // Temperature

    // Default configs
    static ModelConfig openai(String apiKey) {
        return {
            .provider = "openai",
            .model = "gpt-4o",
            .baseUrl = "https://api.openai.com/v1",
            .apiKey = std::move(apiKey)
        };
    }

    static ModelConfig anthropic(String apiKey) {
        return {
            .provider = "anthropic",
            .model = "claude-sonnet-4-20250514",
            .baseUrl = "https://api.anthropic.com/v1",
            .apiKey = std::move(apiKey),
            .maxTokens = 8192
        };
    }
};

// ========== Cost calculation ==========

/// Price table (per million tokens)
struct Pricing {
    double inputPrice = 0.0;
    double outputPrice = 0.0;

    double calculateCost(long inputTokens, long outputTokens) const {
        return (inputTokens * inputPrice + outputTokens * outputPrice) / 1'000'000.0;
    }
};

// Common model prices
inline const std::map<String, Pricing> MODEL_PRICING = {
    {"gpt-4o", {2.50, 10.00}},
    {"gpt-4o-mini", {0.15, 0.60}},
    {"claude-sonnet-4-20250514", {3.00, 15.00}},
    {"claude-opus-4-20250514", {15.00, 75.00}},
};

} // namespace claude
