#include <claude/core/AgentLoopImpl.hpp>
#include <claude/api/RetryableClient.hpp>
#include <spdlog/spdlog.h>

namespace claude {

// ============================================================================
// Construction / Destruction
// ============================================================================

AgentLoop::AgentLoop(
    ApiClient& apiClient,
    ToolRegistry& tools,
    const String& systemPrompt
)
    : impl_(std::make_unique<Impl>(apiClient, tools, systemPrompt))
{
    impl_->toolContext = ToolContext::create(std::filesystem::current_path());
    impl_->toolContext.set("apiClient", static_cast<ApiClient*>(&impl_->apiClient));
    std::lock_guard lock(impl_->historyMutex);
    impl_->messageHistory.push_back(Message::system(systemPrompt));
}

AgentLoop::AgentLoop(
    ApiClient& apiClient,
    ToolRegistry& tools,
    const String& systemPrompt,
    TokenTracker& tokenTracker
)
    : impl_(std::make_unique<Impl>(apiClient, tools, systemPrompt))
{
    impl_->tokenTracker = std::move(tokenTracker);
    impl_->toolContext = ToolContext::create(std::filesystem::current_path());
    impl_->toolContext.set("apiClient", static_cast<ApiClient*>(&impl_->apiClient));
    std::lock_guard lock(impl_->historyMutex);
    impl_->messageHistory.push_back(Message::system(systemPrompt));
}

AgentLoop::~AgentLoop() = default;

// ============================================================================
// Blocking mode
// ============================================================================

std::expected<String, String> AgentLoop::run(const String& userInput) {
    injectContext(userInput);
    {
        std::lock_guard lock(impl_->stateMutex);
        impl_->currentUserInput = userInput;
    }
    resetCancel();
    return executeLoop(false, nullptr);
}

// ============================================================================
// Streaming mode
// ============================================================================

std::expected<String, String> AgentLoop::runStreaming(const String& userInput, OnToken onToken) {
    injectContext(userInput);
    {
        std::lock_guard lock(impl_->stateMutex);
        impl_->currentUserInput = userInput;
    }
    resetCancel();
    return executeLoop(true, onToken);
}

// ============================================================================
// Callback registration
// ============================================================================

void AgentLoop::setOnToolEvent(OnToolEvent callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onToolEvent = std::move(callback);
}

void AgentLoop::setOnPermissionRequest(
    std::function<PermissionChoice(const PermissionRequest&)> callback
) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onPermissionRequest = std::move(callback);
}

void AgentLoop::setOnStreamStart(OnStreamStart callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onStreamStart = std::move(callback);
}

void AgentLoop::setOnThinking(OnThinking callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onThinking = std::move(callback);
}

void AgentLoop::setOnAssistantMessage(std::function<void(const String&)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onAssistantMessage = std::move(callback);
}

void AgentLoop::setOnContentBlockStop(std::function<void(const String& type, int index, const String& content)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onContentBlockStop = std::move(callback);
}

void AgentLoop::setOnToolResult(std::function<void(const String& toolName, const String& result, bool isError)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onToolResult = std::move(callback);
}

void AgentLoop::initAutoCompact(int contextWindow) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->autoCompact.emplace(impl_->apiClient, contextWindow);
}

void AgentLoop::setOnLoopContinue(std::function<void(int iteration, int totalIterations)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onLoopContinue = std::move(callback);
}

void AgentLoop::setOnStreamEvent(std::function<void(const StreamEvent&)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onStreamEvent = std::move(callback);
}

void AgentLoop::setOnTypedEvent(std::function<void(TypedStreamEvent&&)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onTypedEvent = std::move(callback);
}

void AgentLoop::setOnStreamToolEvent(std::function<void(StreamToolEvent&&)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onStreamToolEvent = std::move(callback);
}

void AgentLoop::setOnStopHook(OnStopHook callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->onStopHook = std::move(callback);
}

