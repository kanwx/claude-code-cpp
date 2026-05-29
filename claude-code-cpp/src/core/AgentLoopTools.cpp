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

    executor.setOnToolStart([this](const String& toolName, const String& description) {
        notifyToolEvent(ToolEventPhase::Start, toolName, description);
    });

    executor.setOnToolComplete([this](const String& toolName, bool success) {
        // Tool end notification is handled via onToolResult below,
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

    // Execute with ParallelReadOnly ordering (read-only tools concurrent, write tools sequential)
    auto execResults = executor.execute(calls);

    // Convert ToolExecutionResult -> ToolResponse and fire UI callbacks
    std::vector<ToolResponse> responses;
    responses.reserve(execResults.size());

    for (auto& er : execResults) {
        bool isError = er.response.isError;

        // Emit tool result — goes through emitStreamEvent which dispatches
        // to onStreamEvent if set, or falls back to onToolResult.
        // Do NOT call onToolResult directly here or it fires twice.
        StreamEvent toolResultEvent;
        toolResultEvent.type = StreamEvent::Type::ToolResultReady;
        toolResultEvent.toolName = er.response.toolName;
        toolResultEvent.toolResult = er.response.content;
        toolResultEvent.toolIsError = isError;
        toolResultEvent.toolIsCancelled = er.response.isCancelled;
        toolResultEvent.toolIsRejected = er.response.isRejected;
        for (const auto& call : calls) {
            if (call.name == er.response.toolName) {
                toolResultEvent.toolId = call.id;
                break;
            }
        }
        emitStreamEvent(std::move(toolResultEvent));

        // Fire end notification
        notifyToolEvent(ToolEventPhase::End, er.response.toolName, "", er.response.content);

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

} // namespace claude
