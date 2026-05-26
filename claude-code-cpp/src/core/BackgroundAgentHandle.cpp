#include <claude/core/BackgroundAgentHandle.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/core/UnifiedTaskStore.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <spdlog/spdlog.h>

namespace claude {

std::atomic<int> BackgroundAgentHandle::activeCount_{0};

BackgroundAgentHandle::BackgroundAgentHandle(
    const String& prompt,
    const String& agentType,
    const AgentTypeDefinition& typeDef
) : prompt_(prompt), agentType_(agentType), typeDef_(typeDef),
    startTime_(std::chrono::steady_clock::now()) {}

std::shared_ptr<BackgroundAgentHandle> BackgroundAgentHandle::launch(
    const String& prompt,
    const String& agentType,
    const AgentTypeDefinition& typeDef,
    ApiClient& parentApiClient,
    const ParentContextSnapshot& parentSnapshot
) {
    // Check depth limit
    if (parentSnapshot.agentDepth >= MAX_DEPTH) {
        spdlog::warn("BackgroundAgentHandle: max depth {} reached, not launching", MAX_DEPTH);
        auto handle = std::shared_ptr<BackgroundAgentHandle>(
            new BackgroundAgentHandle(prompt, agentType, typeDef));
        handle->status_ = AgentRunStatus::Failed;
        handle->finalResult_ = "Error: Maximum agent nesting depth (" + std::to_string(MAX_DEPTH) + ") reached";
        return handle;
    }

    // Check concurrency limit
    int current = activeCount_.load(std::memory_order_acquire);
    if (current >= MAX_CONCURRENT) {
        spdlog::warn("BackgroundAgentHandle: max concurrent agents {} reached, not launching", MAX_CONCURRENT);
        auto handle = std::shared_ptr<BackgroundAgentHandle>(
            new BackgroundAgentHandle(prompt, agentType, typeDef));
        handle->status_ = AgentRunStatus::Failed;
        handle->finalResult_ = "Error: Maximum concurrent background agents (" + std::to_string(MAX_CONCURRENT) + ") reached";
        return handle;
    }

    auto handle = std::shared_ptr<BackgroundAgentHandle>(
        new BackgroundAgentHandle(prompt, agentType, typeDef));

    // Snapshot parent context — all value copies, no dangling references
    handle->snapshot_ = parentSnapshot;

    // Clone API client config into the handle before launching async
    // (we need parentApiClient reference only here in launch(), not in the thread)
    String provider = parentApiClient.getProviderName();
    String model = parentApiClient.getModelName();

    // Launch on background thread — captures only the handle's shared_ptr
    // The handle owns all state the thread needs via snapshot_ member
    handle->future_ = std::async(std::launch::async,
        [handle, provider = std::move(provider), model = std::move(model)]() {
            // Create isolated API client at thread start (no parent reference needed)
            std::unique_ptr<ApiClient> isolatedApi;
            if (provider == "anthropic") {
                isolatedApi = std::make_unique<AnthropicClient>();
            } else {
                isolatedApi = std::make_unique<OpenAIClient>();
            }
            isolatedApi->setModel(model);

            // Apply model override if specified
            if (!handle->snapshot_.modelOverride.empty()) {
                isolatedApi->setModel(handle->snapshot_.modelOverride);
                spdlog::debug("BackgroundAgentHandle: model override {} -> {}",
                              model, handle->snapshot_.modelOverride);
            }

            // Store in handle for runOnThread to use
            handle->runOnThreadWithApi(*isolatedApi);
        });

    return handle;
}

void BackgroundAgentHandle::runOnThreadWithApi(ApiClient& isolatedApi) {
    activeCount_.fetch_add(1, std::memory_order_relaxed);
    status_ = AgentRunStatus::Running;

    spdlog::debug("BackgroundAgentHandle: starting agent type='{}' depth={}",
        agentType_, snapshot_.agentDepth);

    // Create isolated tool registry with allowed tools
    auto isolatedRegistry = std::make_unique<ToolRegistry>();
    for (const auto& toolName : typeDef_.allowedTools) {
        auto tool = ToolRegistry::createToolByName(toolName);
        if (tool) {
            isolatedRegistry->registerTool(std::move(tool));
        } else {
            spdlog::warn("BackgroundAgentHandle: unknown tool '{}' for agent type '{}'", toolName, agentType_);
        }
    }

    // Create isolated token tracker
    auto isolatedTracker = std::make_unique<TokenTracker>();

    // Create isolated tool context from snapshot (no parent reference)
    ToolContext isolatedContext = ToolContext::create(snapshot_.workDir);
    isolatedContext.homeDir = snapshot_.homeDir;
    isolatedContext.debug = snapshot_.debug;
    isolatedContext.verbose = snapshot_.verbose;
    isolatedContext.nonInteractive = snapshot_.nonInteractive;
    isolatedContext.set("agentDepth", snapshot_.agentDepth + 1);
    isolatedContext.set("apiClient", static_cast<ApiClient*>(&isolatedApi));

    // Create agent loop
    auto isolatedAgent = std::make_unique<AgentLoop>(
        isolatedApi,
        *isolatedRegistry,
        typeDef_.systemPrompt,
        *isolatedTracker
    );

    // Apply agent-type-specific overrides
    isolatedAgent->setMaxIterations(typeDef_.maxIterations);
    isolatedAgent->setTemperature(typeDef_.temperature);
    isolatedAgent->setMaxTokensOverride(typeDef_.maxTokens);

    // Wire permission engine
    if (snapshot_.permissionEngine) {
        isolatedAgent->setPermissionEngine(snapshot_.permissionEngine);
    }

    // Wire permission callback
    if (snapshot_.permissionCallback) {
        isolatedAgent->setOnPermissionRequest(snapshot_.permissionCallback);
    } else {
        auto engine = snapshot_.permissionEngine;
        isolatedAgent->setOnPermissionRequest([engine](const PermissionRequest& req) {
            if (req.toolName == "Read" || req.toolName == "Glob" ||
                req.toolName == "Grep" || req.toolName == "WebFetch" ||
                req.toolName == "WebSearch" || req.toolName == "LSP") {
                return PermissionChoice::AllowOnce;
            }
            if (engine) {
                return PermissionChoice::AllowOnce;
            }
            return PermissionChoice::DenyOnce;
        });
    }

    // Run via runStreaming() with token accumulation
    auto weakSelf = std::weak_ptr<BackgroundAgentHandle>(shared_from_this());
    auto result = isolatedAgent->runStreaming(prompt_,
        [weakSelf](const String& token) {
            if (auto self = weakSelf.lock()) {
                std::lock_guard<std::mutex> lock(self->outputMutex_);
                self->accumulatedOutput_ += token;
            }
        });

    // Check for cancellation
    if (cancelRequested_.load(std::memory_order_acquire)) {
        status_ = AgentRunStatus::Cancelled;
        std::lock_guard<std::mutex> lock(outputMutex_);
        finalResult_ = "Agent cancelled by user";
        spdlog::debug("BackgroundAgentHandle: agent type='{}' cancelled", agentType_);
    } else if (result) {
        status_ = AgentRunStatus::Completed;
        std::lock_guard<std::mutex> lock(outputMutex_);
        finalResult_ = *result;
        totalTokens_ = isolatedTracker->getTotalTokens();
        spdlog::debug("BackgroundAgentHandle: agent type='{}' completed, {} tokens",
            agentType_, totalTokens_);
    } else {
        status_ = AgentRunStatus::Failed;
        std::lock_guard<std::mutex> lock(outputMutex_);
        finalResult_ = "Error: " + result.error();
        spdlog::debug("BackgroundAgentHandle: agent type='{}' failed: {}",
            agentType_, result.error());
    }

    // Automatically propagate result to UnifiedTaskStore
    // Find the task associated with this handle and update it
    auto& taskStore = UnifiedTaskStore::instance();
    auto tasks = taskStore.listTasks();
    for (const auto& task : tasks) {
        if (task.agentHandle.get() == this) {
            if (status_ == AgentRunStatus::Completed) {
                taskStore.setTaskResult(task.id, finalResult_, totalTokens_);
            } else if (status_ == AgentRunStatus::Failed || status_ == AgentRunStatus::Cancelled) {
                taskStore.setTaskError(task.id, finalResult_);
            }
            break;
        }
    }

    // Signal completion
    completionCv_.notify_all();

    activeCount_.fetch_sub(1, std::memory_order_relaxed);
}

bool BackgroundAgentHandle::waitForCompletion(int timeoutMs) {
    std::unique_lock<std::mutex> lock(completionMutex_);
    if (timeoutMs < 0) {
        completionCv_.wait(lock, [this] { return isDone(); });
        return true;
    }
    return completionCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
        [this] { return isDone(); });
}

String BackgroundAgentHandle::getResult() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return finalResult_;
}

String BackgroundAgentHandle::getAccumulatedOutput() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return accumulatedOutput_;
}

long BackgroundAgentHandle::getTotalTokens() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return totalTokens_;
}

void BackgroundAgentHandle::cancel() {
    cancelRequested_.store(true, std::memory_order_release);
}

} // namespace claude
