#pragma once

#include "Types.hpp"
#include "TokenTracker.hpp"
#include "PromptTooLongException.hpp"
#include "claude/stream/TypedStreamEvent.hpp"
#include "StreamEvent.hpp"
#include "../permission/PermissionTypes.hpp"
#include "../tool/ToolContext.hpp"
#include "HookManager.hpp"
#include <functional>
#include <vector>
#include <memory>
#include <expected>
#include <condition_variable>
#include <atomic>

namespace claude {

// Forward declarations — full definitions only needed in AgentLoop.cpp
class ToolRegistry;
class ToolContext;
class RuleEngine;
class ContextInjector;
class McpClient;
class ApiClient;
class HookManager;

namespace compact {
class CompactService;
class AutoCompact;
class CompactWarningHook;
} // namespace compact

/// Agent loop — inspired by Java AgentLoop design
///
/// Core conversation management, supporting two modes:
/// - run() — blocking mode, waits for complete response
/// - runStreaming() — streaming mode, real-time per-token output
///
/// Streaming output uses Think-Act-Observe-Repeat (TAOR) loop:
/// - Think: model streams text + thinking
/// - Act: execute tool calls (read-only tools concurrent)
/// - Observe: tool results sent back in real time
/// - Repeat: append results to history, continue loop
class AgentLoop {
public:
    /// Maximum iterations per turn, prevents infinite loops
    static constexpr int DEFAULT_MAX_ITERATIONS = 50;

    /// max_output_tokens recovery max retry count
    static constexpr int MAX_OUTPUT_TOKENS_RECOVERY = 3;

    /// Escalated max_tokens for recovery (when default is too small)
    static constexpr int ESCALATED_MAX_TOKENS = 65536;

    /// Maximum reactive compact attempts on 413 errors
    static constexpr int MAX_REACTIVE_COMPACT_ATTEMPTS = 2;

    /// Result from a stop hook evaluation.
    /// If shouldContinue is true, the loop continues even though the model said end_turn.
    struct StopHookResult {
        bool shouldContinue = false;
        String reason;
    };

    /// Stop hook callback type.
    /// Called when the model stops with end_turn (not max_tokens).
    using OnStopHook = std::function<StopHookResult()>;

    // ========== Construction ==========

    AgentLoop(
        ApiClient& apiClient,
        ToolRegistry& tools,
        const String& systemPrompt
    );

    AgentLoop(
        ApiClient& apiClient,
        ToolRegistry& tools,
        const String& systemPrompt,
        TokenTracker& tokenTracker
    );

    /// Destructor must be defined in .cpp (Impl is incomplete type in header)
    ~AgentLoop();

    // Non-copyable, non-movable (holds references and unique_ptr)
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;
    AgentLoop(AgentLoop&&) = delete;
    AgentLoop& operator=(AgentLoop&&) = delete;

    // ========== Blocking mode ==========

    /// Execute a complete Agent loop for one user input (blocking)
    /// Waits for complete response before returning
    std::expected<String, String> run(const String& userInput);

    // ========== Streaming mode ==========

    /// Execute a streaming Agent loop for one user input
    /// Text is delivered per-token via onToken callback in real time
    std::expected<String, String> runStreaming(
        const String& userInput,
        OnToken onToken
    );

    // ========== Callback registration (inspired by Java functional design) ==========

    /// Set tool event callback
    void setOnToolEvent(OnToolEvent callback);

    /// Set permission confirmation callback
    void setOnPermissionRequest(
        std::function<PermissionChoice(const PermissionRequest&)> callback
    );

    /// Set stream start callback
    void setOnStreamStart(OnStreamStart callback);

    /// Set thinking content callback
    void setOnThinking(OnThinking callback);

    /// Set assistant message callback
    void setOnAssistantMessage(std::function<void(const String&)> callback);

    /// Set content block stop callback — fires on each content_block_stop
    /// Key to smooth output: notifies UI as each block completes,
    /// rather than waiting for the entire response
    void setOnContentBlockStop(std::function<void(const String& type, int index, const String& content)> callback);

    /// Set tool result streaming callback — fires as each tool completes
    /// Allows UI to display results progressively during tool execution
    void setOnToolResult(std::function<void(const String& toolName, const String& result, bool isError)> callback);

    /// Initialize AutoCompact (needs ApiClient and context window size)
    void initAutoCompact(int contextWindow);

    /// Set loop continue callback — fires when TAOR loop continues to next iteration
    /// Lets UI know the model is continuing to think/act
    void setOnLoopContinue(std::function<void(int iteration, int totalIterations)> callback);

    /// Set unified stream event callback — new interface replacing 5 independent callbacks
    /// When set, AgentLoop sends StreamEvent events through this callback
    /// Backward compatible: existing independent callbacks still work (as fallback)
    void setOnStreamEvent(std::function<void(const StreamEvent&)> callback);

