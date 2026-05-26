#include <claude/core/AgentLoop.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/core/compact/MicroCompact.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/compact/SessionMemoryCompact.hpp>
#include <claude/core/compact/MessageGrouping.hpp>
#include <claude/core/compact/ApiMicroCompact.hpp>
#include <claude/tool/ResultTruncation.hpp>
#include <claude/api/RetryableClient.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {

AgentLoop::AgentLoop(
    ApiClient& apiClient,
    ToolRegistry& tools,
    const String& systemPrompt
)
    : apiClient_(apiClient)
    , tools_(tools)
    , systemPrompt_(systemPrompt)
    , tokenTracker_()
    , toolContext_(ToolContext::create(std::filesystem::current_path()))
{
    toolContext_.set("apiClient", static_cast<ApiClient*>(&apiClient_));
    messageHistory_.push_back(Message::system(systemPrompt));
}

AgentLoop::AgentLoop(
    ApiClient& apiClient,
    ToolRegistry& tools,
    const String& systemPrompt,
    TokenTracker& tokenTracker
)
    : apiClient_(apiClient)
    , tools_(tools)
    , systemPrompt_(systemPrompt)
    , tokenTracker_(std::move(tokenTracker))
    , toolContext_(ToolContext::create(std::filesystem::current_path()))
{
    toolContext_.set("apiClient", static_cast<ApiClient*>(&apiClient_));
    messageHistory_.push_back(Message::system(systemPrompt));
}

std::expected<String, String> AgentLoop::run(const String& userInput) {
    injectContext(userInput);
    currentUserInput_ = userInput;
    resetCancel();
    return executeLoop(false, nullptr);
}

std::expected<String, String> AgentLoop::runStreaming(const String& userInput, OnToken onToken) {
    injectContext(userInput);
    currentUserInput_ = userInput;
    resetCancel();
    return executeLoop(true, onToken);
}

void AgentLoop::injectContext(const String& userInput) {
    if (!contextInjector_) {
        messageHistory_.push_back(Message::user(userInput));
        return;
    }

    auto ctx = contextInjector_->buildContext(userInput);
    String contextPrefix = contextInjector_->formatAsMessageContent(ctx);

    if (contextPrefix.empty()) {
        messageHistory_.push_back(Message::user(userInput));
    } else {
        // Inject context as a system-reminder user message prefix, matching TS behavior
        String fullContent = contextPrefix + userInput;
        messageHistory_.push_back(Message::user(fullContent));
    }

    // Clear per-turn attachments (not system-level context like git/claudeMd)
    contextInjector_->clearAttachments();
}

void AgentLoop::refreshContext() {
    // This is called between turns to allow the ContextInjector to
    // update dynamic state. The ContextInjector itself manages its
    // internal state (git status, CLAUDE.md) — this is just a hook
    // for the main loop to signal "a new turn is about to begin."
}

void AgentLoop::cancel() {
    cancelled_.store(true, std::memory_order_release);
    apiClient_.abort();
    if (toolExecutor_) {
        toolExecutor_->cancel();
    }
    // Note: spdlog is NOT async-signal-safe, so we log from the
    // loop's cancel check point rather than here.
}

void AgentLoop::resetCancel() {
    cancelled_.store(false, std::memory_order_release);
    apiClient_.resetAbort();
    if (toolExecutor_) {
        // StreamingToolExecutor resets cancelled_ at the start of each execute() batch
    }
}

