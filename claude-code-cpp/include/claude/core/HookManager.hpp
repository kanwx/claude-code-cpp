#pragma once

#include "Types.hpp"
#include "../api/ApiClient.hpp"
#include <functional>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <mutex>

namespace claude {

// ========== Expanded Hook Types (29 events, matching TS original) ==========

enum class HookType {
    // Tool lifecycle
    PreToolUse,
    PostToolUse,
    PostToolUseFailure,

    // Session lifecycle
    SessionStart,
    SessionEnd,

    // Prompt/Response lifecycle
    UserPromptSubmit,
    PrePrompt,
    PostResponse,

    // Stop conditions
    Stop,
    StopFailure,

    // Sub-agent lifecycle
    SubagentStart,
    SubagentStop,

    // Compact lifecycle
    PreCompact,
    PostCompact,

    // Permission lifecycle
    PermissionRequest,
    PermissionDenied,

    // Setup
    Setup,

    // Task lifecycle
    TaskCreated,
    TaskCompleted,

    // Team/Swarm
    TeammateIdle,

    // MCP
    Elicitation,
    ElicitationResult,

    // Configuration
    ConfigChange,

    // Worktree
    WorktreeCreate,
    WorktreeRemove,

    // Context
    InstructionsLoaded,
    CwdChanged,
    FileChanged,

    // Notification
    Notification
};

// ========== Declarative Hook Configuration ==========

struct HookConfig {
    String id;
    HookType event;
    String type;  // "command" or "prompt"

    // For command hooks
    String command;
    std::optional<String> shell;
    std::optional<int> timeout;
    std::optional<String> statusMessage;

    // For prompt hooks
    String prompt;

    // Conditional execution
    std::optional<String> ifCondition;

    // Execution options
    bool once = false;
    bool async = false;
    bool asyncRewake = false;

    // State
    bool hasRun = false;

    static std::expected<HookConfig, String> fromJson(const Json& j, HookType eventType);
    Json toJson() const;
    bool matchesCondition(const struct HookContext& ctx) const;
};

// ========== Expanded Hook Context ==========

struct HookContext {
    // Core
    HookType eventType = HookType::PreToolUse;
    String toolName;
    Json input;
    std::optional<String> result;
    String error;

    // Session info
    String sessionId;

    // File change info
    std::vector<String> changedFiles;
    String oldCwd;
    String newCwd;

    // Task info
    String taskId;
    String taskDescription;

    // Sub-agent info
    String subagentId;
    String subagentType;

    // Permission info
    String permissionToolName;
    String permissionCommandPrefix;

    // Compact info
    long tokensBeforeCompact = 0;
    long tokensAfterCompact = 0;

    // Extras
    std::map<String, String> extras;

    // Permission override
    void setPermissionOverride(bool allow) {
        permissionOverride_ = allow ? PermissionOverride::ForceAllow
                                    : PermissionOverride::ForceDeny;
    }
    std::optional<bool> getPermissionOverride() const {
        switch (permissionOverride_) {
            case PermissionOverride::ForceAllow: return true;
            case PermissionOverride::ForceDeny: return false;
            case PermissionOverride::None: return std::nullopt;
        }
        return std::nullopt;
    }

    // Context injection
    void injectContext(const String& key, const String& value) {
        injectedContext_[key] = value;
    }
    const std::map<String, String>& getInjectedContext() const {
        return injectedContext_;
    }

    void setResult(const String& r) { result = r; }

    // Factory methods
    static HookContext forToolUse(HookType type, const String& toolName, const Json& input) {
        HookContext ctx;
        ctx.eventType = type;
        ctx.toolName = toolName;
        ctx.input = input;
        return ctx;
    }

    static HookContext forSession(HookType type, const String& sessionId) {
        HookContext ctx;
        ctx.eventType = type;
        ctx.sessionId = sessionId;
        return ctx;
    }

    static HookContext forSubagent(HookType type, const String& id, const String& subagentType) {
        HookContext ctx;
        ctx.eventType = type;
        ctx.subagentId = id;
        ctx.subagentType = subagentType;
        return ctx;
    }

