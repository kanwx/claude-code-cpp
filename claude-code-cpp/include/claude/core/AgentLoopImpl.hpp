// Internal header — ONLY include from AgentLoop*.cpp files, never from public headers
#pragma once

#include <claude/core/AgentLoop.hpp>
#include <claude/core/StreamingToolExecutor.hpp>
#include <claude/core/HookManager.hpp>
#include <claude/core/compact/CompactService.hpp>
#include <claude/core/compact/AutoCompact.hpp>
#include <claude/core/compact/CompactWarningHook.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/ToolContext.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/api/ApiClient.hpp>
#include <claude/stream/TypedStreamEvent.hpp>
#include <claude/mcp/McpClient.hpp>
#include <claude/context/ContextInjector.hpp>
#include <claude/ui/MessagePipeline.hpp>
#include <mutex>
#include <optional>

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
    std::function<void(TypedStreamEvent&&)> onTypedEvent;
    std::function<void(StreamToolEvent&&)> onStreamToolEvent;

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

} // namespace claude