std::expected<String, String> AgentLoop::executeLoop(bool streaming, OnToken onToken) {
    int iteration = 0;
    String lastAssistantText;
    int maxOutputTokensRecoveryCount = 0;
    reactiveCompactAttempts_ = 0;

    while (iteration < maxIterations_) {
        iteration++;

        // Check cancellation at start of each iteration
        if (cancelled_.load(std::memory_order_acquire)) {
            spdlog::debug("AgentLoop: cancelled at iteration {}", iteration);
            if (onCancelled_) onCancelled_();
            // Trim any incomplete partial content from lastAssistantText
            if (!lastAssistantText.empty()) {
                return lastAssistantText;
            }
            return std::unexpected("Cancelled by user");
        }

        spdlog::debug("Agent loop iteration {} ({})", iteration, streaming ? "streaming" : "blocking");

        // 通知循环继续 (TAOR Repeat阶段)
        if (iteration > 1 && onLoopContinue_) {
            onLoopContinue_(iteration, maxIterations_);
        }

        // Emit StreamStart at beginning of each iteration
        emitStreamEvent(StreamEvent{StreamEvent::Type::StreamStart});

        // 构建请求
        Json request = buildApiRequest();

        // 调用 API
        IterationResult result;
        try {
            if (streaming && onToken) {
                result = streamingIteration(request, onToken);
            } else {
                result = blockingIteration(request);
            }
        } catch (const PromptTooLongException& ptl) {
            // ========== 413 Prompt-Too-Long: Reactive Compact Recovery ==========
            spdlog::warn("413 prompt-too-long: {} tokens > {} limit (gap: {})",
                ptl.actualTokens(), ptl.maxTokens(), ptl.tokenGap());
            addMissingToolResults();
            if (attemptReactiveCompact(ptl.tokenGap())) {
                continue;  // Retry with compressed context
            }
            return std::unexpected("Context too long and compact failed. Try /compact manually.");
        } catch (const FallbackTriggered& fb) {
            // ========== Model Fallback: Tombstone + System Warning ==========
            spdlog::warn("Model fallback: {} -> {}", fb.fromModel, fb.toModel);

            // 1. Add missing tool_results for any dangling tool_uses
            addMissingToolResults();

            // 2. Strip thinking/signature blocks from message history
            stripThinkingFromHistory();

            // 3. Emit tombstone event for any partial content already yielded
            StreamEvent tombstoneEvent;
            tombstoneEvent.type = StreamEvent::Type::Tombstone;
            tombstoneEvent.fallbackFromModel = fb.fromModel;
            tombstoneEvent.fallbackToModel = fb.toModel;
            emitStreamEvent(std::move(tombstoneEvent));

            // 4. Inject system warning message
            messageHistory_.push_back(Message::user(
                "[System: Switched to " + fb.toModel + " due to high demand for " +
                (fb.fromModel.empty() ? String("primary model") : fb.fromModel) + "]"));
            messageHistory_.push_back(Message::assistant(
                "Understood. I'll continue with " + fb.toModel + "."));

            // 5. Reset recovery counters
            maxOutputTokensRecoveryCount = 0;
            reactiveCompactAttempts_ = 0;

            continue;  // Retry with fallback model
        } catch (const std::exception& e) {
            addMissingToolResults();
            return std::unexpected("API call failed: " + String(e.what()));
        }

        // 记录使用量
        if (result.usage.promptTokens > 0 || result.usage.completionTokens > 0) {
            tokenTracker_.recordUsage(result.usage.promptTokens, result.usage.completionTokens);
            tokenTracker_.recordTaskUsage(result.usage.promptTokens, result.usage.completionTokens);
        }

        // Check task budget
        if (tokenTracker_.isTaskBudgetExceeded()) {
            spdlog::info("Task budget exceeded: {}/{} tokens",
                tokenTracker_.getTaskBudgetUsed(), tokenTracker_.getTaskBudget());
            lastAssistantText += "\n\n[Task budget exceeded: " +
                std::to_string(tokenTracker_.getTaskBudgetUsed()) + "/" +
                std::to_string(tokenTracker_.getTaskBudget()) + " tokens used]";
            break;
        }

        // Check cancellation after API call returns
        // If cancelled mid-stream, the partial message may have incomplete content.
        // Don't add it to history — just return what we have.
        if (cancelled_.load(std::memory_order_acquire)) {
            spdlog::debug("AgentLoop: cancelled after API iteration {}", iteration);
            if (onCancelled_) onCancelled_();
            if (!result.message.content.empty()) {
                return result.message.content;
            }
            return std::unexpected("Cancelled by user");
        }

        // 添加助手消息到历史
        result.message.apiRound = iteration;
        messageHistory_.push_back(result.message);

        // ========== Stop Hook ==========
        // When the model stops with end_turn (not max_tokens), run stop hooks.
        // Stop hooks can force the loop to continue (matching TS handleStopHooks).
        if (result.stopReason != "max_tokens" && result.stopReason != "length") {
            // First: fire PostResponse hook for backward compat
            HookContext hookCtx;
            hookCtx.toolName = "response";
            hookCtx.extras["stopReason"] = result.stopReason;
            hookCtx.extras["content"] = result.message.content;
            auto hookResult = hookManager_.execute(HookType::PostResponse, hookCtx);
            if (hookResult.shouldAbort()) {
                spdlog::info("PostResponse hook aborted the loop");
                String reason = hookCtx.extras.count("reason") ? hookCtx.extras["reason"] : "Hook blocked continuation";
                lastAssistantText += "\n\n[Stopped by hook: " + reason + "]";
                break;
            }

            // Then: run Stop hook (can force continuation)
            if (onStopHook_) {
                auto stopResult = onStopHook_();
                if (stopResult.shouldContinue) {
                    spdlog::info("Stop hook forced continuation: {}", stopResult.reason);
                    messageHistory_.push_back(Message::user(
                        "[System: Continue your work. " + stopResult.reason + "]"));
                    continue;
                }
            }
        }

        // 提取文本
        if (!result.message.content.empty()) {
            lastAssistantText = result.message.content;
            if (!streaming && onAssistantMessage_) {
                onAssistantMessage_(lastAssistantText);
            }
        }

        // ========== max_output_tokens 恢复机制 ==========
        // 当模型因 max_tokens 截断时，自动续写而不是终止
        // 匹配原版 TS 的 recovery loop 行为
        if (result.stopReason == "max_tokens" || result.stopReason == "length") {
            maxOutputTokensRecoveryCount++;
            if (maxOutputTokensRecoveryCount <= MAX_OUTPUT_TOKENS_RECOVERY) {
                spdlog::info("max_output_tokens reached, recovery iteration {}/{}",
                    maxOutputTokensRecoveryCount, MAX_OUTPUT_TOKENS_RECOVERY);

                // Escalate max_tokens on 2nd+ recovery attempt
                if (maxOutputTokensRecoveryCount >= 2 && maxTokensOverride_ < ESCALATED_MAX_TOKENS) {
                    maxTokensOverride_ = ESCALATED_MAX_TOKENS;
                    spdlog::info("Escalating max_tokens to {} for recovery", ESCALATED_MAX_TOKENS);
                }

                // 添加续写指令 (匹配原版TS: "Resume directly — no apology, no recap")
                messageHistory_.push_back(Message::assistant(
                    "Continue from where you left off. Do not repeat what you already wrote. "
                    "Resume directly — no apology, no recap."
                ));
                continue;  // TAOR: Repeat
            }
            // 超过恢复次数，添加截断警告并结束
            lastAssistantText += "\n\n[Output truncated: max tokens reached]";
            break;
        }

        // 没有工具调用 → 结束
        if (!result.message.hasToolCalls() && result.interleavedToolResults.empty()) {
            break;
        }

        // ========== TAOR: Act + Observe ==========
        std::vector<ToolResponse> toolResponses;

        // Add interleaved results (already executed during streaming)
        if (!result.interleavedToolResults.empty()) {
            toolResponses = std::move(result.interleavedToolResults);
        }

        // Execute any remaining tool calls (not interleaved)
        if (result.message.hasToolCalls()) {
            auto batchResponses = executeToolCalls(result.message.toolCalls);
            toolResponses.insert(toolResponses.end(),
                std::make_move_iterator(batchResponses.begin()),
                std::make_move_iterator(batchResponses.end()));
        }

        // Check cancellation after tool execution
        if (cancelled_.load(std::memory_order_acquire)) {
            spdlog::debug("AgentLoop: cancelled after tool execution at iteration {}", iteration);
            if (onCancelled_) onCancelled_();
            if (!lastAssistantText.empty()) {
                return lastAssistantText;
            }
            return std::unexpected("Cancelled by user");
        }

        // ========== Skill sentinel detection ==========
        // When SkillTool returns __SKILL_PROMPT__, inject the skill prompt as
        // a new user message so the AI executes the skill instructions.
        String pendingSkillPrompt;
        String pendingSkillModel;
        std::vector<String> pendingSkillTools;

        for (auto& resp : toolResponses) {
            if (resp.toolName != "Skill" || resp.content.empty()) continue;
            auto sentinelPos = resp.content.find("__SKILL_PROMPT__");
            if (sentinelPos == String::npos) continue;

            String displayInfo = resp.content.substr(0, sentinelPos);
            String afterSentinel = resp.content.substr(sentinelPos);
            std::istringstream sStream(afterSentinel);
            String headerLine;
            bool pastHeaders = false;
            String promptBody;

            while (std::getline(sStream, headerLine)) {
                if (!pastHeaders) {
                    if (headerLine.rfind("__SKILL_PROMPT__", 0) == 0) {
                        pendingSkillModel = headerLine.substr(16);
                    } else if (headerLine.rfind("__SKILL_TOOLS__", 0) == 0) {
                        String toolsStr = headerLine.substr(15);
                        std::istringstream tStream(toolsStr);
                        String tool;
                        while (std::getline(tStream, tool, ',')) {
                            if (!tool.empty()) pendingSkillTools.push_back(tool);
                        }
                    } else if (headerLine.empty()) {
                        pastHeaders = true;
                    }
                } else {
                    if (!promptBody.empty()) promptBody += "\n";
                    promptBody += headerLine;
                }
            }

            resp.content = displayInfo;
            pendingSkillPrompt = std::move(promptBody);
            spdlog::info("SkillTool detected: model='{}', tools={}, prompt={} chars",
                pendingSkillModel, pendingSkillTools.size(), pendingSkillPrompt.size());
        }

        // 添加工具结果到历史
        {
            auto toolMsg = Message::toolResult(std::move(toolResponses));
            toolMsg.apiRound = iteration;
            messageHistory_.push_back(std::move(toolMsg));
        }

        // Inject skill prompt as a new user message
        if (!pendingSkillPrompt.empty()) {
            messageHistory_.push_back(Message::user(pendingSkillPrompt));
            // TODO: If pendingSkillModel is non-empty, switch model for this turn
            // TODO: If pendingSkillTools is non-empty, restrict tools for this turn
            spdlog::debug("Skill prompt injected as user message");
        }

        // ========== 微压缩：清除过期工具结果 ==========
        applyMicrocompact();

        // ========== 自动压缩：上下文窗口超 93% 时压缩 ==========
        applyAutoCompact();

        // TAOR: 循环继续 — 下一轮 Think
    }

    if (iteration >= maxIterations_) {
        spdlog::warn("Agent loop reached max iterations {}", maxIterations_);
        lastAssistantText += "\n\n[WARNING: Maximum loop iteration limit reached]";
    }

    // Reset escalated max_tokens to default — only reset if we escalated
    // (don't reset user-set overrides that weren't from recovery escalation)
    if (maxTokensOverride_ > 0 && maxTokensOverride_ == ESCALATED_MAX_TOKENS) {
        apiClient_.setMaxTokens(16384);
        maxTokensOverride_ = -1;  // Reset override
    }

    return lastAssistantText;
}