void AgentLoop::setOnCompactWarning(std::function<void(int level, long currentTokens, long maxTokens)> callback) {
    std::lock_guard lock(impl_->callbackMutex);
    impl_->compactWarningHook.setCallback(std::move(callback));
}

// ============================================================================
// Per-agent overrides
// ============================================================================

void AgentLoop::setMaxIterations(int maxIter) { impl_->maxIterations = maxIter; }
int AgentLoop::getMaxIterations() const { return impl_->maxIterations; }

void AgentLoop::setTemperature(double temp) { impl_->temperature = temp; }
double AgentLoop::getTemperature() const { return impl_->temperature; }

void AgentLoop::setMaxTokensOverride(int maxTokens) { impl_->maxTokensOverride = maxTokens; }
int AgentLoop::getMaxTokensOverride() const { return impl_->maxTokensOverride; }

void AgentLoop::setTaskBudget(long budget) { impl_->tokenTracker.setTaskBudget(budget); }
long AgentLoop::getTaskBudget() const { return impl_->tokenTracker.getTaskBudget(); }
long AgentLoop::getTaskBudgetUsed() const { return impl_->tokenTracker.getTaskBudgetUsed(); }

void AgentLoop::setAllowedTools(std::vector<String> tools) {
    std::lock_guard lock(impl_->toolFilterMutex);
    impl_->allowedTools = std::move(tools);
}
const std::vector<String>& AgentLoop::getAllowedTools() const {
    std::lock_guard lock(impl_->toolFilterMutex);
    return impl_->allowedTools;
}

void AgentLoop::setDisallowedTools(std::vector<String> tools) {
    std::lock_guard lock(impl_->toolFilterMutex);
    impl_->disallowedTools = std::move(tools);
}
const std::vector<String>& AgentLoop::getDisallowedTools() const {
    std::lock_guard lock(impl_->toolFilterMutex);
    return impl_->disallowedTools;
}

void AgentLoop::setSystemBlocks(std::vector<TextBlockParam> blocks) {
    impl_->systemBlocks = std::move(blocks);
}

bool AgentLoop::hasSystemBlocks() const {
    return impl_->systemBlocks.has_value() && !impl_->systemBlocks->empty();
}

void AgentLoop::setContextInjector(ContextInjector* injector) {
    impl_->contextInjector = injector;
}

void AgentLoop::refreshContext() {
    // This is called between turns to allow the ContextInjector to
    // update dynamic state. The ContextInjector itself manages its
    // internal state (git status, CLAUDE.md) — this is just a hook
    // for the main loop to signal "a new turn is about to begin."
}

// ============================================================================
// Permission engine
// ============================================================================

void AgentLoop::setPermissionEngine(RuleEngine* engine) {
    impl_->permissionEngine = engine;
    if (engine) {
        impl_->toolContext.set("permissionEngine", engine);
    }
}

RuleEngine* AgentLoop::getPermissionEngine() const {
    return impl_->permissionEngine;
}

// ============================================================================
// Cognitive backend
// ============================================================================

void AgentLoop::setCognitiveBackend(std::shared_ptr<McpClient> mcpClient) {
    impl_->cognitiveMcpClient = std::move(mcpClient);
    // Auto-register cognitive tools
    impl_->tools.registerCognitiveTools(impl_->cognitiveMcpClient);
}

std::shared_ptr<McpClient> AgentLoop::getCognitiveBackend() const {
    return impl_->cognitiveMcpClient;
}

bool AgentLoop::hasCognitiveBackend() const {
    return impl_->cognitiveMcpClient != nullptr;
}

// ============================================================================
// Cancellation
// ============================================================================

void AgentLoop::cancel() {
    impl_->cancelled.store(true, std::memory_order_release);
    impl_->apiClient.abort();
    if (impl_->toolExecutor) {
        impl_->toolExecutor->cancel();
    }
    // Note: spdlog is NOT async-signal-safe, so we log from the
    // loop's cancel check point rather than here.
}

bool AgentLoop::isCancelled() const {
    return impl_->cancelled.load(std::memory_order_acquire);
}

