#include <claude/core/AgentLoopImpl.hpp>
#include <claude/tool/ResultTruncation.hpp>
#include <spdlog/spdlog.h>

namespace claude {

// ============================================================================
// Tool execution
// ============================================================================

std::vector<ToolResponse> AgentLoop::executeToolCalls(const std::vector<ToolCall>& calls) {
    // ========== Initialize StreamingToolExecutor (lazy) ==========
    if (!impl_->toolExecutor) {
        impl_->toolExecutor.emplace(impl_->tools, impl_->toolContext, impl_->hookManager, impl_->permissionEngine);
    }

    auto& executor = *impl_->toolExecutor;

    // Wire callbacks from AgentLoop into the executor — copy under lock
    auto permCb = [&] {
        std::lock_guard lock(impl_->callbackMutex);
        return impl_->onPermissionRequest;
    }();
    executor.setOnPermissionRequest(permCb);
    executor.setTranscript(&impl_->messageHistory);

    // Store parent permission callback in ToolContext for sub-agent delegation
    impl_->toolContext.set("parentPermissionCallback", permCb);

    // Track active tool count for progressive yielding
    ToolExecutionState execState;
    execState.activeCount.store(static_cast<int>(calls.size()), std::memory_order_relaxed);

    executor.setOnToolStart([this](const String& toolName, const String& description, const String& toolId) {
        notifyToolEvent(ToolEventPhase::Start, toolName, description);
        // New 5-layer pipeline: emit StreamToolEvent for tool start
        {
            auto cb = [&] {
                std::lock_guard lock(impl_->callbackMutex);
                return impl_->onStreamToolEvent;
            }();
            if (cb) {
                cb(StreamToolEvent{
                    .type = StreamToolEventType::Started,
                    .toolCallId = toolId,
                    .toolName = toolName,
                    .activity = "Running…"
                });
            }
        }
    });

    executor.setOnToolComplete([this](const String& toolName, bool success) {
        // Tool end notification is handled via onToolResultReady below,
        // but we fire the general event here for completeness
        if (!success) {
            notifyToolEvent(ToolEventPhase::End, toolName, "", "Error");
        }
    });

    executor.setOnToolChunk([this](const String& chunk) {
        StreamEvent chunkEvent;
        chunkEvent.type = StreamEvent::Type::ToolChunkReady;
        chunkEvent.text = chunk;
        emitStreamEvent(std::move(chunkEvent));
    });

    // Progressive yielding: fire StreamToolEvent + StreamEvent immediately
    // upon each tool's completion via the onToolResultReady callback.
    // This replaces the batch loop that used to iterate execResults after
    // executor.execute() returned.
    executor.setOnToolResultReady([this, &execState](const ToolExecutionResult& er) {
        bool isError = er.response.isError;

        // Emit tool result — goes through emitStreamEvent which dispatches
        // to onStreamEvent if set, or falls back to onToolResult.
        StreamEvent toolResultEvent;
        toolResultEvent.type = StreamEvent::Type::ToolResultReady;
        toolResultEvent.toolId = er.response.callId;
        toolResultEvent.toolName = er.response.toolName;
        toolResultEvent.toolResult = er.response.content;
        toolResultEvent.toolIsError = isError;
        toolResultEvent.toolIsCancelled = er.response.isCancelled;
        toolResultEvent.toolIsRejected = er.response.isRejected;
        emitStreamEvent(std::move(toolResultEvent));

        // New 5-layer pipeline: emit StreamToolEvent for tool completion
        {
            auto streamToolCb = [&] {
                std::lock_guard lock(impl_->callbackMutex);
                return impl_->onStreamToolEvent;
            }();
            if (streamToolCb) {
                StreamToolEventType eventType = isError ? StreamToolEventType::Error
                    : er.response.isCancelled ? StreamToolEventType::Cancelled
                    : er.response.isRejected ? StreamToolEventType::Rejected
                    : StreamToolEventType::Completed;
                streamToolCb(StreamToolEvent{
                    .type = eventType,
                    .toolCallId = er.response.callId,
                    .toolName = er.response.toolName,
                    .summary = er.displaySummary,
                    .durationMs = static_cast<double>(er.duration.count())
                });
            }
        }

        // Fire end notification
        notifyToolEvent(ToolEventPhase::End, er.response.toolName, "", er.response.content);

        // Decrement active count and notify waiters
        execState.activeCount.fetch_sub(1, std::memory_order_relaxed);
        execState.cv.notify_one();
    });

    // Execute with ParallelReadOnly ordering (read-only tools concurrent, write tools sequential)
    auto execResults = executor.execute(calls);

    // Wait for any remaining in-flight tools (should already be complete since
    // executor.execute() joins all threads, but this is defensive)
    waitForRemaining(execState);

    // Convert ToolExecutionResult -> ToolResponse
    // StreamToolEvent emissions already happened via onToolResultReady callback
    std::vector<ToolResponse> responses;
    responses.reserve(execResults.size());

    for (auto& er : execResults) {
        responses.push_back(std::move(er.response));
    }

    return responses;
}

String AgentLoop::executeTool(const ToolCall& call) {
    // Find tool
    Tool* tool = impl_->tools.findByName(call.name);
    if (!tool) {
        return "Error: Unknown tool '" + call.name + "'";
    }

    // Parse arguments
    Json input;
    try {
        input = Json::parse(call.arguments);
    } catch (const Json::parse_error& e) {
        return "Error: Invalid JSON arguments: " + String(e.what());
    }

    // Notify start
    notifyToolEvent(ToolEventPhase::Start, call.name, call.arguments);

    // ========== PreToolUse Hook ==========
    HookContext preCtx;
    preCtx.toolName = call.name;
    preCtx.input = input;
    if (impl_->hookManager.execute(HookType::PreToolUse, preCtx) .shouldAbort()) {
        notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "Hook blocked");
        return "Error: Tool blocked by pre-tool hook";
    }

