#include <claude/core/AgentLoopImpl.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <spdlog/spdlog.h>

namespace claude {

// ============================================================================
// Blocking iteration
// ============================================================================

AgentLoop::IterationResult AgentLoop::blockingIteration(const Json& request) {
    auto response = impl_->apiClient.call(request["messages"], request["tools"]);

    if (!response) {
        throw std::runtime_error(response.error());
    }

    Json& res = *response;

    // Parse usage
    Usage usage;
    if (res.contains("usage") && res["usage"].is_object()) {
        const auto& u = res["usage"];
        if (u.contains("prompt_tokens") && u["prompt_tokens"].is_number()) {
            usage.promptTokens = u["prompt_tokens"].get<long>();
        }
        if (u.contains("completion_tokens") && u["completion_tokens"].is_number()) {
            usage.completionTokens = u["completion_tokens"].get<long>();
        }
        if (u.contains("total_tokens") && u["total_tokens"].is_number()) {
            usage.totalTokens = u["total_tokens"].get<long>();
        }
    }

    // Parse stop_reason
    String stopReason = res.is_object() ? res.value("stop_reason", res.value("finish_reason", "end_turn")) : "end_turn";

    // Parse message
    Message msg = Message::assistant("");

    if (res.contains("content")) {
        if (res["content"].is_array()) {
            for (const auto& block : res["content"]) {
                if (!block.is_object()) continue;
                if (block.value("type", "") == "text") {
                    msg.content += block.value("text", "");
                } else if (block.value("type", "") == "tool_use") {
                    msg.toolCalls.push_back({
                        block.value("id", ""),
                        block.value("name", ""),
                        block.contains("input") ? block["input"].dump() : "{}"
                    });
                }
            }
        } else if (res["content"].is_string()) {
            msg.content = res["content"].get<String>();
        }
    }

    // OpenAI format
    if (res.contains("message")) {
        if (res["message"].contains("content") && res["message"]["content"].is_string()) {
            msg.content = res["message"]["content"].get<String>();
        } else {
            msg.content = "";
        }
        if (res["message"].contains("tool_calls") && res["message"]["tool_calls"].is_array()) {
            for (const auto& tc : res["message"]["tool_calls"]) {
                if (!tc.is_object()) continue;
                String id = tc.value("id", "call_0");
                String name = tc.contains("function") && tc["function"].is_object()
                    ? tc["function"].value("name", "unknown") : "unknown";
                String args = tc.contains("function") && tc["function"].is_object()
                    ? tc["function"].value("arguments", "{}") : "{}";
                msg.toolCalls.push_back({id, name, args});
            }
        }
    }

    return {msg, usage, stopReason};
}

// ============================================================================
// Streaming iteration
// ============================================================================

AgentLoop::IterationResult AgentLoop::streamingIteration(const Json& request, OnToken onToken) {
    String textBuffer;
    std::vector<ToolCall> toolCalls;
    std::map<String, String> toolCallBuffers;
    Usage usage;
    bool firstToken = true;
    String stopReason = "end_turn";

    // Initialize tool executor early if interleaving is enabled
    {
        bool shouldInterleave = false;
        {
            std::lock_guard lock(impl_->stateMutex);
            shouldInterleave = impl_->interleaveToolExecution;
        }
        if (shouldInterleave && !impl_->toolExecutor) {
            auto permCb = [&] {
                std::lock_guard lock2(impl_->callbackMutex);
                return impl_->onPermissionRequest;
            }();
            impl_->toolExecutor.emplace(impl_->tools, impl_->toolContext, impl_->hookManager, impl_->permissionEngine);
            impl_->toolExecutor->setOnPermissionRequest(permCb);
            impl_->toolExecutor->setTranscript(&impl_->messageHistory);

            // Wire progressive yielding callback for interleaved tool execution.
            // Each tool completion fires immediately via onToolResultReady,
            // rather than waiting for collectResults() at stream end.
            impl_->toolExecutor->setOnToolResultReady([this](const ToolExecutionResult& er) {
                bool isError = er.response.isError;

                // Emit tool result via old StreamEvent path (backward compat)
                StreamEvent toolResultEvent;
                toolResultEvent.type = StreamEvent::Type::ToolResultReady;
                toolResultEvent.toolId = er.response.callId;
                toolResultEvent.toolName = er.response.toolName;
                toolResultEvent.toolResult = er.response.content;
                toolResultEvent.toolIsError = isError;
                toolResultEvent.toolIsCancelled = er.response.isCancelled;
                toolResultEvent.toolIsRejected = er.response.isRejected;
                emitStreamEvent(std::move(toolResultEvent));

                // New 5-layer pipeline: emit StreamToolEvent immediately
                {
                    auto cb = [&] {
                        std::lock_guard lock(impl_->callbackMutex);
                        return impl_->onStreamToolEvent;
                    }();
                    if (cb) {
                        StreamToolEventType eventType = isError ? StreamToolEventType::Error
                            : er.response.isCancelled ? StreamToolEventType::Cancelled
                            : er.response.isRejected ? StreamToolEventType::Rejected
                            : StreamToolEventType::Completed;
                        cb(StreamToolEvent{
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
            });
        }
    }

    // For real-time token estimation
    int estimatedOutputTokens = 0;

    // Estimate input tokens from request messages (~4 chars per token)
    if (request.contains("messages") && request["messages"].is_array()) {
        String allText;
        for (const auto& msg : request["messages"]) {
            if (msg.contains("content")) {
                if (msg["content"].is_string()) {
                    allText += msg["content"].get<String>();
                } else if (msg["content"].is_array()) {
                    for (const auto& block : msg["content"]) {
                        if (block.contains("text")) {
                            allText += block["text"].get<String>();
                        }
                    }
                }
            }
        }
        long estimatedInputTokens = allText.length() / 4;
        if (estimatedInputTokens > 0) {
            impl_->tokenTracker.recordUsage(estimatedInputTokens, 0);
        }
    }

    // For Anthropic streaming tool calls
    std::map<int, ToolCall> anthropicToolCalls;
    int currentTextBlockIndex = -1;

    // For Extended Thinking
    String thinkingBuffer;
    String signatureBuffer;
    std::map<int, std::pair<String, String>> thinkingBlocks;
    std::vector<Json> redactedThinkingBlocks;  // Preserve for re-emission in API requests

    // Wire onTypedEvent to the API client if callback is set.
    // AnthropicClient already emits TypedStreamEvents via onTypedEvent for each SSE
    // event (ThinkingDelta, TextDelta, ToolUseStart, etc.). We must NOT also emit
    // them from the chunk callback below — that would duplicate every event.
    // For OpenAI/non-Anthropic clients, we emit TypedStreamEvents inline below.
    auto typedEventCb = [&] {
        std::lock_guard lock(impl_->callbackMutex);
        return impl_->onTypedEvent;
    }();
    bool useTypedEvents = impl_->apiClient.isNativeSseParser();
    if (useTypedEvents) {
        auto cb = impl_->onTypedEvent;
        // Wire typed event callback for native SSE parsers
        static_cast<AnthropicClient&>(impl_->apiClient).onTypedEvent = [cb](TypedStreamEvent&& event) {
            cb(std::move(event));
        };
    }

    impl_->apiClient.stream(request["messages"], request["tools"], [&](const Json& chunk) {
        // Handle usage (including cache tokens)
        if (chunk.contains("usage") && chunk["usage"].is_object()) {
            const auto& u = chunk["usage"];
            if (u.contains("prompt_tokens") && u["prompt_tokens"].is_number()) {
                usage.promptTokens = u["prompt_tokens"].get<long>();
            }
            if (u.contains("completion_tokens") && u["completion_tokens"].is_number()) {
                usage.completionTokens = u["completion_tokens"].get<long>();
            }
            if (u.contains("total_tokens") && u["total_tokens"].is_number()) {
                usage.totalTokens = u["total_tokens"].get<long>();
            }

            spdlog::debug("Usage from stream: prompt={}, completion={}, total={}",
                usage.promptTokens, usage.completionTokens, usage.totalTokens);

            if (usage.promptTokens > 0 || usage.completionTokens > 0) {
                impl_->tokenTracker.recordUsage(usage.promptTokens, usage.completionTokens);
            }
        }

        // Anthropic format
        String type = chunk.value("type", "");

        // Handle content_block_start for tool_use and thinking
        if (type == "content_block_start") {
            const auto& contentBlock = chunk.contains("content_block") && chunk["content_block"].is_object()
                ? chunk["content_block"] : Json::object();
            int index = chunk.contains("index") && chunk["index"].is_number() ? chunk["index"].get<int>() : 0;
            String blockType = contentBlock.contains("type") && contentBlock["type"].is_string()
                ? contentBlock["type"].get<String>() : "";

            if (blockType == "tool_use") {
                ToolCall tc;
                tc.id = contentBlock.contains("id") && contentBlock["id"].is_string() ? contentBlock["id"].get<String>() : "";
                tc.name = contentBlock.contains("name") && contentBlock["name"].is_string() ? contentBlock["name"].get<String>() : "";
                tc.arguments = "";
                anthropicToolCalls[index] = tc;
                // AnthropicClient already emits ToolUseStart via onTypedEvent
                if (typedEventCb && !useTypedEvents) {
                    typedEventCb(TypedStreamEvent{
                        .type = StreamEventType::ToolUseStart,
                        .blockIndex = index,
                        .toolCall = ToolCall{.id = tc.id, .name = tc.name, .arguments = ""}
                    });
                }
            } else if (blockType == "text") {
                currentTextBlockIndex = index;
            } else if (blockType == "thinking") {
                thinkingBlocks[index] = {"", ""};
            } else if (blockType == "redacted_thinking") {
                // Redacted thinking blocks contain encrypted content from extended thinking.
                // They MUST be preserved verbatim for subsequent API requests.
                // The data field is in the content_block_start event.
                thinkingBlocks[index] = {"[redacted]", ""};
                Json rtBlock;
                rtBlock["type"] = "redacted_thinking";
                // Capture the data field from the block if present
                if (chunk.contains("content_block") && chunk["content_block"].is_object()) {
                    auto& cb = chunk["content_block"];
                    if (cb.contains("data")) {
                        rtBlock["data"] = cb["data"];
                    }
                }
                redactedThinkingBlocks.push_back(rtBlock);
            }
        }

        if (type == "content_block_delta") {
            int index = chunk.contains("index") && chunk["index"].is_number() ? chunk["index"].get<int>() : 0;
            const auto& delta = chunk.contains("delta") && chunk["delta"].is_object()
                ? chunk["delta"] : Json::object();
            String deltaType = delta.contains("type") && delta["type"].is_string()
                ? delta["type"].get<String>() : "";

            if (deltaType == "text_delta") {
                String text = delta.contains("text") && delta["text"].is_string() ? delta["text"].get<String>() : "";
                if (!text.empty()) {
                    if (firstToken) {
                        firstToken = false;
                        auto cb = [&] {
                            std::lock_guard lock2(impl_->callbackMutex);
                            return impl_->onStreamStart;
                        }();
                        if (cb) cb();
                    }
                    textBuffer += text;
                    if (onToken) onToken(text);
                    // Only emit TextDelta for OpenAI clients — AnthropicClient
                    // already emits via onTypedEvent callback
                    if (typedEventCb && !useTypedEvents) {
                        typedEventCb(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = text});
                    }
                }
            } else if (deltaType == "input_json_delta") {
                String partialJson = delta.contains("partial_json") && delta["partial_json"].is_string()
                    ? delta["partial_json"].get<String>() : "";
                if (anthropicToolCalls.contains(index)) {
                    anthropicToolCalls[index].arguments += partialJson;
                }
                if (typedEventCb && !useTypedEvents) {
                    typedEventCb(TypedStreamEvent{
                        .type = StreamEventType::InputJsonDelta,
                        .text = partialJson,
                        .blockIndex = index
                    });
                }
            } else if (deltaType == "thinking_delta") {
                String thinking = delta.contains("thinking") && delta["thinking"].is_string()
                    ? delta["thinking"].get<String>() : "";
                if (!thinking.empty()) {
                    thinkingBuffer += thinking;
                    if (thinkingBlocks.contains(index)) {
                        thinkingBlocks[index].first += thinking;
                    }
                    // AnthropicClient already emits ThinkingDelta via onTypedEvent
                    if (typedEventCb && !useTypedEvents) {
                        typedEventCb(TypedStreamEvent{
                            .type = StreamEventType::ThinkingDelta,
                            .text = thinking
                        });
                    }
                }
            } else if (deltaType == "signature_delta") {
                String sig = delta.contains("signature") && delta["signature"].is_string()
                    ? delta["signature"].get<String>() : "";
                if (thinkingBlocks.contains(index)) {
                    thinkingBlocks[index].second = sig;
                }
            }
        }

        // ========== content_block_stop: key block-level yield ==========
        // Matches original TS yielding an AssistantMessage at each content_block_stop
        // Core of smooth output: notify UI as each block completes, not the entire response
        if (type == "content_block_stop") {
            int index = chunk.contains("index") && chunk["index"].is_number() ? chunk["index"].get<int>() : 0;
            String blockType = "unknown";

            if (anthropicToolCalls.contains(index)) {
                blockType = "tool_use";

                // Interleaved execution: dispatch this tool call immediately
                bool shouldInterleave = false;
                {
                    std::lock_guard lock2(impl_->stateMutex);
                    shouldInterleave = impl_->interleaveToolExecution;
                }
                if (shouldInterleave && impl_->toolExecutor) {
                    auto it = anthropicToolCalls.find(index);
                    if (it != anthropicToolCalls.end()) {
                        // Validate JSON before dispatching
                        try {
                            auto parsed = Json::parse(it->second.arguments);
                            (void)parsed;
                            impl_->toolExecutor->enqueue(std::move(it->second), index);
                            anthropicToolCalls.erase(it);
                        } catch (...) {
                            // Invalid JSON — leave in map, will be filtered later
                        }
                    }
                }
            } else if (thinkingBlocks.contains(index)) {
                // Distinguish redacted_thinking from regular thinking:
                // redacted_thinking blocks were stored with "[redacted]" marker text
                blockType = (thinkingBlocks[index].first == "[redacted]")
                    ? "redacted_thinking" : "thinking";
            } else {
                blockType = "text";
            }

            // Redacted thinking blocks are preserved in redactedThinkingBlocks for
            // API conversation continuity but must NOT be displayed to the user.
            if (blockType == "redacted_thinking") {
                // Already captured in redactedThinkingBlocks at content_block_start
            } else if (blockType == "thinking") {
                // AnthropicClient already emits ThinkingBlockStop via onTypedEvent
                if (typedEventCb && !useTypedEvents) {
                    typedEventCb(TypedStreamEvent{
                        .type = StreamEventType::ThinkingBlockStop
                    });
                }
            } else {
                auto cb = [&] {
                    std::lock_guard lock2(impl_->callbackMutex);
                    return impl_->onContentBlockStop;
                }();
                if (cb) {
                    String content;
                    if (blockType == "thinking" && thinkingBlocks.contains(index)) {
                        content = thinkingBlocks[index].first;
                    }
                    cb(blockType, index, content);
                }
            }
        }

        // message_start event - capture initial usage
        if (type == "message_start") {
            if (chunk.contains("message") && chunk["message"].is_object()) {
                const auto& msg = chunk["message"];
                if (msg.contains("usage") && msg["usage"].is_object()) {
                    const auto& u = msg["usage"];
                    if (u.contains("input_tokens") && u["input_tokens"].is_number())
                        usage.promptTokens = u["input_tokens"].get<long>();
                }
            }
        }

        // message_delta event - capture stop_reason, emit ToolUseComplete and StreamEnd
        if (type == "message_delta") {
            if (chunk.contains("delta") && chunk["delta"].is_object()) {
                const auto& delta = chunk["delta"];
                if (delta.contains("stop_reason") && delta["stop_reason"].is_string()) {
                    stopReason = delta["stop_reason"].get<String>();

                    // Emit ToolUseComplete for any pending Anthropic tool calls
                    // AnthropicClient already emits these via onTypedEvent
                    if (typedEventCb && !useTypedEvents && stopReason == "tool_use") {
                        for (auto& [idx, tc] : anthropicToolCalls) {
                            typedEventCb(TypedStreamEvent{
                                .type = StreamEventType::ToolUseComplete,
                                .blockIndex = idx,
                                .toolCall = tc
                            });
                        }
                    }

                    // Emit StreamEnd for new pipeline
                    // AnthropicClient emits StreamEnd via onTypedEvent on message_stop
                    if (typedEventCb && !useTypedEvents) {
                        typedEventCb(TypedStreamEvent{
                            .type = StreamEventType::StreamEnd,
                            .stopReason = stopReason
                        });
                    }
                }
            }
            // Also capture final usage from message_delta
            if (chunk.contains("usage") && chunk["usage"].is_object()) {
                const auto& u = chunk["usage"];
                if (u.contains("output_tokens") && u["output_tokens"].is_number()) {
                    usage.completionTokens = u["output_tokens"].get<long>();
                }
            }
        }

        // OpenAI format
        if (chunk.contains("choices") && chunk["choices"].is_array()) {
            for (const auto& choice : chunk["choices"]) {
                if (!choice.is_object()) continue;

                if (choice.contains("delta") && choice["delta"].is_object()) {
                    const auto& delta = choice["delta"];

                    // Text content
                    if (delta.contains("content") && delta["content"].is_string()) {
                        String text = delta["content"].get<String>();
                        if (!text.empty()) {
                            if (firstToken) {
                                firstToken = false;
                                auto streamStartCb = [&] {
                                    std::lock_guard lock2(impl_->callbackMutex);
                                    return impl_->onStreamStart;
                                }();
                                if (streamStartCb) streamStartCb();
                                // Emit StreamStart for new pipeline
                                if (typedEventCb) {
                                    typedEventCb(TypedStreamEvent{.type = StreamEventType::StreamStart});
                                }
                            }
                            textBuffer += text;
                            if (onToken) onToken(text);

                            // Emit TextDelta for new pipeline
                            if (typedEventCb) {
                                typedEventCb(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = text});
                            }

                            estimatedOutputTokens = textBuffer.length() / 4;
                            if (estimatedOutputTokens > 0) {
                                impl_->tokenTracker.setOutputTokens(estimatedOutputTokens);
                            }
                        }
                    }

                    // Tool calls (OpenAI streaming format)
                    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                        for (const auto& tc : delta["tool_calls"]) {
                            if (!tc.is_object()) continue;

                            String id = tc.contains("id") && tc["id"].is_string() ? tc["id"].get<String>() : "";
                            int tcIndex = tc.contains("index") && tc["index"].is_number() ? tc["index"].get<int>() : 0;

                            while ((int)toolCalls.size() <= tcIndex) {
                                toolCalls.push_back({"", "", ""});
                            }

                            if (!id.empty()) {
                                toolCalls[tcIndex].id = id;
                                // Emit ToolUseStart when we first see the tool call ID
                                if (typedEventCb) {
                                    String name;
                                    if (tc.contains("function") && tc["function"].is_object() &&
                                        tc["function"].contains("name") && tc["function"]["name"].is_string()) {
                                        name = tc["function"]["name"].get<String>();
                                    }
                                    typedEventCb(TypedStreamEvent{
                                        .type = StreamEventType::ToolUseStart,
                                        .blockIndex = tcIndex,
                                        .toolCall = ToolCall{.id = id, .name = name, .arguments = ""}
                                    });
                                }
                            }

                            if (tc.contains("function") && tc["function"].is_object()) {
                                const auto& func = tc["function"];
                                if (func.contains("name") && func["name"].is_string()) {
                                    toolCalls[tcIndex].name = func["name"].get<String>();
                                }
                                if (func.contains("arguments") && func["arguments"].is_string()) {
                                    String partialJson = func["arguments"].get<String>();
                                    toolCalls[tcIndex].arguments += partialJson;
                                    // Emit InputJsonDelta for new pipeline
                                    if (typedEventCb) {
                                        typedEventCb(TypedStreamEvent{
                                            .type = StreamEventType::InputJsonDelta,
                                            .text = partialJson,
                                            .blockIndex = tcIndex
                                        });
                                    }
                                }
                            }
                        }
                    }
                }

                // finish_reason
                if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                    String finish = choice["finish_reason"].get<String>();
                    if (finish == "tool_calls") {
                        stopReason = "tool_use";
                        spdlog::debug("OpenAI stream finished with tool_calls");
                    } else if (finish == "length") {
                        stopReason = "max_tokens";
                    } else if (!finish.empty() && finish != "null") {
                        stopReason = finish;
                    }

                    // Emit ToolUseComplete for any pending tool calls
                    if (typedEventCb && finish == "tool_calls") {
                        for (size_t i = 0; i < toolCalls.size(); ++i) {
                            typedEventCb(TypedStreamEvent{
                                .type = StreamEventType::ToolUseComplete,
                                .blockIndex = static_cast<int>(i),
                                .toolCall = toolCalls[i]
                            });
                        }
                    }

                    // Emit StreamEnd for new pipeline
                    if (typedEventCb) {
                        typedEventCb(TypedStreamEvent{
                            .type = StreamEventType::StreamEnd,
                            .stopReason = stopReason
                        });
                    }

                    // content_block_stop equivalent for OpenAI format
                    {
                        auto cb = [&] {
                            std::lock_guard lock2(impl_->callbackMutex);
                            return impl_->onContentBlockStop;
                        }();
                        if (cb) {
                            cb(finish == "tool_calls" ? "tool_use" : "text", 0, "");
                        }
                    }
                }
            }
        }
    });

    // Collect interleaved execution results.
    // Note: StreamToolEvent and StreamEvent emissions already happened
    // progressively via the onToolResultReady callback wired above.
    // Here we only need to collect the ToolResponse objects for the loop.
    std::vector<ToolResponse> interleavedToolResponses;
    bool shouldCollect = false;
    {
        std::lock_guard lock(impl_->stateMutex);
        shouldCollect = impl_->interleaveToolExecution;
    }
    if (shouldCollect && impl_->toolExecutor && impl_->toolExecutor->hasPending()) {
        auto interleaveExecResults = impl_->toolExecutor->collectResults();
        interleavedToolResponses.reserve(interleaveExecResults.size());
        for (auto& ier : interleaveExecResults) {
            interleavedToolResponses.push_back(std::move(ier.response));
        }
    }

    // Convert Anthropic tool calls
    if (!anthropicToolCalls.empty()) {
        for (auto& [index, tc] : anthropicToolCalls) {
            toolCalls.push_back(std::move(tc));
        }
    }

    // Validate tool calls: handle any with invalid JSON arguments.
    // When the stream is aborted mid-tool-call, the arguments buffer
    // may contain truncated JSON. We keep the tool call in the message
    // (so the API sees the tool_use block) and generate a synthetic
    // error tool_result so the model can recover.
    std::vector<ToolCall> validToolCalls;
    std::vector<ToolResponse> syntheticErrorResults;
    for (auto& tc : toolCalls) {
        try {
            auto parsed = Json::parse(tc.arguments);
            (void)parsed;
            validToolCalls.push_back(std::move(tc));
        } catch (...) {
            // Truncated JSON — generate synthetic error result
            spdlog::warn("Tool call {} has truncated JSON arguments, generating error result", tc.name);
            syntheticErrorResults.emplace_back(
                tc.id.empty() ? "call_0" : tc.id,
                tc.name.empty() ? "unknown" : tc.name,
                "Error: Tool call had truncated/malformed JSON arguments. The stream was interrupted.",
                true  // isError
            );
            // Keep the tool call with empty arguments so it appears in the assistant message
            tc.arguments = "{}";
            validToolCalls.push_back(std::move(tc));
        }
    }
    toolCalls = std::move(validToolCalls);

    // If cancelled during streaming and no valid tool calls remain,
    // treat as end of turn rather than tool_use
    bool wasAborted = impl_->cancelled.load(std::memory_order_acquire);
    if (wasAborted && toolCalls.empty()) {
        stopReason = "end_turn";
    }

    // Build message with thinking support
    Message msg = Message::assistant(textBuffer, std::move(toolCalls));
    if (!thinkingBuffer.empty()) {
        msg.thinking = thinkingBuffer;
    }
    if (!signatureBuffer.empty()) {
        msg.signature = signatureBuffer;
    }
    if (!redactedThinkingBlocks.empty()) {
        msg.redactedThinking = std::move(redactedThinkingBlocks);
    }

    spdlog::debug("streamingIteration done: text={} bytes, toolCalls={}, stopReason={}, interleaved={}, syntheticErrors={}",
        textBuffer.size(), msg.toolCalls.size(), stopReason, interleavedToolResponses.size(), syntheticErrorResults.size());

    // Append synthetic error results for truncated tool calls
    if (!syntheticErrorResults.empty()) {
        interleavedToolResponses.insert(interleavedToolResponses.end(),
            std::make_move_iterator(syntheticErrorResults.begin()),
            std::make_move_iterator(syntheticErrorResults.end()));
    }

    return {msg, usage, stopReason, std::move(interleavedToolResponses), wasAborted};
}

} // namespace claude