void AgentLoop::resetCancel() {
    impl_->cancelled.store(false, std::memory_order_release);
    impl_->apiClient.resetAbort();
    if (impl_->toolExecutor) {
        // StreamingToolExecutor resets cancelled_ at the start of each execute() batch
    }
}

// ============================================================================
// State access
// ============================================================================

const std::vector<Message>& AgentLoop::getMessageHistory() const {
    std::lock_guard lock(impl_->historyMutex);
    return impl_->messageHistory;
}

TokenTracker& AgentLoop::getTokenTracker() {
    return impl_->tokenTracker;
}

const TokenTracker& AgentLoop::getTokenTracker() const {
    return impl_->tokenTracker;
}

const String& AgentLoop::getSystemPrompt() const {
    return impl_->systemPrompt;
}

ApiClient& AgentLoop::getApiClient() {
    return impl_->apiClient;
}

ToolContext& AgentLoop::getToolContext() {
    return impl_->toolContext;
}

HookManager& AgentLoop::getHookManager() {
    return impl_->hookManager;
}

// ============================================================================
// History management
// ============================================================================

void AgentLoop::reset() {
    std::lock_guard lock(impl_->historyMutex);
    impl_->messageHistory.clear();
    impl_->messageHistory.push_back(Message::system(impl_->systemPrompt));
}

void AgentLoop::replaceHistory(std::vector<Message> newHistory) {
    std::lock_guard lock(impl_->historyMutex);
    impl_->messageHistory = std::move(newHistory);
}

size_t AgentLoop::getMessageCount() const {
    std::lock_guard lock(impl_->historyMutex);
    return impl_->messageHistory.size();
}

// ============================================================================
// Core loop — TAOR: Think-Act-Observe-Repeat
// ============================================================================