AgentLoop::IterationResult AgentLoop::blockingIteration(const Json& request) {
    auto response = apiClient_.call(request["messages"], request["tools"]);

    if (!response) {
        throw std::runtime_error(response.error());
    }

    Json& res = *response;

    // 解析使用量
    Usage usage;
    if (res.contains("usage")) {
        usage.promptTokens = res["usage"].value("prompt_tokens", 0L);
        usage.completionTokens = res["usage"].value("completion_tokens", 0L);
        usage.totalTokens = res["usage"].value("total_tokens", 0L);
    }

    // 解析 stop_reason
    String stopReason = res.value("stop_reason", res.value("finish_reason", "end_turn"));

    // 解析消息
    Message msg = Message::assistant("");

    if (res.contains("content")) {
        if (res["content"].is_array()) {
            for (const auto& block : res["content"]) {
                if (block.value("type", "") == "text") {
                    msg.content += block.value("text", "");
                } else if (block.value("type", "") == "tool_use") {
                    msg.toolCalls.push_back({
                        block.value("id", ""),
                        block.value("name", ""),
                        block.value("input", Json{}).dump()
                    });
                }
            }
        } else if (res["content"].is_string()) {
            msg.content = res["content"].get<String>();
        }
    }

    // OpenAI 格式
    if (res.contains("message")) {
        if (res["message"].contains("content") && res["message"]["content"].is_string()) {
            msg.content = res["message"]["content"].get<String>();
        } else {
            msg.content = "";
        }
        if (res["message"].contains("tool_calls") && res["message"]["tool_calls"].is_array()) {
            for (const auto& tc : res["message"]["tool_calls"]) {
                String id = tc.value("id", "call_0");
                String name = tc.contains("function") ? tc["function"].value("name", "unknown") : "unknown";
                String args = tc.contains("function") ? tc["function"].value("arguments", "{}") : "{}";
                msg.toolCalls.push_back({id, name, args});
            }
        }
    }

    return {msg, usage, stopReason};
}