    static HookContext forPermission(HookType type, const String& toolName, const String& prefix) {
        HookContext ctx;
        ctx.eventType = type;
        ctx.permissionToolName = toolName;
        ctx.permissionCommandPrefix = prefix;
        return ctx;
    }

    static HookContext forCompact(HookType type, long before, long after) {
        HookContext ctx;
        ctx.eventType = type;
        ctx.tokensBeforeCompact = before;
        ctx.tokensAfterCompact = after;
        return ctx;
    }

    static HookContext forFileChange(const std::vector<String>& files) {
        HookContext ctx;
        ctx.eventType = HookType::FileChanged;
        ctx.changedFiles = files;
        return ctx;
    }

    static HookContext forCwdChange(const String& oldCwd, const String& newCwd) {
        HookContext ctx;
        ctx.eventType = HookType::CwdChanged;
        ctx.oldCwd = oldCwd;
        ctx.newCwd = newCwd;
        return ctx;
    }

private:
    enum class PermissionOverride { None, ForceAllow, ForceDeny };
    PermissionOverride permissionOverride_ = PermissionOverride::None;
    std::map<String, String> injectedContext_;
};

// ========== Hook Result ==========

struct HookResult {
    enum Action { Continue, Abort, SuppressOutput } action = Continue;
    String reason;
    std::optional<String> output;
    std::optional<String> modifiedPrompt;

    static HookResult ok() { return {Continue}; }
    static HookResult abort(String r) { return {Abort, std::move(r)}; }
    static HookResult suppress() { return {SuppressOutput}; }

    bool shouldContinue() const { return action == Continue; }
    bool shouldAbort() const { return action == Abort; }
};

// ========== Hook Function Type ==========

using HookFn = std::function<HookResult(HookContext&)>;

// ========== Hook Manager ==========

class HookManager {
public:
    HookManager() = default;

    // Register code-based hook
    void registerHook(HookType type, HookFn hook) {
        std::lock_guard lock(hooksMutex_);
        codeHooks_[type].push_back(std::move(hook));
    }

    // Load declarative hooks from settings JSON
    std::expected<void, String> loadFromConfig(const Json& config);

    // Save current hook config
    Json saveToConfig() const;

    // Execute hooks for a given event type
    HookResult execute(HookType type, HookContext& ctx);

    // Execute a single command hook
    HookResult executeCommandHook(const HookConfig& config, HookContext& ctx);

    // Execute a single prompt hook (LLM evaluation)
    HookResult executePromptHook(const HookConfig& config, HookContext& ctx, ApiClient* client);

    // Clear
    void clearHooks(HookType type) {
        std::lock_guard lock(hooksMutex_);
        codeHooks_[type].clear();
        // Also remove declarative hooks for this type
        std::erase_if(declarativeHooks_, [type](const HookConfig& h) { return h.event == type; });
    }
    void clearAll() {
        std::lock_guard lock(hooksMutex_);
        codeHooks_.clear();
        declarativeHooks_.clear();
    }

    // Query
    bool hasHooks(HookType type) const;
    std::vector<HookConfig> getDeclarativeHooks() const {
        std::lock_guard lock(hooksMutex_);
        return declarativeHooks_;
    }

    // Set API client for prompt hooks
    void setApiClient(ApiClient* client) {
        std::lock_guard lock(hooksMutex_);
        apiClient_ = client;
    }

    // Map string event name to HookType
    static std::optional<HookType> parseEventType(const String& name);
    static String eventTypeToString(HookType type);

private:
    std::map<HookType, std::vector<HookFn>> codeHooks_;
    std::vector<HookConfig> declarativeHooks_;
    ApiClient* apiClient_ = nullptr;
    mutable std::mutex hooksMutex_;  // guards codeHooks_, declarativeHooks_

    bool evaluateCondition(const String& condition, const HookContext& ctx) const;
    HookResult runShellCommand(const String& command, const String& shell,
                               int timeout, HookContext& ctx) const;
    String substituteTemplateVars(const String& tmpl, const HookContext& ctx) const;
};

} // namespace claude