std::expected<String, String> AgentLoop::executeLoop(bool streaming, OnToken onToken) {
    int iteration = 0;
    String lastAssistantText;
    int maxOutputTokensRecoveryCount = 0;
    {
        std::lock_guard lock(impl_->stateMutex);
        impl_->reactiveCompactAttempts = 0;
    }

    while (iteration < impl_->maxIterations) {
        iteration++;

        // Check cancellation at start of each iteration
        if (impl_->cancelled.load(std::memory_order_acquire)) {
            spdlog::debug("AgentLoop: cancelled at iteration {}", iteration);
            {
                auto cb = [&] {
                    std::lock_guard lock(impl_->callbackMutex);
                    return impl_->onCancelled;
                }();
                if (cb) cb();
            }
            // Trim any incomplete partial content from lastAssistantText
            if (!lastAssistantText.empty()) {
                return lastAssistantText;
            }
            return std::unexpected("Cancelled by user");
        }

        spdlog::debug("Agent loop iteration {} ({})", iteration, streaming ? "streaming" : "blocking");

        // Notify loop continue (TAOR Repeat phase)
        if (iteration > 1) {
            auto cb = [&] {
                std::lock_guard lock(impl_->callbackMutex);
                return impl_->onLoopContinue;
            }();
            if (cb) cb(iteration, impl_->maxIterations);
        }

        // Emit StreamStart at beginning of each iteration
        emitStreamEvent(StreamEvent{StreamEvent::Type::StreamStart});

        // Apply skill model override for this iteration (save/restore pattern)
        String savedModelForSkill;
        {
            std::lock_guard lock(impl_->toolFilterMutex);
            if (!impl_->pendingSkillModel.empty()) {
                savedModelForSkill = impl_->apiClient.getModelName();
                impl_->apiClient.setModel(impl_->pendingSkillModel);
            }
        }

        // Build request
        Json request = buildApiRequest();

        // Call API
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
            {
                std::lock_guard lock(impl_->historyMutex);
                impl_->messageHistory.push_back(Message::user(
                    "[System: Switched to " + fb.toModel + " due to high demand for " +
                    (fb.fromModel.empty() ? String("primary model") : fb.fromModel) + "]"));
                impl_->messageHistory.push_back(Message::assistant(
                    "Understood. I'll continue with " + fb.toModel + "."));
            }

            // 5. Reset recovery counters
            maxOutputTokensRecoveryCount = 0;
            {
                std::lock_guard lock(impl_->stateMutex);
                impl_->reactiveCompactAttempts = 0;
            }

            continue;  // Retry with fallback model
        } catch (const std::exception& e) {
            addMissingToolResults();
            return std::unexpected("API call failed: " + String(e.what()));
        }

        // Record usage
        if (result.usage.promptTokens > 0 || result.usage.completionTokens > 0) {
            impl_->tokenTracker.recordUsage(result.usage.promptTokens, result.usage.completionTokens);
            impl_->tokenTracker.recordTaskUsage(result.usage.promptTokens, result.usage.completionTokens);
        }

        // Restore model after skill override (if applied for this iteration)
        if (!savedModelForSkill.empty()) {
            impl_->apiClient.setModel(savedModelForSkill);
            std::lock_guard lock(impl_->toolFilterMutex);
            impl_->pendingSkillModel.clear();
        }
        // Clear skill tool restriction after use (applied in buildApiRequest)
        {
            std::lock_guard lock(impl_->toolFilterMutex);
            if (!impl_->pendingSkillTools.empty()) {
                impl_->pendingSkillTools.clear();
            }
        }

        // Check task budget
        if (impl_->tokenTracker.isTaskBudgetExceeded()) {
            spdlog::debug("Task budget exceeded: {}/{} tokens",
                impl_->tokenTracker.getTaskBudgetUsed(), impl_->tokenTracker.getTaskBudget());
            lastAssistantText += "\n\n[Task budget exceeded: " +
                std::to_string(impl_->tokenTracker.getTaskBudgetUsed()) + "/" +
                std::to_string(impl_->tokenTracker.getTaskBudget()) + " tokens used]";
            break;
        }

        // Check cancellation after API call returns
        // If cancelled mid-stream, the partial message may have incomplete content.
        // Don't add it to history — just return what we have.
        if (impl_->cancelled.load(std::memory_order_acquire)) {
            spdlog::debug("AgentLoop: cancelled after API iteration {}", iteration);
            {
                auto cb = [&] {
                    std::lock_guard lock(impl_->callbackMutex);
                    return impl_->onCancelled;
                }();
                if (cb) cb();
            }
            if (!result.message.content.empty()) {
                return result.message.content;
            }
            return std::unexpected("Cancelled by user");
        }

        // Add assistant message to history
        result.message.apiRound = iteration;
        {
            std::lock_guard lock(impl_->historyMutex);
            impl_->messageHistory.push_back(result.message);
        }

        // ========== Stop Hook ==========
        // When the model stops with end_turn (not max_tokens), run stop hooks.
        // Stop hooks can force the loop to continue (matching TS handleStopHooks).
        if (result.stopReason != "max_tokens" && result.stopReason != "length") {
            // First: fire PostResponse hook for backward compat
            HookContext hookCtx;
            hookCtx.toolName = "response";
            hookCtx.extras["stopReason"] = result.stopReason;
            hookCtx.extras["content"] = result.message.content;
            auto hookResult = impl_->hookManager.execute(HookType::PostResponse, hookCtx);
            if (hookResult.shouldAbort()) {
                spdlog::debug("PostResponse hook aborted the loop");
                String reason = hookCtx.extras.count("reason") ? hookCtx.extras["reason"] : "Hook blocked continuation";
                lastAssistantText += "\n\n[Stopped by hook: " + reason + "]";
                break;
            }

            // Then: run Stop hook (can force continuation)
            {
                auto cb = [&] {
                    std::lock_guard lock(impl_->callbackMutex);
                    return impl_->onStopHook;
                }();
                if (cb) {
                    auto stopResult = cb();
                    if (stopResult.shouldContinue) {
                        spdlog::debug("Stop hook forced continuation: {}", stopResult.reason);
                        std::lock_guard lock(impl_->historyMutex);
                        impl_->messageHistory.push_back(Message::user(
                            "[System: Continue your work. " + stopResult.reason + "]"));
                        continue;
                    }
                }
            }
        }

        // Extract text
        if (!result.message.content.empty()) {
            lastAssistantText = result.message.content;
            if (!streaming) {
                auto cb = [&] {
                    std::lock_guard lock(impl_->callbackMutex);
                    return impl_->onAssistantMessage;
                }();
                if (cb) cb(lastAssistantText);
            }
        }

        // ========== max_output_tokens recovery mechanism ==========
        // When the model is truncated by max_tokens, auto-continue instead of terminating
        // Matches original TS recovery loop behavior
        if (result.stopReason == "max_tokens" || result.stopReason == "length") {
            maxOutputTokensRecoveryCount++;
            if (maxOutputTokensRecoveryCount <= MAX_OUTPUT_TOKENS_RECOVERY) {
                spdlog::debug("max_output_tokens reached, recovery iteration {}/{}",
                    maxOutputTokensRecoveryCount, MAX_OUTPUT_TOKENS_RECOVERY);

                // Escalate max_tokens on 2nd+ recovery attempt
                if (maxOutputTokensRecoveryCount >= 2 && impl_->maxTokensOverride < ESCALATED_MAX_TOKENS) {
                    impl_->maxTokensOverride = ESCALATED_MAX_TOKENS;
                    spdlog::debug("Escalating max_tokens to {} for recovery", ESCALATED_MAX_TOKENS);
                }

                // Add continuation instruction (matching original TS: "Resume directly — no apology, no recap")
                {
                    std::lock_guard lock(impl_->historyMutex);
                    impl_->messageHistory.push_back(Message::assistant(
                        "Continue from where you left off. Do not repeat what you already wrote. "
                        "Resume directly — no apology, no recap."
                    ));
                }
                continue;  // TAOR: Repeat
            }
            // Exceeded recovery attempts, add truncation warning and end
            lastAssistantText += "\n\n[Output truncated: max tokens reached]";
            break;
        }

        // No tool calls → end
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
        if (impl_->cancelled.load(std::memory_order_acquire)) {
            spdlog::debug("AgentLoop: cancelled after tool execution at iteration {}", iteration);
            {
                auto cb = [&] {
                    std::lock_guard lock(impl_->callbackMutex);
                    return impl_->onCancelled;
                }();
                if (cb) cb();
            }
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
            spdlog::debug("SkillTool detected: model='{}', tools={}, prompt={} chars",
                pendingSkillModel, pendingSkillTools.size(), pendingSkillPrompt.size());
        }

        // Add tool results to history
        {
            auto toolMsg = Message::toolResult(std::move(toolResponses));
            toolMsg.apiRound = iteration;
            std::lock_guard lock(impl_->historyMutex);
            impl_->messageHistory.push_back(std::move(toolMsg));
        }

        // Inject skill prompt as a new user message
        if (!pendingSkillPrompt.empty()) {
            {
                std::lock_guard lock(impl_->historyMutex);
                impl_->messageHistory.push_back(Message::user(pendingSkillPrompt));
            }

            // Store skill model override — will be applied in the next executeLoop iteration
            if (!pendingSkillModel.empty()) {
                std::lock_guard lock(impl_->toolFilterMutex);
                impl_->pendingSkillModel = std::move(pendingSkillModel);
                spdlog::debug("Skill model override: {}", impl_->pendingSkillModel);
            }

            // Store skill tool restriction — will be applied in buildApiRequest()
            if (!pendingSkillTools.empty()) {
                std::lock_guard lock(impl_->toolFilterMutex);
                impl_->pendingSkillTools = std::move(pendingSkillTools);
                spdlog::debug("Skill tool restriction: {} tools", impl_->pendingSkillTools.size());
            }

            spdlog::debug("Skill prompt injected as user message");
        }

        // ========== Micro-compact: clear expired tool results ==========
        applyMicrocompact();

        // ========== Auto-compact: compact when context window exceeds 93% ==========
        applyAutoCompact();

        // TAOR: loop continue — next Think round
    }

    if (iteration >= impl_->maxIterations) {
        spdlog::warn("Agent loop reached max iterations {}", impl_->maxIterations);
        lastAssistantText += "\n\n[WARNING: Maximum loop iteration limit reached]";
    }

    // Reset escalated max_tokens to default — only reset if we escalated
    // (don't reset user-set overrides that weren't from recovery escalation)
    if (impl_->maxTokensOverride > 0 && impl_->maxTokensOverride == ESCALATED_MAX_TOKENS) {
        impl_->apiClient.setMaxTokens(16384);
        impl_->maxTokensOverride = -1;  // Reset override
    }

    return lastAssistantText;
}

