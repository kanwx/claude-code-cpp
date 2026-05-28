#include <claude/core/AgentLoop.hpp>
#include <claude/core/StreamingToolExecutor.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/core/compact/MicroCompact.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/compact/SessionMemoryCompact.hpp>
#include <claude/core/compact/MessageGrouping.hpp>
#include <claude/core/compact/ApiMicroCompact.hpp>
#include <claude/tool/ResultTruncation.hpp>
#include <claude/api/RetryableClient.hpp>
#include <claude/core/compact/CompactService.hpp>
#include <claude/core/compact/AutoCompact.hpp>
#include <claude/core/compact/CompactWarningHook.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/api/ApiClient.hpp>
#include <claude/mcp/McpClient.hpp>
#include <claude/context/ContextInjector.hpp>
#include <spdlog/spdlog.h>

namespace claude {

// ============================================================================
// pImpl — all private state lives here, breaking transitive includes
// ============================================================================

struct AgentLoop::Impl {
    // Core dependencies
    ApiClient& apiClient;
    ToolRegistry& tools;
    String systemPrompt;
    std::optional<std::vector<TextBlockParam>> systemBlocks;
    TokenTracker tokenTracker;
    ToolContext toolContext;
    HookManager hookManager;
    compact::CompactService compactService;
    std::optional<compact::AutoCompact> autoCompact;
    compact::CompactWarningHook compactWarningHook;

    // Cognitive backend
    std::shared_ptr<McpClient> cognitiveMcpClient;

    // Permission engine
    RuleEngine* permissionEngine = nullptr;

    // StreamingToolExecutor
    std::optional<StreamingToolExecutor> toolExecutor;

    // Message history
    std::vector<Message> messageHistory;

    // Callbacks
    OnToolEvent onToolEvent;
    std::function<PermissionChoice(const PermissionRequest&)> onPermissionRequest;
    OnStreamStart onStreamStart;
    OnThinking onThinking;
    std::function<void(const String&)> onAssistantMessage;
    std::function<void(const String& type, int index, const String& content)> onContentBlockStop;
    std::function<void(const String& toolName, const String& result, bool isError)> onToolResult;
    std::function<void(int iteration, int totalIterations)> onLoopContinue;
    std::function<void()> onCancelled;
    OnStopHook onStopHook;
    std::function<void(const StreamEvent&)> onStreamEvent;

    // Cancellation
    std::atomic<bool> cancelled{false};

    // Per-agent overrides
    int maxIterations = AgentLoop::DEFAULT_MAX_ITERATIONS;
    double temperature = -1;
    int maxTokensOverride = -1;

    // Reactive compact
    int reactiveCompactAttempts = 0;

    // Context injection
    ContextInjector* contextInjector = nullptr;

    // Tool filtering
    std::vector<String> allowedTools;
    std::vector<String> disallowedTools;
    std::vector<String> pendingSkillTools;
    String pendingSkillModel;

    // Interleaved execution
    bool interleaveToolExecution = false;

    // Current user input
    String currentUserInput;

    // Synchronization
    mutable std::mutex historyMutex;
    mutable std::mutex callbackMutex;
    mutable std::mutex toolFilterMutex;
    mutable std::mutex stateMutex;

