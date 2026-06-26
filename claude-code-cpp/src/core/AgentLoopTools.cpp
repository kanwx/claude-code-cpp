#include <claude/core/AgentLoopImpl.hpp>
#include <claude/tool/ResultTruncation.hpp>
#include <spdlog/spdlog.h>
#include <set>

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
// P2: Ordered history-layer tool_result insert/merge
// ============================================================================

void insertToolResultsIntoHistory(
    std::vector<Message>& history,
    const std::vector<ToolCall>& expected,
    std::vector<ToolResponse>& actual,
    const String& fallbackErrorText
) {
    if (expected.empty()) return;

    // Build expected ID set
    std::set<String> expectedIds;
    for (auto& tc : expected) {
        expectedIds.insert(tc.id);
    }

    // === Step 1: Find the matching assistant message ===
    // Scan backwards through history to find the assistant message whose
    // tool_use IDs match the expected set.  Must not stop at the last
    // assistant — a newer prompt may have inserted a different assistant
    // after the one we're looking for.
    int assistantIdx = -1;
    for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::Assistant) continue;
        if (!msg.hasToolCalls()) continue;

        // Check if this assistant's tool_use IDs are a subset of expectedIds
        bool allMatch = true;
        for (auto& tc : msg.toolCalls) {
            if (!expectedIds.count(tc.id)) { allMatch = false; break; }
        }
        if (allMatch) {
            assistantIdx = i;
            break;
        }
    }

    if (assistantIdx < 0) {
        spdlog::debug("insertOrMergeToolResults: no matching assistant found for {} tool_use IDs",
                      expectedIds.size());
        return;
    }

    // === Step 2: Collect existing late tool_results for these IDs ===
    // These are tool_result messages after the assistant that belong to
    // our tool_use IDs.  We'll remove them from their current positions
    // and merge them into the result we place after the assistant.
    std::map<String, ToolResponse> existingResults;
    std::vector<size_t> indicesToRemove;  // right-to-left for safe removal

    for (size_t i = assistantIdx + 1; i < history.size(); ++i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::ToolResult) continue;
        for (auto& tr : msg.toolResults) {
            if (expectedIds.count(tr.callId)) {
                // Deduplicate: keep only the first occurrence
                if (!existingResults.count(tr.callId)) {
                    existingResults[tr.callId] = tr;
                }
            }
        }
        // If ALL tool_results in this message belong to our IDs, mark for removal
        bool allBelong = true;
        for (auto& tr : msg.toolResults) {
            if (!expectedIds.count(tr.callId)) { allBelong = false; break; }
        }
        if (allBelong && !msg.toolResults.empty()) {
            indicesToRemove.push_back(i);
        }
    }

    // Remove marked tool_result messages (right-to-left for index stability)
    for (auto it = indicesToRemove.rbegin(); it != indicesToRemove.rend(); ++it) {
        history.erase(history.begin() + static_cast<long>(*it));
    }

    // === Step 3: Build deduplicated result list ===
    // Priority: actual (fresh) > existing (late/stale) > synthetic (error)
    std::vector<ToolResponse> results;
    std::set<String> seenIds;

    // Actual results from executeToolCalls (highest priority)
    for (auto& ar : actual) {
        if (!expectedIds.count(ar.callId)) continue;
        if (seenIds.count(ar.callId)) continue;
        seenIds.insert(ar.callId);  // must capture before std::move
        results.push_back(std::move(ar));
    }

    // Existing late results (lower priority, fill gaps)
    for (auto& tc : expected) {
        if (seenIds.count(tc.id)) continue;
        auto it = existingResults.find(tc.id);
        if (it != existingResults.end()) {
            results.push_back(it->second);
            seenIds.insert(tc.id);
        }
    }

    // Synthesize error results for remaining missing IDs
    for (auto& tc : expected) {
        if (seenIds.count(tc.id)) continue;
        ToolResponse synth;
        synth.callId = tc.id;
        synth.toolName = tc.name;
        synth.content = fallbackErrorText;
        synth.isError = true;
        synth.isCancelled = true;
        results.push_back(std::move(synth));
        seenIds.insert(tc.id);
    }

    // === Step 4: Insert at the correct position ===
    // Check what immediately follows the assistant
    bool hasExistingToolResult = false;
    size_t insertPos = static_cast<size_t>(assistantIdx) + 1;
    if (insertPos < history.size() && history[insertPos].role == MessageRole::ToolResult) {
        // Replace the existing tool_result message at this position
        hasExistingToolResult = true;
    }

    auto toolMsg = Message::toolResult(std::move(results));
    toolMsg.apiRound = history[assistantIdx].apiRound;

    if (hasExistingToolResult) {
        // Merge: keep any non-our results in the existing tool_result message
        auto& existingMsg = history[insertPos];
        for (auto& tr : existingMsg.toolResults) {
            if (!expectedIds.count(tr.callId)) {
                toolMsg.toolResults.push_back(std::move(tr));
            }
        }
        history[insertPos] = std::move(toolMsg);
    } else {
        // Insert: place tool_result immediately after assistant,
        // before any user/system text that was already inserted
        history.insert(history.begin() + static_cast<long>(insertPos),
                       std::move(toolMsg));
    }

    spdlog::debug("insertOrMergeToolResults: assistantIdx={}, insertPos={}, "
                  "results={}, existingLate={}, synthetic={}",
                  assistantIdx, insertPos, actual.size(),
                  existingResults.size(),
                  seenIds.size() - existingResults.size() - actual.size());
}

void AgentLoop::insertOrMergeToolResultsAfterAssistant(
    const std::vector<ToolCall>& expected,
    std::vector<ToolResponse>& actual,
    const String& fallbackErrorText
) {
    std::lock_guard lock(impl_->historyMutex);
    insertToolResultsIntoHistory(impl_->messageHistory, expected, actual, fallbackErrorText);
}

// ============================================================================
// P2: History-level validation (sanity-check after repair)
// ============================================================================

bool validateHistoryAfterRepair(const std::vector<Message>& history) {
    for (size_t i = 0; i < history.size(); ++i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::Assistant || !msg.hasToolCalls()) continue;

        // The next message must exist and be a tool_result containing
        // all the tool_use IDs from this assistant message.
        if (i + 1 >= history.size()) {
            spdlog::error("validateHistoryAfterRepair: assistant at {} has tool_use "
                          "but no following message", i);
            return false;
        }

        auto& next = history[i + 1];
        if (next.role != MessageRole::ToolResult) {
            spdlog::error("validateHistoryAfterRepair: assistant at {} has tool_use "
                          "but next message is not tool_result (role={})",
                          i, static_cast<int>(next.role));
            return false;
        }

        // Check that every tool_use ID is covered
        std::set<String> resultIds;
        for (auto& tr : next.toolResults) {
            resultIds.insert(tr.callId);
        }
        for (auto& tc : msg.toolCalls) {
            if (!resultIds.count(tc.id)) {
                spdlog::error("validateHistoryAfterRepair: assistant at {} tool_use "
                              "id={} has no matching tool_result", i, tc.id);
                return false;
            }
        }
    }
    return true;
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