// ============================================================================
// API request building
// ============================================================================

Json AgentLoop::buildApiRequest() {
    Json messages = Json::array();

    {
        // Snapshot messageHistory under lock to iterate safely.
        // vector can be reallocated by concurrent push_back.
        std::lock_guard lock(impl_->historyMutex);
        for (const auto& msg : impl_->messageHistory) {
            Json m;

            switch (msg.role) {
                case MessageRole::System:
                    m["role"] = "system";
                    // If we have pre-built system blocks with cache_control,
                    // serialize them as a JSON array so AnthropicClient can
                    // detect and use them directly (instead of the flat string).
                    if (impl_->systemBlocks.has_value() && !impl_->systemBlocks->empty()) {
                        Json contentArray = Json::array();
                        for (const auto& block : *impl_->systemBlocks) {
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
    }

    Json req;
    req["messages"] = messages;

    // Convert tool definitions to JSON array (per provider format)
    String provider = impl_->apiClient.getProviderName();
    Json toolsJson = Json::array();

    // Snapshot tool filter lists under lock
    std::vector<String> localPendingSkillTools;
    std::vector<String> localAllowedTools;
    std::vector<String> localDisallowedTools;
    {
        std::lock_guard lock(impl_->toolFilterMutex);
        localPendingSkillTools = impl_->pendingSkillTools;
        localAllowedTools = impl_->allowedTools;
        localDisallowedTools = impl_->disallowedTools;
    }

    for (const auto& def : impl_->tools.toToolDefinitions()) {
        String toolName = def.name;

        // Apply tool filtering: allowedTools, disallowedTools, and skill-restricted tools
        bool excluded = false;

        // Skill tool restriction takes precedence: only these tools are available this turn
        if (!localPendingSkillTools.empty()) {
            bool found = false;
            for (const auto& allowed : localPendingSkillTools) {
                if (allowed == toolName) { found = true; break; }
            }
            if (!found) excluded = true;
        }

        // Static allowedTools filter (from --allowedTools or agent config)
        if (!excluded && !localAllowedTools.empty()) {
            bool found = false;
            for (const auto& allowed : localAllowedTools) {
                if (allowed == toolName) { found = true; break; }
            }
            if (!found) excluded = true;
        }

        // Disallowed tools filter (from --disallowedTools)
        if (!excluded) {
            for (const auto& disallowed : localDisallowedTools) {
                if (disallowed == toolName) { excluded = true; break; }
            }
        }

        if (!excluded) {
            toolsJson.push_back(def.toJson(provider));
        }
    }
    req["tools"] = toolsJson;

    // Apply per-agent overrides if set
    if (impl_->temperature >= 0) {
        impl_->apiClient.setTemperature(impl_->temperature);
    }
    if (impl_->maxTokensOverride > 0) {
        impl_->apiClient.setMaxTokens(impl_->maxTokensOverride);
    }

    return req;
}

// ============================================================================
// Context injection
// ============================================================================

void AgentLoop::injectContext(const String& userInput) {
    if (!impl_->contextInjector) {
        std::lock_guard lock(impl_->historyMutex);
        impl_->messageHistory.push_back(Message::user(userInput));
        return;
    }

    auto ctx = impl_->contextInjector->buildContext(userInput);
    String contextPrefix = impl_->contextInjector->formatAsMessageContent(ctx);

    if (contextPrefix.empty()) {
        std::lock_guard lock(impl_->historyMutex);
        impl_->messageHistory.push_back(Message::user(userInput));
    } else {
        // Inject context as a system-reminder user message prefix, matching TS behavior
        String fullContent = contextPrefix + userInput;
        std::lock_guard lock(impl_->historyMutex);
        impl_->messageHistory.push_back(Message::user(fullContent));
    }

    // Clear per-turn attachments (not system-level context like git/claudeMd)
    impl_->contextInjector->clearAttachments();
}

// ============================================================================
// Helper methods
// ============================================================================

void AgentLoop::notifyToolEvent(ToolEventPhase phase, const String& name,
                                const String& args, const String& result) {
    auto cb = [&] {
        std::lock_guard lock(impl_->callbackMutex);
        return impl_->onToolEvent;
    }();
    if (cb) {
        cb({phase, name, args, result});
    }
}

// ============================================================================
// Interleaved tool execution (private)
// ============================================================================

void AgentLoop::setInterleaveToolExecution(bool enable) {
    std::lock_guard lock(impl_->stateMutex);
    impl_->interleaveToolExecution = enable;
}
bool AgentLoop::isInterleaveToolExecution() const {
    std::lock_guard lock(impl_->stateMutex);
    return impl_->interleaveToolExecution;
}

// ============================================================================
// History cleanup
// ============================================================================

void AgentLoop::stripThinkingFromHistory() {
    std::lock_guard lock(impl_->historyMutex);
    for (auto& msg : impl_->messageHistory) {
        msg.thinking.reset();
        msg.signature.reset();
    }
    spdlog::debug("Stripped thinking/signature blocks from message history");
}

void AgentLoop::extractThinkingContent(const Json& /*response*/) {
    // Placeholder — thinking extraction is handled inline in streamingIteration
}

// ============================================================================
// Stream Event Emission
// ============================================================================

void AgentLoop::emitStreamEvent(StreamEvent event) {
    // Copy all relevant callbacks under one lock, then invoke outside lock
    std::function<void(const StreamEvent&)> localStreamEvent;
    std::function<void()> localStreamStart;
    std::function<void(const String&)> localThinking;
    std::function<void(const String&, const String&, bool)> localToolResult;
    {
        std::lock_guard lock(impl_->callbackMutex);
        localStreamEvent = impl_->onStreamEvent;
        if (!localStreamEvent) {
            // Snapshot individual callbacks for fallback dispatch
            localStreamStart = impl_->onStreamStart;
            localThinking = impl_->onThinking;
            localToolResult = impl_->onToolResult;
        }
    }

    if (localStreamEvent) {
        localStreamEvent(event);
        return;
    }

    // Fallback: dispatch to individual callbacks when unified callback not set
    switch (event.type) {
        case StreamEvent::Type::StreamStart:
            if (localStreamStart) localStreamStart();
            break;
        case StreamEvent::Type::TextDelta:
            // Text deltas are handled by the onToken callback in streamingIteration
            // They go through a different path (direct onToken callback, not via emitStreamEvent)
            break;
        case StreamEvent::Type::ThinkingDelta:
            if (localThinking) localThinking(event.text);
            break;
        case StreamEvent::Type::ToolUseStart:
        case StreamEvent::Type::ToolUseComplete:
            // Tool events go through onToolEvent and onContentBlockStop
            break;
        case StreamEvent::Type::ToolResultReady:
            if (localToolResult) localToolResult(event.toolName, event.toolResult, event.toolIsError);
            break;
        case StreamEvent::Type::Tombstone:
            // Tombstone events notify the UI that previous content is invalidated
            // No individual callback equivalent — only via onStreamEvent
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
            // These have no individual callback equivalents — only via onStreamEvent
            break;
    }
}

} // namespace claude