    // Permission check (consider hook permission override)
    auto permOverride = preCtx.getPermissionOverride();
    if (permOverride && *permOverride) {
        // Hook force-allow: skip permission check
        spdlog::debug("Permission overridden by hook: allowing {}", call.name);
    } else if (permOverride && !*permOverride) {
        // Hook force-deny
        notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "Hook denied");
        return "Permission denied";
    } else if (impl_->permissionEngine) {
        auto decision = impl_->permissionEngine->evaluate(call.name, input, tool->isReadOnly(), impl_->messageHistory);

        if (decision.isDenied()) {
            notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "Permission denied");
            return "Permission denied: " + decision.reason;
        }

        if (decision.needsAsk()) {
            auto permCb = [&] {
                std::lock_guard lock(impl_->callbackMutex);
                return impl_->onPermissionRequest;
            }();
            if (permCb) {
                PermissionRequest req{call.name, call.arguments, tool->activityDescription(input)};
                auto choice = permCb(req);

                if (choice == PermissionChoice::DenyOnce || choice == PermissionChoice::AlwaysDeny) {
                    notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "User denied");
                    return "Permission denied";
                }

                // Apply choice
                String command = input.is_object() ? input.value("command", input.value("file_path", "")) : "";
                impl_->permissionEngine->applyChoice(choice, call.name, command);
            }
        }
    }

    // Execute
    String result;
    try {
        result = tool->execute(input, impl_->toolContext);
    } catch (const std::exception& e) {
        result = "Error: " + String(e.what());
    }

    // ========== Tool result budget truncation ==========
    if (result.size() > tool->maxResultSizeChars()) {
        spdlog::debug("Tool [{}] result size {} exceeds budget {}, truncating",
            call.name, result.size(), tool->maxResultSizeChars());
        result = ResultTruncation::truncate(result, tool->maxResultSizeChars(), call.name);
    }

    // ========== PostToolUse Hook ==========
    HookContext postCtx;
    postCtx.toolName = call.name;
    postCtx.input = input;
    postCtx.result = result;
    if (impl_->hookManager.execute(HookType::PostToolUse, postCtx) .shouldAbort()) {
        return "Error: Tool execution blocked by post-tool hook";
    }
    // Hook may have modified the result
    if (postCtx.result && *postCtx.result != result) {
        result = *postCtx.result;
    }

    // Notify end (note: for concurrent tools, End notification is handled in executeToolCalls)
    // For sequential tools, notify directly here
    if (!isToolReadOnly(call.name)) {
        notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, result);
    }

    return result;
}