AgentLoop::IterationResult AgentLoop::streamingIteration(const Json& request, OnToken onToken) {
    String textBuffer;
    std::vector<ToolCall> toolCalls;
    std::map<String, String> toolCallBuffers;
    Usage usage;
    bool firstToken = true;
    String stopReason = "end_turn";

    // Initialize tool executor early if interleaving is enabled
    if (interleaveToolExecution_ && !toolExecutor_) {
        toolExecutor_.emplace(tools_, toolContext_, hookManager_, permissionEngine_);
        toolExecutor_->setOnPermissionRequest(onPermissionRequest_);
        toolExecutor_->setTranscript(&messageHistory_);
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
            tokenTracker_.recordUsage(estimatedInputTokens, 0);
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

    apiClient_.stream(request["messages"], request["tools"], [&](const Json& chunk) {
        // 处理使用量 (包括缓存 tokens)
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
                tokenTracker_.recordUsage(usage.promptTokens, usage.completionTokens);
            }
        }

        // Anthropic 格式
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
                        if (onStreamStart_) onStreamStart_();
                    }
                    textBuffer += text;
                    if (onToken) onToken(text);
                }
            } else if (deltaType == "input_json_delta") {
                String partialJson = delta.contains("partial_json") && delta["partial_json"].is_string()
                    ? delta["partial_json"].get<String>() : "";
                if (anthropicToolCalls.contains(index)) {
                    anthropicToolCalls[index].arguments += partialJson;
                }
            } else if (deltaType == "thinking_delta") {
                String thinking = delta.contains("thinking") && delta["thinking"].is_string()
                    ? delta["thinking"].get<String>() : "";
                if (!thinking.empty()) {
                    thinkingBuffer += thinking;
                    if (thinkingBlocks.contains(index)) {
                        thinkingBlocks[index].first += thinking;
                    }
                    if (onThinking_) onThinking_(thinking);
                }
            } else if (deltaType == "signature_delta") {
                String sig = delta.contains("signature") && delta["signature"].is_string()
                    ? delta["signature"].get<String>() : "";
                if (thinkingBlocks.contains(index)) {
                    thinkingBlocks[index].second += sig;
                }
            }
        }

        // ========== content_block_stop: 关键的 block 级 yield ==========
        // 匹配原版 TS 在每个 content_block_stop 时 yield 一个 AssistantMessage
        // 这是流畅输出的核心：不等整个 response 完成，每个块完成即通知UI
        if (type == "content_block_stop") {
            int index = chunk.contains("index") && chunk["index"].is_number() ? chunk["index"].get<int>() : 0;
            String blockType = "unknown";

            if (anthropicToolCalls.contains(index)) {
                blockType = "tool_use";

                // Interleaved execution: dispatch this tool call immediately
                if (interleaveToolExecution_ && toolExecutor_) {
                    auto it = anthropicToolCalls.find(index);
                    if (it != anthropicToolCalls.end()) {
                        // Validate JSON before dispatching
                        try {
                            auto parsed = Json::parse(it->second.arguments);
                            (void)parsed;
                            toolExecutor_->enqueue(std::move(it->second), index);
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
            // Skip UI callbacks for redacted_thinking entirely.
            if (blockType == "redacted_thinking") {
                // Already captured in redactedThinkingBlocks at content_block_start
            } else if (onContentBlockStop_) {
                String content;
                if (blockType == "thinking" && thinkingBlocks.contains(index)) {
                    content = thinkingBlocks[index].first;
                }
                onContentBlockStop_(blockType, index, content);
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

        // message_delta event - capture stop_reason
        if (type == "message_delta") {
            if (chunk.contains("delta") && chunk["delta"].is_object()) {
                const auto& delta = chunk["delta"];
                if (delta.contains("stop_reason") && delta["stop_reason"].is_string()) {
                    stopReason = delta["stop_reason"].get<String>();
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

        // OpenAI 格式
        if (chunk.contains("choices") && chunk["choices"].is_array()) {
            for (const auto& choice : chunk["choices"]) {
                if (!choice.is_object()) continue;

                if (choice.contains("delta") && choice["delta"].is_object()) {
                    const auto& delta = choice["delta"];

                    // 文本内容
                    if (delta.contains("content") && delta["content"].is_string()) {
                        String text = delta["content"].get<String>();
                        if (!text.empty()) {
                            if (firstToken) {
                                firstToken = false;
                                if (onStreamStart_) onStreamStart_();
                            }
                            textBuffer += text;
                            if (onToken) onToken(text);

                            estimatedOutputTokens = textBuffer.length() / 4;
                            if (estimatedOutputTokens > 0) {
                                tokenTracker_.setOutputTokens(estimatedOutputTokens);
                            }
                        }
                    }

                    // 工具调用 (OpenAI 流式格式)
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
                            }

                            if (tc.contains("function") && tc["function"].is_object()) {
                                const auto& func = tc["function"];
                                if (func.contains("name") && func["name"].is_string()) {
                                    toolCalls[tcIndex].name = func["name"].get<String>();
                                }
                                if (func.contains("arguments") && func["arguments"].is_string()) {
                                    toolCalls[tcIndex].arguments += func["arguments"].get<String>();
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

                    // content_block_stop equivalent for OpenAI format
                    if (onContentBlockStop_) {
                        onContentBlockStop_(finish == "tool_calls" ? "tool_use" : "text", 0, "");
                    }
                }
            }
        }
    });

    // Collect interleaved execution results
    std::vector<ToolResponse> interleavedToolResponses;
    if (interleaveToolExecution_ && toolExecutor_ && toolExecutor_->hasPending()) {
        auto interleaveExecResults = toolExecutor_->collectResults();
        interleavedToolResponses.reserve(interleaveExecResults.size());
        for (auto& ier : interleaveExecResults) {
            StreamEvent toolResultEvent;
            toolResultEvent.type = StreamEvent::Type::ToolResultReady;
            toolResultEvent.toolName = ier.response.toolName;
            toolResultEvent.toolResult = ier.response.content;
            toolResultEvent.toolIsError = ier.response.isError;
            toolResultEvent.toolIsCancelled = ier.response.isCancelled;
            toolResultEvent.toolIsRejected = ier.response.isRejected;
            emitStreamEvent(std::move(toolResultEvent));

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
    if (cancelled_.load(std::memory_order_acquire) && toolCalls.empty()) {
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

    return {msg, usage, stopReason, std::move(interleavedToolResponses)};
}

std::vector<ToolResponse> AgentLoop::executeToolCalls(const std::vector<ToolCall>& calls) {
    // ========== 初始化 StreamingToolExecutor (lazy) ==========
    if (!toolExecutor_) {
        toolExecutor_.emplace(tools_, toolContext_, hookManager_, permissionEngine_);
    }

    auto& executor = *toolExecutor_;

    // Wire callbacks from AgentLoop into the executor
    executor.setOnPermissionRequest(onPermissionRequest_);
    executor.setTranscript(&messageHistory_);

    // Store parent permission callback in ToolContext for sub-agent delegation
    toolContext_.set("parentPermissionCallback", onPermissionRequest_);

    executor.setOnToolStart([this](const String& toolName, const String& description) {
        notifyToolEvent(ToolEventPhase::Start, toolName, description);
    });

    executor.setOnToolComplete([this](const String& toolName, bool success) {
        // Tool end notification is handled via onToolResult_ below,
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
        // to onStreamEvent_ if set, or falls back to onToolResult_.
        // Do NOT call onToolResult_ directly here or it fires twice.
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
    // 查找工具
    Tool* tool = tools_.findByName(call.name);
    if (!tool) {
        return "Error: Unknown tool '" + call.name + "'";
    }

    // 解析参数
    Json input;
    try {
        input = Json::parse(call.arguments);
    } catch (const Json::parse_error& e) {
        return "Error: Invalid JSON arguments: " + String(e.what());
    }

    // 通知开始
    notifyToolEvent(ToolEventPhase::Start, call.name, call.arguments);

    // ========== PreToolUse Hook ==========
    HookContext preCtx;
    preCtx.toolName = call.name;
    preCtx.input = input;
    if (hookManager_.execute(HookType::PreToolUse, preCtx) .shouldAbort()) {
        notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "Hook blocked");
        return "Error: Tool blocked by pre-tool hook";
    }

    // 权限检查 (考虑 hook 的权限覆盖)
    auto permOverride = preCtx.getPermissionOverride();
    if (permOverride && *permOverride) {
        // Hook 强制允许：跳过权限检查
        spdlog::debug("Permission overridden by hook: allowing {}", call.name);
    } else if (permOverride && !*permOverride) {
        // Hook 强制拒绝
        notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "Hook denied");
        return "Permission denied";
    } else if (permissionEngine_) {
        auto decision = permissionEngine_->evaluate(call.name, input, tool->isReadOnly(), messageHistory_);

        if (decision.isDenied()) {
            notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "Permission denied");
            return "Permission denied: " + decision.reason;
        }

        if (decision.needsAsk() && onPermissionRequest_) {
            PermissionRequest req{call.name, call.arguments, tool->activityDescription(input)};
            auto choice = onPermissionRequest_(req);

            if (choice == PermissionChoice::DenyOnce || choice == PermissionChoice::AlwaysDeny) {
                notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, "User denied");
                return "Permission denied";
            }

            // 应用选择
            String command = input.value("command", input.value("file_path", ""));
            permissionEngine_->applyChoice(choice, call.name, command);
        }
    }

    // 执行
    String result;
    try {
        result = tool->execute(input, toolContext_);
    } catch (const std::exception& e) {
        result = "Error: " + String(e.what());
    }

    // ========== 工具结果预算截断 ==========
    if (result.size() > tool->maxResultSizeChars()) {
        spdlog::info("Tool [{}] result size {} exceeds budget {}, truncating",
            call.name, result.size(), tool->maxResultSizeChars());
        result = ResultTruncation::truncate(result, tool->maxResultSizeChars(), call.name);
    }

    // ========== PostToolUse Hook ==========
    HookContext postCtx;
    postCtx.toolName = call.name;
    postCtx.input = input;
    postCtx.result = result;
    if (hookManager_.execute(HookType::PostToolUse, postCtx) .shouldAbort()) {
        return "Error: Tool execution blocked by post-tool hook";
    }
    // Hook may have modified the result
    if (postCtx.result && *postCtx.result != result) {
        result = *postCtx.result;
    }

    // 通知结束 (注意: 对于并发执行的工具，End通知在executeToolCalls中统一处理)
    // 对于串行执行的工具，这里直接通知
    if (!isToolReadOnly(call.name)) {
        notifyToolEvent(ToolEventPhase::End, call.name, call.arguments, result);
    }

    return result;
}

bool AgentLoop::isToolReadOnly(const String& toolName) const {
    Tool* tool = tools_.findByName(toolName);
    if (!tool) return false;
    return tool->isReadOnly();
}

Json AgentLoop::buildApiRequest() {
    Json messages = Json::array();

    for (const auto& msg : messageHistory_) {
        Json m;

        switch (msg.role) {
            case MessageRole::System:
                m["role"] = "system";
                // If we have pre-built system blocks with cache_control,
                // serialize them as a JSON array so AnthropicClient can
                // detect and use them directly (instead of the flat string).
                if (systemBlocks_.has_value() && !systemBlocks_->empty()) {
                    Json contentArray = Json::array();
                    for (const auto& block : *systemBlocks_) {
                        contentArray.push_back(block.toJson());
                    }
                    m["content"] = contentArray;
                } else {
                    m["content"] = msg.content;
                }
                break;

            case MessageRole::User:
                m["role"] = "user";
                m["content"] = msg.content;
                break;

            case MessageRole::Assistant:
                m["role"] = "assistant";
                m["content"] = msg.content.empty() ? "" : msg.content;
                // Preserve thinking/signature for Anthropic format conversion
                if (msg.thinking) {
                    m["thinking"] = *msg.thinking;
                }
                if (msg.signature) {
                    m["signature"] = *msg.signature;
                }
                if (!msg.redactedThinking.empty()) {
                    m["redacted_thinking"] = msg.redactedThinking;
                }
                if (!msg.toolCalls.empty()) {
                    m["tool_calls"] = Json::array();
                    for (const auto& tc : msg.toolCalls) {
                        m["tool_calls"].push_back({
                            {"id", tc.id.empty() ? "call_0" : tc.id},
                            {"type", "function"},
                            {"function", {
                                {"name", tc.name.empty() ? "unknown" : tc.name},
                                {"arguments", tc.arguments.empty() ? "{}" : tc.arguments}
                            }}
                        });
                    }
                }
                break;

            case MessageRole::ToolResult:
                // OpenAI format: each tool result is a separate message
                for (const auto& tr : msg.toolResults) {
                    Json trMsg;
                    trMsg["role"] = "tool";
                    trMsg["tool_call_id"] = tr.callId.empty() ? "call_0" : tr.callId;
                    trMsg["content"] = tr.content.empty() ? "" : tr.content;
                    messages.push_back(trMsg);
                }
                continue; // skip the messages.push_back(m) at the end
        }

        messages.push_back(m);
    }

    Json req;
    req["messages"] = messages;

    // 转换工具定义为 JSON 数组 (根据 provider 格式)
    String provider = apiClient_.getProviderName();
    Json toolsJson = Json::array();
    for (const auto& def : tools_.toToolDefinitions()) {
        toolsJson.push_back(def.toJson(provider));
    }
    req["tools"] = toolsJson;

    // Apply per-agent overrides if set
    if (temperature_ >= 0) {
        apiClient_.setTemperature(temperature_);
    }
    if (maxTokensOverride_ > 0) {
        apiClient_.setMaxTokens(maxTokensOverride_);
    }

    return req;
}

void AgentLoop::notifyToolEvent(ToolEventPhase phase, const String& name,
                                const String& args, const String& result) {
    if (onToolEvent_) {
        onToolEvent_({phase, name, args, result});
    }
}

void AgentLoop::reset() {
    messageHistory_.clear();
    messageHistory_.push_back(Message::system(systemPrompt_));
}

void AgentLoop::replaceHistory(std::vector<Message> newHistory) {
    messageHistory_ = std::move(newHistory);
}

void AgentLoop::applyMicrocompact() {
    // Use MicroCompact for age-based tool result clearing
    int compacted = compact::MicroCompact::apply(messageHistory_);
    if (compacted > 0) {
        spdlog::info("Microcompact: cleared {} old tool result content fields", compacted);
    }

    // Context-pressure-based micro-compact: compact large results when window is filling
    double usageRatio = tokenTracker_.getUsagePercentage();
    if (usageRatio >= 0.70) {
        int pressureCompacted = compact::MicroCompact::applyByPressure(messageHistory_, usageRatio);
        if (pressureCompacted > 0) {
            compacted += pressureCompacted;
        }
    }

    // Also check for API streaming micro-compact (prompt >85% of context window)
    long promptTokens = tokenTracker_.getInputTokens();
    long apiContextWindow = tokenTracker_.getUsagePercentage() > 0
        ? static_cast<long>(tokenTracker_.getTotalTokens() / tokenTracker_.getUsagePercentage())
        : TokenTracker::DEFAULT_CONTEXT_WINDOW;
    if (apiContextWindow > 0 && compact::ApiMicroCompact::shouldTrigger(
            Usage{promptTokens, 0, 0}, static_cast<int>(apiContextWindow))) {
        long reclaimed = compact::ApiMicroCompact::compact(messageHistory_);
        if (reclaimed > 0) {
            spdlog::info("ApiMicroCompact: reclaimed ~{} tokens", reclaimed);
        }
    }
}

bool AgentLoop::applyAutoCompact() {
    // ========== Compact warning hook ==========
    long currentTokens = tokenTracker_.getInputTokens();
    long contextWindow = tokenTracker_.getUsagePercentage() > 0
        ? static_cast<long>(tokenTracker_.getTotalTokens() / tokenTracker_.getUsagePercentage())
        : TokenTracker::DEFAULT_CONTEXT_WINDOW;
    compactWarningHook_.check(currentTokens, contextWindow);

    // ========== Auto-compact: use AutoCompact class if initialized ==========
    if (autoCompact_ && autoCompact_->shouldTrigger(currentTokens)) {
        spdlog::info("Auto-compact triggered: usage at {:.1f}% of context window",
            static_cast<double>(currentTokens) / contextWindow * 100.0);

        auto newHistory = autoCompact_->compact(messageHistory_);
        if (newHistory) {
            // Post-compact cleanup
            compact::PostCompactCleanup::cleanup(*newHistory);

            // Extract session memory from compacted messages
            auto facts = compact::SessionMemoryCompact::extractKeyFacts(messageHistory_);
            if (!facts.empty()) {
                String memoryBlock = compact::SessionMemoryCompact::buildMemoryBlock(facts);
                spdlog::debug("Auto-compact: extracted {} key facts into memory block", facts.size());
                // Inject memory block as a user message before recent messages
                // Find the last user message position and insert before it
                if (newHistory->size() > 2) {
                    newHistory->insert(newHistory->end() - 2,
                        Message::user("[Session memory from prior conversation]\n" + memoryBlock));
                }
            }

            size_t oldSize = messageHistory_.size();
            messageHistory_ = std::move(*newHistory);

            // Adjust token tracker to prevent re-triggering
            // Estimate new input tokens from compressed history
            long estimatedNewTokens = 0;
            for (const auto& msg : messageHistory_) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
                for (const auto& tc : msg.toolCalls) {
                    estimatedNewTokens += static_cast<long>(tc.arguments.size()) / 4;
                }
                for (const auto& tr : msg.toolResults) {
                    estimatedNewTokens += static_cast<long>(tr.content.size()) / 4;
                }
            }
            tokenTracker_.adjustAfterCompaction(estimatedNewTokens);

            spdlog::info("Auto-compact completed: {} messages → {} messages",
                oldSize, messageHistory_.size());
            return true;
        }
    }

    // ========== Fallback: use LLM-based compaction when autoCompact_ is not initialized ==========
    if (!tokenTracker_.shouldAutoCompact()) {
        return false;
    }

    if (messageHistory_.size() <= 3) {
        spdlog::debug("Auto-compact: too few messages to compress");
        return false;
    }

    // Split history: compress old, keep recent
    size_t keepRecent = 5;
    if (messageHistory_.size() <= keepRecent + 1) {
        spdlog::debug("Auto-compact: not enough messages beyond recent to compress");
        return false;
    }

    std::vector<Message> toCompress(messageHistory_.begin() + 1,
        messageHistory_.end() - keepRecent);
    std::vector<Message> recentMsgs(messageHistory_.end() - keepRecent,
        messageHistory_.end());

    // Build compression prompt
    String compressText;
    for (const auto& msg : toCompress) {
        String role;
        switch (msg.role) {
            case MessageRole::User: role = "User"; break;
            case MessageRole::Assistant: role = "Assistant"; break;
            case MessageRole::ToolResult: role = "ToolResult"; break;
            default: role = "System"; break;
        }
        String content = msg.content;
        if (content.size() > 2000) {
            content = content.substr(0, 1997) + "...";
        }
        compressText += role + ": " + content + "\n\n";
    }

    String summaryPrompt = compact::CompactPrompt::getBasePrompt() +
        "\n\n<conversation>\n" + compressText + "\n</conversation>\n\n" +
        "Provide a detailed summary following the format above.";

    // Call LLM for summary
    Json summaryMessages = Json::array();
    summaryMessages.push_back({{"role", "user"}, {"content", summaryPrompt}});

    // Build tools list (no tools for summary)
    Json noTools = Json::array();

    auto llmResult = apiClient_.call(summaryMessages, noTools);
    if (!llmResult) {
        spdlog::warn("Auto-compact LLM call failed: {}", llmResult.error());
        return false;
    }

    String summary;
    if (llmResult->contains("choices") && !(*llmResult)["choices"].empty()) {
        summary = (*llmResult)["choices"][0]["message"]["content"].get<String>();
    } else if (llmResult->contains("content") && !(*llmResult)["content"].empty()) {
        auto& blocks = (*llmResult)["content"];
        for (const auto& block : blocks) {
            if (block.value("type", "") == "text") {
                summary += block["text"].get<String>();
            }
        }
    }

    if (summary.empty()) {
        spdlog::warn("Auto-compact: LLM returned empty summary");
        return false;
    }

    // Extract session memory from compacted messages
    auto facts = compact::SessionMemoryCompact::extractKeyFacts(toCompress);

    // Build new history
    std::vector<Message> newHistory;
    newHistory.push_back(messageHistory_[0]); // system prompt

    if (!facts.empty()) {
        String memoryBlock = compact::SessionMemoryCompact::buildMemoryBlock(facts);
        newHistory.push_back(Message::user(
            "[Session memory from prior conversation]\n" + memoryBlock));
        newHistory.push_back(Message::assistant(
            "I understand the session memory. I'll reference these facts as needed."));
    }

    newHistory.push_back(Message::user(
        "[Auto-compact: Summary of prior conversation]\n\n" + summary));
    newHistory.push_back(Message::assistant(
        "I understand the conversation summary. I'll continue from here with full context of what we've discussed."));

    for (const auto& msg : recentMsgs) {
        newHistory.push_back(msg);
    }

    // Post-compact cleanup on the new history
    compact::PostCompactCleanup::cleanup(newHistory);

    size_t oldSize = messageHistory_.size();
    messageHistory_ = std::move(newHistory);

    // Adjust token tracker
    long estimatedNewTokens = 0;
    for (const auto& msg : messageHistory_) {
        estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
    }
    tokenTracker_.adjustAfterCompaction(estimatedNewTokens);

    spdlog::info("Auto-compact (fallback) completed: {} messages → {} messages",
        oldSize, messageHistory_.size());

    return true;
}

bool AgentLoop::attemptReactiveCompact(long tokenGap) {
    if (reactiveCompactAttempts_ >= MAX_REACTIVE_COMPACT_ATTEMPTS) {
        spdlog::warn("Reactive compact: max attempts ({}) reached", MAX_REACTIVE_COMPACT_ATTEMPTS);
        return false;
    }

    reactiveCompactAttempts_++;
    spdlog::info("Reactive compact: attempt {}/{} (413 prompt-too-long recovery, token gap: {})",
        reactiveCompactAttempts_, MAX_REACTIVE_COMPACT_ATTEMPTS, tokenGap);

    // Force compact regardless of threshold
    if (autoCompact_) {
        auto newHistory = autoCompact_->compact(messageHistory_);
        if (newHistory) {
            compact::PostCompactCleanup::cleanup(*newHistory);
            messageHistory_ = std::move(*newHistory);

            // Adjust token tracker
            long estimatedNewTokens = 0;
            for (const auto& msg : messageHistory_) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
            }
            tokenTracker_.adjustAfterCompaction(estimatedNewTokens);
            return true;
        }
    }

    // Fallback: aggressive micro-compact
    int compacted = compact::MicroCompact::applyByPressure(messageHistory_, 0.50);
    if (compacted > 0) {
        spdlog::info("Reactive compact (micro): cleared {} tool results", compacted);
        return true;
    }

    return false;
}

void AgentLoop::addMissingToolResults() {
    if (messageHistory_.empty()) return;

    // Find the last assistant message with tool calls
    auto it = messageHistory_.rbegin();
    for (; it != messageHistory_.rend(); ++it) {
        if (it->role == MessageRole::Assistant && it->hasToolCalls()) {
            break;
        }
    }
    if (it == messageHistory_.rend()) return;

    // Check if the message after this assistant message is a tool_result
    auto assistantIdx = std::distance(messageHistory_.begin(), it.base()) - 1;
    bool hasToolResult = false;
    if (assistantIdx + 1 < static_cast<long>(messageHistory_.size())) {
        hasToolResult = (messageHistory_[assistantIdx + 1].role == MessageRole::ToolResult);
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
            messageHistory_.push_back(Message::toolResult(std::move(errorResults)));
            spdlog::info("Added {} synthetic error tool_results for unmatched tool_uses", errorResults.size());
        }
    }
}

void AgentLoop::stripThinkingFromHistory() {
    for (auto& msg : messageHistory_) {
        msg.thinking.reset();
        msg.signature.reset();
    }
    spdlog::debug("Stripped thinking/signature blocks from message history");
}

// ========== Stream Event Emission ==========

void AgentLoop::emitStreamEvent(StreamEvent event) {
    if (onStreamEvent_) {
        onStreamEvent_(event);
        return;
    }

    // Fallback: dispatch to individual callbacks when unified callback not set
    switch (event.type) {
        case StreamEvent::Type::StreamStart:
            if (onStreamStart_) onStreamStart_();
            break;
        case StreamEvent::Type::TextDelta:
            // Text deltas are handled by the onToken callback in streamingIteration
            // They go through a different path (direct onToken callback, not via emitStreamEvent)
            break;
        case StreamEvent::Type::ThinkingDelta:
            if (onThinking_) onThinking_(event.text);
            break;
        case StreamEvent::Type::ToolUseStart:
        case StreamEvent::Type::ToolUseComplete:
            // Tool events go through onToolEvent_ and onContentBlockStop_
            break;
        case StreamEvent::Type::ToolResultReady:
            if (onToolResult_) onToolResult_(event.toolName, event.toolResult, event.toolIsError);
            break;
        case StreamEvent::Type::Tombstone:
            // Tombstone events notify the UI that previous content is invalidated
            // No individual callback equivalent — only via onStreamEvent_
            break;
        case StreamEvent::Type::StreamEnd:
        case StreamEvent::Type::StreamError:
        case StreamEvent::Type::UserMessage:
        case StreamEvent::Type::SystemMessage:
        case StreamEvent::Type::ErrorMessage:
        case StreamEvent::Type::TurnDuration:
        case StreamEvent::Type::CompactBoundary:
        case StreamEvent::Type::HookSummary:
        case StreamEvent::Type::ToolChunkReady:
            // These have no individual callback equivalents — only via onStreamEvent_
            break;
    }
}

} // namespace claude
