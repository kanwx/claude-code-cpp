#pragma once

#include "../tool/ToolRegistry.hpp"
#include "../tool/ToolContext.hpp"
#include "../tool/ResultTruncation.hpp"
#include "../permission/RuleEngine.hpp"
#include "HookManager.hpp"
#include "Types.hpp"
#include <chrono>
#include <atomic>
#include <functional>
#include <vector>
#include <future>
#include <mutex>

namespace claude {

/// Tool execution ordering mode
enum class ToolExecutionOrder {
    Sequential,       // Execute tools one at a time
    ParallelReadOnly, // Read-only tools in parallel, write tools sequential
    FullParallel      // All tools in parallel (unsafe)
};

/// Tool execution result with metadata
struct ToolExecutionResult {
    ToolResponse response;
    std::chrono::milliseconds duration{0};
    bool wasParallel = false;
    int executionOrder = 0;  // 0-based order in which it was dispatched
    ToolResultSummary displaySummary;  // structured summary for UI rendering
};

/// Streaming tool executor -- concurrent tool execution with ordering guarantees
///
/// Design principles from TS original:
/// 1. Read-only tools (isReadOnly()=true) execute concurrently
/// 2. Write/destructive tools execute sequentially
/// 3. Tool results are returned in the same order as the tool calls
/// 4. PreToolUse/PostToolUse hooks are called around each tool
/// 5. Permission checks happen before execution
/// 6. Tool execution can be cancelled mid-flight
class StreamingToolExecutor {
public:
    StreamingToolExecutor(
        ToolRegistry& tools,
        ToolContext& context,
        HookManager& hooks,
        RuleEngine* permissionEngine = nullptr
    );

    /// Execute a list of tool calls with optimal parallelism
    /// Returns results in the SAME ORDER as input toolCalls
    std::vector<ToolExecutionResult> execute(const std::vector<ToolCall>& toolCalls);

    /// Execute with a specific ordering mode
    std::vector<ToolExecutionResult> executeWithOrder(
        const std::vector<ToolCall>& toolCalls,
        ToolExecutionOrder order
    );

    /// Cancel all running/pending tool executions
    void cancel();

    /// Check if currently executing
    bool isExecuting() const;

    /// Get number of currently running tools
    int activeToolCount() const;

    // Configuration
    void setMaxParallelism(int max) { maxParallelism_ = max; }
    int getMaxParallelism() const { return maxParallelism_; }

    // Permission callback
    void setOnPermissionRequest(
        std::function<PermissionChoice(const PermissionRequest&)> callback
    ) { onPermissionRequest_ = std::move(callback); }

    // Progress callbacks
    void setOnToolStart(std::function<void(const String& toolName, const String& description, const String& toolId)> cb) {
        onToolStart_ = std::move(cb);
    }
    void setOnToolComplete(std::function<void(const String& toolName, bool success)> cb) {
        onToolComplete_ = std::move(cb);
    }
    void setOnToolChunk(std::function<void(const String& chunk)> cb) {
        onToolChunk_ = std::move(cb);
    }

    /// Set per-tool-result-ready callback — fires immediately upon each tool's
    /// completion with full ToolExecutionResult, enabling progressive yielding.
    /// Unlike onToolComplete (which only gets toolName + success), this callback
    /// receives the complete result including callId, summary, duration, and status.
    void setOnToolResultReady(std::function<void(const ToolExecutionResult&)> cb) {
        onToolResultReady_ = std::move(cb);
    }

    /// Set the conversation transcript for permission evaluation (YOLO classifier, etc.)
    void setTranscript(const std::vector<Message>* transcript) { transcript_ = transcript; }

    // ========== Incremental enqueue API (for streaming interleaving) ==========

    /// Enqueue a single tool call for incremental execution.
    /// Dispatches in a separate thread via std::async. The future is stored
    /// internally and collected later via collectResults().
    void enqueue(ToolCall call, int index);

    /// Collect all pending results from previously enqueued tool calls.
    /// Joins all futures and returns results in original index order.
    /// Applies aggregate truncation across all collected results.
    std::vector<ToolExecutionResult> collectResults();

    /// Check if there are pending (in-flight) tool executions
    bool hasPending() const { return !pendingFutures_.empty(); }

    /// Get the number of pending tool executions
    size_t pendingCount() const { return pendingFutures_.size(); }

private:
    ToolRegistry& tools_;
    ToolContext& context_;
    HookManager& hooks_;
    RuleEngine* permissionEngine_;

    int maxParallelism_ = 4;
    std::atomic<bool> cancelled_{false};
    std::atomic<int> activeCount_{0};
    std::atomic<bool> executing_{false};

    /// Monotonic generation counter — incremented on cancel() and on each
    /// new execute() call.  Used by executeSingle to detect stale executions
    /// from a detached thread.  After an old thread's tool.sleep() returns,
    /// the generation mismatch prevents it from touching callbacks that now
    /// belong to a newer execute() call.
    std::atomic<uint64_t> generation_{0};

    /// Protects permission engine evaluation + applyChoice (not thread-safe internally)
    std::mutex permissionMutex_;

    const std::vector<Message>* transcript_ = nullptr;

    std::function<PermissionChoice(const PermissionRequest&)> onPermissionRequest_;
    std::function<void(const String&, const String&, const String&)> onToolStart_;
    std::function<void(const String&, bool)> onToolComplete_;
    std::function<void(const String&)> onToolChunk_;
    std::function<void(const ToolExecutionResult&)> onToolResultReady_;

    /// Pending futures from enqueued tool calls (for streaming interleaving)
    struct PendingFuture {
        int index;
        std::future<ToolExecutionResult> future;
    };
    std::vector<PendingFuture> pendingFutures_;

    /// Classify a tool call as parallel-safe or sequential
    bool isParallelSafe(const ToolCall& call) const;

    /// Execute a single tool call (with hooks, permissions, error handling)
    ToolExecutionResult executeSingle(const ToolCall& call, int order, bool parallel);

    /// Check permissions for a tool call (caller must hold permissionMutex_)
    PermissionDecision checkPermissions(const String& toolName, const Json& input, bool isReadOnly);

    /// Run PreToolUse hooks, populating ctx for permission override inspection
    HookResult runPreToolUseHooks(const String& toolName, const Json& input, HookContext& ctx);

    /// Run PostToolUse hooks, allowing result modification via ctx
    void runPostToolUseHooks(const String& toolName, const Json& input, const String& result, HookContext& ctx);

    /// Execute a batch of parallel-safe tool calls
    std::vector<ToolExecutionResult> executeParallel(
        const std::vector<std::pair<ToolCall, int>>& calls
    );

    /// Apply aggregate budget truncation across all results
    void applyAggregateTruncation(std::vector<ToolExecutionResult>& results);
};

} // namespace claude