    /// Set typed stream event callback — new 5-layer pipeline interface
    void setOnTypedEvent(std::function<void(TypedStreamEvent&&)> callback);

    /// Set stream tool event callback — new 5-layer pipeline interface
    void setOnStreamToolEvent(std::function<void(StreamToolEvent&&)> callback);

    /// Set the stop hook callback.
    /// When the model stops (end_turn), this hook runs and can force continuation.
    void setOnStopHook(OnStopHook callback);

    /// Set context compact warning callback — fires when token usage approaches context window limit
    /// level 1: 80% (warning), level 2: 93% (critical)
    void setOnCompactWarning(std::function<void(int level, long currentTokens, long maxTokens)> callback);

    // ========== Per-agent overrides ==========

    /// Set max iterations (overrides default 50). Used by sub-agents.
    void setMaxIterations(int maxIter);
    int getMaxIterations() const;

    /// Set temperature for API calls (-1 = use API default). Used by sub-agents.
    void setTemperature(double temp);
    double getTemperature() const;

    /// Set max_tokens override for API calls (-1 = use ApiClient default). Used by sub-agents.
    void setMaxTokensOverride(int maxTokens);
    int getMaxTokensOverride() const;

    /// Set per-task token budget (0 = unlimited). Loop stops when exceeded.
    void setTaskBudget(long budget);
    long getTaskBudget() const;
    long getTaskBudgetUsed() const;

    /// Set allowed tools filter — only these tools will be sent in API requests.
    /// Empty means all registered tools are available (default).
    void setAllowedTools(std::vector<String> tools);
    const std::vector<String>& getAllowedTools() const;

    /// Set disallowed tools — these tools will be excluded from API requests.
    void setDisallowedTools(std::vector<String> tools);
    const std::vector<String>& getDisallowedTools() const;

    /// Set pre-built system prompt blocks with cache_control markers.
    /// When set, buildApiRequest() serializes these blocks instead of
    /// the flat systemPrompt_ string, enabling proper prompt caching
    /// (global cache on static sections, org cache on the last block).
    void setSystemBlocks(std::vector<TextBlockParam> blocks);

    /// Check if system prompt blocks are available
    bool hasSystemBlocks() const;

    /// Set context injector for per-turn context injection.
    /// When set, buildContext() is called before each user turn to inject
    /// git status, CLAUDE.md, system reminders, skills, memories, and attachments.
    void setContextInjector(ContextInjector* injector);

    /// Refresh dynamic context (git status, etc.) before a new turn.
    /// Called by the main loop between turns.
    void refreshContext();

    // ========== Permission engine ==========

    void setPermissionEngine(RuleEngine* engine);
    RuleEngine* getPermissionEngine() const;

    // ========== Cognitive backend ==========

    /// Set cognitive backend MCP client and register cognitive tools
    void setCognitiveBackend(std::shared_ptr<McpClient> mcpClient);

    /// Get cognitive backend client
    std::shared_ptr<McpClient> getCognitiveBackend() const;

    /// Check if cognitive backend is enabled
    bool hasCognitiveBackend() const;

    // ========== Cancellation ==========

    /// Cancel the running agent loop and any in-flight API stream.
    /// Thread-safe: may be called from the UI thread or signal handler.
    void cancel();

    /// Check if cancel was requested
    bool isCancelled() const;

    /// Reset cancellation state for a new turn
    void resetCancel();

    // ========== State access ==========

    /// Thread-safety: caller must not hold the returned reference across
    /// mutation points (push_back, clear, replaceHistory). If concurrent
    /// access is possible, copy under historyMutex_ or use getMessageCount().
    const std::vector<Message>& getMessageHistory() const;

    TokenTracker& getTokenTracker();
    const TokenTracker& getTokenTracker() const;

    const String& getSystemPrompt() const;

    ApiClient& getApiClient();

    ToolContext& getToolContext();

    HookManager& getHookManager();

    // ========== History management ==========

    /// Reset history (preserves system prompt)
    void reset();

    /// Replace message history (used after context compaction)
    void replaceHistory(std::vector<Message> newHistory);

    /// Get message count
    size_t getMessageCount() const;

private:
    // ========== Core loop ==========

    /// Unified execution loop (TAOR: Think-Act-Observe-Repeat)
    std::expected<String, String> executeLoop(
        bool streaming,
        OnToken onToken
    );

    /// Enable or disable interleaved tool execution during streaming.
    /// When enabled, tool calls are dispatched at content_block_stop time
    /// instead of waiting for the entire stream to complete.
    void setInterleaveToolExecution(bool enable);
    bool isInterleaveToolExecution() const;