bool AgentLoop::isToolReadOnly(const String& toolName) const {
    Tool* tool = impl_->tools.findByName(toolName);
    if (!tool) return false;
    return tool->isReadOnly();
}

void AgentLoop::addMissingToolResults() {
    std::lock_guard lock(impl_->historyMutex);
    if (impl_->messageHistory.empty()) return;

    // Find the last assistant message with tool calls
    auto it = impl_->messageHistory.rbegin();
    for (; it != impl_->messageHistory.rend(); ++it) {
        if (it->role == MessageRole::Assistant && it->hasToolCalls()) {
            break;
        }
    }
    if (it == impl_->messageHistory.rend()) return;

    // Check if the message after this assistant message is a tool_result
    auto assistantIdx = std::distance(impl_->messageHistory.begin(), it.base()) - 1;
    bool hasToolResult = false;
    if (assistantIdx + 1 < static_cast<long>(impl_->messageHistory.size())) {
        hasToolResult = (impl_->messageHistory[assistantIdx + 1].role == MessageRole::ToolResult);
    }

    if (!hasToolResult) {
        // Generate synthetic error results for each tool call
        std::vector<ToolResponse> errorResults;
        for (const auto& tc : it->toolCalls) {
            ToolResponse resp;
            resp.callId = tc.id;
            resp.toolName = tc.name;
            resp.content = "[Error: API call failed before tool execution completed]";
            resp.isError = true;
            errorResults.push_back(std::move(resp));
        }
        if (!errorResults.empty()) {
            impl_->messageHistory.push_back(Message::toolResult(std::move(errorResults)));
            spdlog::debug("Added {} synthetic error tool_results for unmatched tool_uses", errorResults.size());
        }
    }
}

// ============================================================================
// Progressive tool result yielding
// ============================================================================

void AgentLoop::waitForRemaining(ToolExecutionState& state) {
    std::unique_lock lock(state.mutex);
    state.cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return state.activeCount.load(std::memory_order_acquire) == 0
            || state.cancelled.load(std::memory_order_acquire);
    });
}

void AgentLoop::discardStreamingTools() {
    // Generate synthetic error events for all pending interleaved tools.
    // Called when a streaming fallback occurs — any tools that were dispatched
    // via the interleaved enqueue API but haven't completed yet need to be
    // cancelled and reported to the UI.
    if (!impl_->toolExecutor) return;

    auto& executor = *impl_->toolExecutor;

    // Cancel the executor — this marks all pending/running tools as cancelled
    executor.cancel();

    // If there are pending enqueued tools, collect them (which joins their
    // futures and returns cancelled results) and emit error events
    size_t pendingBefore = executor.pendingCount();
    if (executor.hasPending()) {
        auto pendingResults = executor.collectResults();
        auto streamToolCb = [&] {
            std::lock_guard lock(impl_->callbackMutex);
            return impl_->onStreamToolEvent;
        }();

        for (const auto& er : pendingResults) {
            // Emit StreamToolEvent for the cancelled tool
            if (streamToolCb) {
                streamToolCb(StreamToolEvent{
                    .type = StreamToolEventType::Cancelled,
                    .toolCallId = er.response.callId,
                    .toolName = er.response.toolName,
                    .summary = ToolResultSummary::dim("Discarded due to model fallback"),
                    .durationMs = static_cast<double>(er.duration.count())
                });
            }

            // Also emit via old StreamEvent path for backward compat
            StreamEvent toolResultEvent;
            toolResultEvent.type = StreamEvent::Type::ToolResultReady;
            toolResultEvent.toolId = er.response.callId;
            toolResultEvent.toolName = er.response.toolName;
            toolResultEvent.toolResult = er.response.content;
            toolResultEvent.toolIsError = true;
            toolResultEvent.toolIsCancelled = true;
            emitStreamEvent(std::move(toolResultEvent));
        }
    }

    spdlog::info("Discarded streaming tools due to fallback ({} pending)", pendingBefore);
}

} // namespace claude