    // Constructor
    Impl(ApiClient& api, ToolRegistry& reg, const String& prompt)
        : apiClient(api), tools(reg), systemPrompt(prompt) {}
};

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
            spdlog::info("Task budget exceeded: {}/{} tokens",
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
                spdlog::info("PostResponse hook aborted the loop");
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
                        spdlog::info("Stop hook forced continuation: {}", stopResult.reason);
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
                spdlog::info("max_output_tokens reached, recovery iteration {}/{}",
                    maxOutputTokensRecoveryCount, MAX_OUTPUT_TOKENS_RECOVERY);

                // Escalate max_tokens on 2nd+ recovery attempt
                if (maxOutputTokensRecoveryCount >= 2 && impl_->maxTokensOverride < ESCALATED_MAX_TOKENS) {
                    impl_->maxTokensOverride = ESCALATED_MAX_TOKENS;
                    spdlog::info("Escalating max_tokens to {} for recovery", ESCALATED_MAX_TOKENS);
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
            spdlog::info("SkillTool detected: model='{}', tools={}, prompt={} chars",
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
                spdlog::info("Skill model override: {}", impl_->pendingSkillModel);
            }

            // Store skill tool restriction — will be applied in buildApiRequest()
            if (!pendingSkillTools.empty()) {
                std::lock_guard lock(impl_->toolFilterMutex);
                impl_->pendingSkillTools = std::move(pendingSkillTools);
                spdlog::info("Skill tool restriction: {} tools", impl_->pendingSkillTools.size());
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
                    auto cb = [&] {
                        std::lock_guard lock2(impl_->callbackMutex);
                        return impl_->onThinking;
                    }();
                    if (cb) cb(thinking);
                }
            } else if (deltaType == "signature_delta") {
                String sig = delta.contains("signature") && delta["signature"].is_string()
                    ? delta["signature"].get<String>() : "";
                if (thinkingBlocks.contains(index)) {
                    thinkingBlocks[index].second += sig;
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
            // Skip UI callbacks for redacted_thinking entirely.
            if (blockType == "redacted_thinking") {
                // Already captured in redactedThinkingBlocks at content_block_start
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
                            }
                            textBuffer += text;
                            if (onToken) onToken(text);

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

    // Collect interleaved execution results
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
    if (impl_->cancelled.load(std::memory_order_acquire) && toolCalls.empty()) {
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
        spdlog::info("Tool [{}] result size {} exceeds budget {}, truncating",
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
// Compact operations
// ============================================================================

void AgentLoop::applyMicrocompact() {
    std::lock_guard lock(impl_->historyMutex);
    // Use MicroCompact for age-based tool result clearing
    int compacted = compact::MicroCompact::apply(impl_->messageHistory);
    if (compacted > 0) {
        spdlog::info("Microcompact: cleared {} old tool result content fields", compacted);
    }

    // Context-pressure-based micro-compact: compact large results when window is filling
    double usageRatio = impl_->tokenTracker.getUsagePercentage();
    if (usageRatio >= 0.70) {
        int pressureCompacted = compact::MicroCompact::applyByPressure(impl_->messageHistory, usageRatio);
        if (pressureCompacted > 0) {
            compacted += pressureCompacted;
        }
    }

    // Also check for API streaming micro-compact (prompt >85% of context window)
    long promptTokens = impl_->tokenTracker.getInputTokens();
    long apiContextWindow = impl_->tokenTracker.getUsagePercentage() > 0
        ? static_cast<long>(impl_->tokenTracker.getTotalTokens() / impl_->tokenTracker.getUsagePercentage())
        : TokenTracker::DEFAULT_CONTEXT_WINDOW;
    if (apiContextWindow > 0 && compact::ApiMicroCompact::shouldTrigger(
            Usage{promptTokens, 0, 0}, static_cast<int>(apiContextWindow))) {
        long reclaimed = compact::ApiMicroCompact::compact(impl_->messageHistory);
        if (reclaimed > 0) {
            spdlog::info("ApiMicroCompact: reclaimed ~{} tokens", reclaimed);
        }
    }
}

bool AgentLoop::applyAutoCompact() {
    // ========== Compact warning hook ==========
    long currentTokens = impl_->tokenTracker.getInputTokens();
    long contextWindow = impl_->tokenTracker.getUsagePercentage() > 0
        ? static_cast<long>(impl_->tokenTracker.getTotalTokens() / impl_->tokenTracker.getUsagePercentage())
        : TokenTracker::DEFAULT_CONTEXT_WINDOW;
    impl_->compactWarningHook.check(currentTokens, contextWindow);

    // ========== Auto-compact: use AutoCompact class if initialized ==========
    // autoCompact is set once via initAutoCompact() before the loop starts,
    // so reading it without lock is safe. We guard the check under callbackMutex
    // to be formally correct with respect to concurrent initAutoCompact() calls.
    bool hasAutoCompact = false;
    {
        std::lock_guard lock(impl_->callbackMutex);
        hasAutoCompact = impl_->autoCompact.has_value();
    }
    if (hasAutoCompact && impl_->autoCompact->shouldTrigger(currentTokens)) {
        spdlog::info("Auto-compact triggered: usage at {:.1f}% of context window",
            static_cast<double>(currentTokens) / contextWindow * 100.0);

        std::vector<Message> historySnapshot;
        {
            std::lock_guard lock(impl_->historyMutex);
            historySnapshot = impl_->messageHistory;
        }
        auto newHistory = impl_->autoCompact->compact(historySnapshot);
        if (newHistory) {
            // Post-compact cleanup
            compact::PostCompactCleanup::cleanup(*newHistory);

            // Extract session memory from compacted messages
            auto facts = compact::SessionMemoryCompact::extractKeyFacts(historySnapshot);
            if (!facts.empty()) {
                String memoryBlock = compact::SessionMemoryCompact::buildMemoryBlock(facts);
                spdlog::debug("Auto-compact: extracted {} key facts into memory block", facts.size());
                if (newHistory->size() > 2) {
                    newHistory->insert(newHistory->end() - 2,
                        Message::user("[Session memory from prior conversation]\n" + memoryBlock));
                }
            }

            std::lock_guard lock(impl_->historyMutex);
            size_t oldSize = impl_->messageHistory.size();
            impl_->messageHistory = std::move(*newHistory);

            long estimatedNewTokens = 0;
            for (const auto& msg : impl_->messageHistory) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
                for (const auto& tc : msg.toolCalls) {
                    estimatedNewTokens += static_cast<long>(tc.arguments.size()) / 4;
                }
                for (const auto& tr : msg.toolResults) {
                    estimatedNewTokens += static_cast<long>(tr.content.size()) / 4;
                }
            }
            impl_->tokenTracker.adjustAfterCompaction(estimatedNewTokens);

            spdlog::info("Auto-compact completed: {} messages -> {} messages",
                oldSize, impl_->messageHistory.size());
            return true;
        }
    }

    // ========== Fallback: use LLM-based compaction when autoCompact is not initialized ==========
    if (!impl_->tokenTracker.shouldAutoCompact()) {
        return false;
    }

    // Snapshot history under lock, then release lock for the API call
    std::vector<Message> toCompressSnapshot;
    std::vector<Message> recentMsgsSnapshot;
    Message systemPromptMsg;
    {
        std::lock_guard lock(impl_->historyMutex);
        if (impl_->messageHistory.size() <= 3) {
            spdlog::debug("Auto-compact: too few messages to compress");
            return false;
        }

        size_t keepRecent = 5;
        if (impl_->messageHistory.size() <= keepRecent + 1) {
            spdlog::debug("Auto-compact: not enough messages beyond recent to compress");
            return false;
        }

        systemPromptMsg = impl_->messageHistory[0];
        toCompressSnapshot.assign(impl_->messageHistory.begin() + 1,
            impl_->messageHistory.end() - keepRecent);
        recentMsgsSnapshot.assign(impl_->messageHistory.end() - keepRecent,
            impl_->messageHistory.end());
    }

    // Build compression prompt from snapshot (no lock needed)
    String compressText;
    for (const auto& msg : toCompressSnapshot) {
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

    // Call LLM for summary (no lock during API call)
    Json summaryMessages = Json::array();
    summaryMessages.push_back({{"role", "user"}, {"content", summaryPrompt}});

    Json noTools = Json::array();

    auto llmResult = impl_->apiClient.call(summaryMessages, noTools);
    if (!llmResult) {
        spdlog::warn("Auto-compact LLM call failed: {}", llmResult.error());
        return false;
    }

    String summary;
    if (llmResult->contains("choices") && (*llmResult)["choices"].is_array() && !(*llmResult)["choices"].empty()) {
        const auto& firstChoice = (*llmResult)["choices"][0];
        if (firstChoice.is_object() && firstChoice.contains("message") && firstChoice["message"].is_object()
            && firstChoice["message"].contains("content") && firstChoice["message"]["content"].is_string()) {
            summary = firstChoice["message"]["content"].get<String>();
        }
    } else if (llmResult->contains("content") && (*llmResult)["content"].is_array() && !(*llmResult)["content"].empty()) {
        const auto& blocks = (*llmResult)["content"];
        for (const auto& block : blocks) {
            if (block.is_object() && block.value("type", "") == "text"
                && block.contains("text") && block["text"].is_string()) {
                summary += block["text"].get<String>();
            }
        }
    }

    if (summary.empty()) {
        spdlog::warn("Auto-compact: LLM returned empty summary");
        return false;
    }

    // Extract session memory from compacted messages
    auto facts = compact::SessionMemoryCompact::extractKeyFacts(toCompressSnapshot);

    // Build new history
    std::vector<Message> newHistory;
    newHistory.push_back(systemPromptMsg);

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

    for (const auto& msg : recentMsgsSnapshot) {
        newHistory.push_back(msg);
    }

    // Post-compact cleanup on the new history
    compact::PostCompactCleanup::cleanup(newHistory);

    {
        std::lock_guard lock(impl_->historyMutex);
        size_t oldSize = impl_->messageHistory.size();
        impl_->messageHistory = std::move(newHistory);

        // Adjust token tracker
        long estimatedNewTokens = 0;
        for (const auto& msg : impl_->messageHistory) {
            estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
        }
        impl_->tokenTracker.adjustAfterCompaction(estimatedNewTokens);

        spdlog::info("Auto-compact (fallback) completed: {} messages -> {} messages",
            oldSize, impl_->messageHistory.size());
    }

    return true;
}

bool AgentLoop::attemptReactiveCompact(long tokenGap) {
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->reactiveCompactAttempts >= MAX_REACTIVE_COMPACT_ATTEMPTS) {
            spdlog::warn("Reactive compact: max attempts ({}) reached", MAX_REACTIVE_COMPACT_ATTEMPTS);
            return false;
        }

        impl_->reactiveCompactAttempts++;
        spdlog::info("Reactive compact: attempt {}/{} (413 prompt-too-long recovery, token gap: {})",
            impl_->reactiveCompactAttempts, MAX_REACTIVE_COMPACT_ATTEMPTS, tokenGap);
    }

    // Force compact regardless of threshold
    if (impl_->autoCompact) {
        std::vector<Message> historySnapshot;
        {
            std::lock_guard lock(impl_->historyMutex);
            historySnapshot = impl_->messageHistory;
        }
        auto newHistory = impl_->autoCompact->compact(historySnapshot);
        if (newHistory) {
            compact::PostCompactCleanup::cleanup(*newHistory);
            std::lock_guard lock(impl_->historyMutex);
            impl_->messageHistory = std::move(*newHistory);

            // Adjust token tracker
            long estimatedNewTokens = 0;
            for (const auto& msg : impl_->messageHistory) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
            }
            impl_->tokenTracker.adjustAfterCompaction(estimatedNewTokens);
            return true;
        }
    }

    // Fallback: aggressive micro-compact
    {
        std::lock_guard lock(impl_->historyMutex);
        int compacted = compact::MicroCompact::applyByPressure(impl_->messageHistory, 0.50);
        if (compacted > 0) {
            spdlog::info("Reactive compact (micro): cleared {} tool results", compacted);
            return true;
        }
    }

    return false;
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
            spdlog::info("Added {} synthetic error tool_results for unmatched tool_uses", errorResults.size());
        }
    }
}

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