    /// Blocking iteration result
    struct IterationResult {
        Message message;
        Usage usage;
        String stopReason;  // "end_turn", "max_tokens", "tool_use", etc.
        // Interleaved tool results (already executed during streaming)
        std::vector<ToolResponse> interleavedToolResults;
        // Set when the API stream was aborted mid-flight.
        // Survives resetCancel() because it's per-result, not shared state.
        bool wasAborted = false;
    };
    IterationResult blockingIteration(const Json& prompt);

    /// Streaming iteration
    IterationResult streamingIteration(const Json& prompt, OnToken onToken);

    // ========== Tool execution ==========

    /// Execute tool call list (read-only tools concurrent)
    std::vector<ToolResponse> executeToolCalls(const std::vector<ToolCall>& calls);

    /// Execute single tool
    String executeTool(const ToolCall& call);

    /// Check if tool is read-only (can be executed concurrently)
    bool isToolReadOnly(const String& toolName) const;

    /// Micro-compact: clear expired tool result content
    /// Matches original TS microcompact behavior:
    /// - Tool results older than 60 minutes and not adjacent to current user message → replace with [Old tool result content cleared]
    /// - Preserve last N tool results from compaction
    void applyMicrocompact();

    /// Auto-compact: when token usage exceeds 93% of context window,
    /// use LLM to summarize conversation history, keeping last N messages
    /// Matches original TS auto-compact behavior
    bool applyAutoCompact();

    /// Reactive compact: attempt compact on 413 (prompt too long) errors.
    /// Returns true if compact succeeded and the caller should retry.
    /// @param tokenGap How many tokens over the limit (from 413 error body)
    bool attemptReactiveCompact(long tokenGap = 0);

    /// Generate synthetic error tool_results for any tool_use blocks
    /// in the last assistant message that lack matching tool_results.
    void addMissingToolResults();

    /// Insert or merge tool results immediately after the matching assistant
    /// message in messageHistory.  Used when the agent loop is cancelled after
    /// tool execution — the assistant message is already in history but the
    /// tool_result was not written yet.  This ensures messageHistory itself
    /// is a valid transcript without relying on P0's API-copy repair.
    ///
    /// Algorithm: scans backwards through history to find the assistant
    /// message whose tool_use IDs match expectedToolCallIds.  Collects
    /// existing late tool_results, moves them to immediately after the
    /// assistant, and fills in missing results from actualResults or
    /// synthetic error fallback.
    void insertOrMergeToolResultsAfterAssistant(
        const std::vector<ToolCall>& expected,
        std::vector<ToolResponse>& actual,
        const String& fallbackErrorText = "Interrupted"
    );

    /// Strip thinking/signature/redacted_thinking from message history.
    /// Called before retrying with a fallback model to prevent 400 errors.
    void stripThinkingFromHistory();

    // ========== Helper methods ==========

    /// Build API request
    Json buildApiRequest();

    /// Inject context into the user message via ContextInjector
    void injectContext(const String& userInput);

    /// Extract thinking content
    void extractThinkingContent(const Json& response);

    /// Notify tool event
    void notifyToolEvent(ToolEventPhase phase, const String& name,
                         const String& args, const String& result = {});

    /// Emit stream event — if onStreamEvent_ is set, use it; otherwise fall back to individual callbacks
    void emitStreamEvent(StreamEvent event);

    /// State for progressive tool result yielding — tracks active tool count
    /// with condition variable for non-blocking wait
    struct ToolExecutionState {
        std::atomic<int> activeCount{0};
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> cancelled{false};
    };

    /// Wait for remaining in-flight tools to complete, using condition variable.
    /// Returns when all tools finish or when the state is cancelled.
    void waitForRemaining(ToolExecutionState& state);

    /// Discard all in-flight streaming tools — emit synthetic error events
    /// for any pending interleaved tools. Called when a streaming fallback occurs.
    void discardStreamingTools();

private:
    /// pImpl — all private state lives in Impl, defined in AgentLoop.cpp
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace claude

// ============================================================================
// P2: Free functions for history-layer tool_result repair.
// Exposed for unit testing; AgentLoop methods delegate to these.
// ============================================================================

namespace claude {

/// Insert or merge tool results into a message history vector at the correct
/// position — immediately after the matching assistant tool_use message,
/// before any subsequently-inserted user/system messages.
///
/// Handles race conditions: if a new prompt created a different assistant
/// after our assistant, this function still finds the correct one by ID.
void insertToolResultsIntoHistory(
    std::vector<Message>& history,
    const std::vector<ToolCall>& expected,
    std::vector<ToolResponse>& actual,
    const String& fallbackErrorText
);

/// Validate history after repair: every assistant with tool_use must have
/// an immediately-following tool_result message covering all tool_use IDs.
bool validateHistoryAfterRepair(const std::vector<Message>& history);

} // namespace claude
