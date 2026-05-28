#pragma once

#include "../core/Types.hpp"
#include "MetricsState.hpp"
#include "PermissionState.hpp"
#include "UIState.hpp"
#include "SessionState.hpp"
#include <chrono>
#include <map>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace claude {

/// Invoked skill info (matches TS InvokedSkillInfo)
struct InvokedSkillInfo {
    String skillName;
    String skillPath;
    String content;
    String agentId;
    std::chrono::steady_clock::time_point invokedAt = std::chrono::steady_clock::now();
};

/// Slow operation record
struct SlowOperation {
    String operation;
    double durationMs = 0.0;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
};

/// In-memory error log entry
struct ErrorLogEntry {
    String error;
    String timestamp;
};

/// Session cron task
struct SessionCronTask {
    String id;
    String cron;
    String prompt;
    bool recurring = true;
};

/// Teleported session info
struct TeleportedSessionInfo {
    bool isTeleported = false;
    bool hasLoggedFirstMessage = false;
    String sessionId;
};

/// Model strings config
struct ModelStrings {
    String modelId;
    String displayName;
    String provider;
};

/// Centralized application state — thin aggregator over 4 focused sub-states
class AppState {
public:
    static AppState& instance();
    void reset();

    // === Direct sub-state access ===
    MetricsState& metrics() { return metrics_; }
    const MetricsState& metrics() const { return metrics_; }
    PermissionState& permission() { return permission_; }
    const PermissionState& permission() const { return permission_; }
    UIState& ui() { return ui_; }
    const UIState& ui() const { return ui_; }
    SessionState& session() { return session_; }
    const SessionState& session() const { return session_; }

    // ================================================================
    // Session Identity (forwarding to SessionState)
    // ================================================================
    String sessionId() const { return session_.sessionId(); }
    void setSessionId(const String& id) { session_.setSessionId(id); }
    void regenerateSessionId(bool preserveParent = false) { session_.regenerateSessionId(preserveParent); }
    String parentSessionId() const { return session_.parentSessionId(); }
    void setParentSessionId(const String& id) { session_.setParentSessionId(id); }
    void switchSession(const String& newSessionId, const String& newProjectDir) { session_.switchSession(newSessionId, newProjectDir); }
    void onSessionSwitch(SessionSwitchCallback cb) { session_.onSessionSwitch(std::move(cb)); }
    String sessionProjectDir() const { return session_.sessionProjectDir(); }
    void setSessionProjectDir(const String& dir) { session_.setSessionProjectDir(dir); }

    // ================================================================
    // Paths (forwarding to SessionState)
    // ================================================================
    String cwd() const { return session_.cwd(); }
    void setCwd(const String& path) { session_.setCwd(path); }
    String originalCwd() const { return session_.originalCwd(); }
    void setOriginalCwd(const String& path) { session_.setOriginalCwd(path); }
    String projectRoot() const { return session_.projectRoot(); }
    void setProjectRoot(const String& path) { session_.setProjectRoot(path); }

    // ================================================================
    // Interaction Mode (forwarding to UIState)
    // ================================================================
    bool isInteractive() const { return ui_.isInteractive(); }
    void setIsInteractive(bool val) { ui_.setIsInteractive(val); }
    bool isRemoteMode() const { return ui_.isRemoteMode(); }
    void setIsRemoteMode(bool val) { ui_.setIsRemoteMode(val); }
    bool isPlanMode() const { return ui_.isPlanMode(); }
    void setIsPlanMode(bool val) { ui_.setIsPlanMode(val); }
    bool isNonInteractiveSession() const { return ui_.isNonInteractiveSession(); }

    // ================================================================
    // Model Configuration (stays in AppState)
    // ================================================================
    String mainLoopModel() const;
    void setMainLoopModel(const String& model);
    String mainLoopModelOverride() const;
    void setMainLoopModelOverride(const String& model);
    String initialModel() const;
    void setInitialModel(const String& model);
    String fastModel() const;
    void setFastModel(const String& model);
    std::optional<ModelStrings> modelStrings() const;
    void setModelStrings(const ModelStrings& ms);
    void resetModelStrings();

    // ================================================================
    // Permission Mode (forwarding to PermissionState)
    // ================================================================
    String permissionMode() const { return permission_.permissionMode(); }
    void setPermissionMode(const String& mode) { permission_.setPermissionMode(mode); }
    bool bypassPermissions() const { return permission_.bypassPermissions(); }
    void setBypassPermissions(bool val) { permission_.setBypassPermissions(val); }
    bool sessionBypassPermissionsMode() const { return permission_.sessionBypassPermissionsMode(); }
    void setSessionBypassPermissionsMode(bool val) { permission_.setSessionBypassPermissionsMode(val); }
    bool strictToolResultPairing() const { return permission_.strictToolResultPairing(); }
    void setStrictToolResultPairing(bool val) { permission_.setStrictToolResultPairing(val); }

    // ================================================================
    // Cost / Tokens (forwarding to MetricsState)
    // ================================================================
    double totalCostUSD() const { return metrics_.totalCostUSD(); }
    void addCost(double cost) { metrics_.addCost(cost); }
    void setTotalCostUSD(double cost) { metrics_.setTotalCostUSD(cost); }
    void resetCostState() { metrics_.resetCostState(); }
    int totalInputTokens() const { return metrics_.totalInputTokens(); }
    void addInputTokens(int tokens) { metrics_.addInputTokens(tokens); }
    int totalOutputTokens() const { return metrics_.totalOutputTokens(); }
    void addOutputTokens(int tokens) { metrics_.addOutputTokens(tokens); }
    int totalCacheReadInputTokens() const { return metrics_.totalCacheReadInputTokens(); }
    void addCacheReadInputTokens(int tokens) { metrics_.addCacheReadInputTokens(tokens); }
    int totalCacheCreationInputTokens() const { return metrics_.totalCacheCreationInputTokens(); }
    void addCacheCreationInputTokens(int tokens) { metrics_.addCacheCreationInputTokens(tokens); }
    int totalWebSearchRequests() const { return metrics_.totalWebSearchRequests(); }
    void addWebSearchRequests(int count) { metrics_.addWebSearchRequests(count); }
    bool hasUnknownModelCost() const { return metrics_.hasUnknownModelCost(); }
    void setHasUnknownModelCost(bool val) { metrics_.setHasUnknownModelCost(val); }

    // ================================================================
    // Duration Metrics (forwarding to MetricsState)
    // ================================================================
    double totalAPIDuration() const { return metrics_.totalAPIDuration(); }
    void addToTotalDuration(double durationMs, double durationWithoutRetriesMs = 0.0) { metrics_.addToTotalDuration(durationMs, durationWithoutRetriesMs); }
    double totalAPIDurationWithoutRetries() const { return metrics_.totalAPIDurationWithoutRetries(); }
    double totalToolDuration() const { return metrics_.totalToolDuration(); }
    void addToToolDuration(double durationMs) { metrics_.addToToolDuration(durationMs); }
    void resetTotalDurationState() { metrics_.resetTotalDurationState(); }

    double turnHookDurationMs() const { return metrics_.turnHookDurationMs(); }
    void addToTurnHookDuration(double durationMs) { metrics_.addToTurnHookDuration(durationMs); }
    void resetTurnHookDuration() { metrics_.resetTurnHookDuration(); }
    int turnHookCount() const { return metrics_.turnHookCount(); }

    double turnToolDurationMs() const { return metrics_.turnToolDurationMs(); }
    void resetTurnToolDuration() { metrics_.resetTurnToolDuration(); }
    int turnToolCount() const { return metrics_.turnToolCount(); }

    double turnClassifierDurationMs() const { return metrics_.turnClassifierDurationMs(); }
    void addToTurnClassifierDuration(double durationMs) { metrics_.addToTurnClassifierDuration(durationMs); }
    void resetTurnClassifierDuration() { metrics_.resetTurnClassifierDuration(); }
    int turnClassifierCount() const { return metrics_.turnClassifierCount(); }

    // ================================================================
    // Token Budget (forwarding to MetricsState)
    // ================================================================
    int turnOutputTokens() const { return metrics_.turnOutputTokens(); }
    int currentTurnTokenBudget() const { return metrics_.currentTurnTokenBudget(); }
    void snapshotOutputTokensForTurn(int budget) { metrics_.snapshotOutputTokensForTurn(budget); }
    int budgetContinuationCount() const { return metrics_.budgetContinuationCount(); }
    void incrementBudgetContinuationCount() { metrics_.incrementBudgetContinuationCount(); }

    // ================================================================
    // Interaction Time (forwarding to MetricsState)
    // ================================================================
    void updateLastInteractionTime(bool immediate = false) { metrics_.updateLastInteractionTime(immediate); }
    void flushInteractionTime() { metrics_.flushInteractionTime(); }
    double lastInteractionTime() const { return metrics_.lastInteractionTime(); }

    // ================================================================
    // Line Changes (forwarding to MetricsState)
    // ================================================================
    int totalLinesAdded() const { return metrics_.totalLinesAdded(); }
    int totalLinesRemoved() const { return metrics_.totalLinesRemoved(); }
    void addToTotalLinesChanged(int added, int removed) { metrics_.addToTotalLinesChanged(added, removed); }

    // ================================================================
    // Session Flags (forwarding to UIState)
    // ================================================================
    bool hasExitedPlanMode() const { return ui_.hasExitedPlanMode(); }
    void setHasExitedPlanMode(bool val) { ui_.setHasExitedPlanMode(val); }
    bool needsPlanModeExitAttachment() const { return ui_.needsPlanModeExitAttachment(); }
    void setNeedsPlanModeExitAttachment(bool val) { ui_.setNeedsPlanModeExitAttachment(val); }
    void handlePlanModeTransition(const String& fromMode, const String& toMode) { ui_.handlePlanModeTransition(fromMode, toMode); }
    bool needsAutoModeExitAttachment() const { return ui_.needsAutoModeExitAttachment(); }
    void setNeedsAutoModeExitAttachment(bool val) { ui_.setNeedsAutoModeExitAttachment(val); }
    void handleAutoModeTransition(const String& fromMode, const String& toMode) { ui_.handleAutoModeTransition(fromMode, toMode); }
    bool hasKairosActive() const { return ui_.hasKairosActive(); }
    void setKairosActive(bool val) { ui_.setKairosActive(val); }

    // ================================================================
    // Session Lifecycle (split: source/persistence -> SessionState, trust -> PermissionState)
    // ================================================================
    String sessionSource() const { return session_.sessionSource(); }
    void setSessionSource(const String& source) { session_.setSessionSource(source); }
    bool sessionTrustAccepted() const { return permission_.sessionTrustAccepted(); }
    void setSessionTrustAccepted(bool val) { permission_.setSessionTrustAccepted(val); }
    bool sessionPersistenceDisabled() const { return session_.sessionPersistenceDisabled(); }
    void setSessionPersistenceDisabled(bool val) { session_.setSessionPersistenceDisabled(val); }

    // ================================================================
    // Beta Header Latches (forwarding to UIState)
    // ================================================================
    std::optional<bool> afkModeLatched() const { return ui_.afkModeLatched(); }
    void setAfkModeLatched(std::optional<bool> val) { ui_.setAfkModeLatched(val); }
    std::optional<bool> fastModeLatched() const { return ui_.fastModeLatched(); }
    void setFastModeLatched(std::optional<bool> val) { ui_.setFastModeLatched(val); }
    std::optional<bool> cacheEditingHeaderLatched() const { return ui_.cacheEditingHeaderLatched(); }
    void setCacheEditingHeaderLatched(std::optional<bool> val) { ui_.setCacheEditingHeaderLatched(val); }
    std::optional<bool> thinkingClearLatched() const { return ui_.thinkingClearLatched(); }
    void setThinkingClearLatched(std::optional<bool> val) { ui_.setThinkingClearLatched(val); }
    void clearBetaHeaderLatches() { ui_.clearBetaHeaderLatches(); }

    // ================================================================
    // Cache / System Prompt (stays in AppState)
    // ================================================================
    std::unordered_map<String, std::optional<String>> systemPromptSectionCache() const;
    void setSystemPromptSectionCacheEntry(const String& name, const std::optional<String>& value);
    void clearSystemPromptSectionState();
    String cachedClaudeMdContent() const;
    void setCachedClaudeMdContent(const String& content);
    String promptId() const;
    void setPromptId(const String& id);

    // ================================================================
    // Prompt Cache (stays in AppState)
    // ================================================================
    std::optional<std::vector<String>> promptCache1hAllowlist() const;
    void setPromptCache1hAllowlist(const std::vector<String>& list);
    std::optional<bool> promptCache1hEligible() const;
    void setPromptCache1hEligible(bool val);

    // ================================================================
    // Auth (stays in AppState)
    // ================================================================
    std::optional<String> sessionIngressToken() const;
    void setSessionIngressToken(const std::optional<String>& token);
    std::optional<String> oauthTokenFromFd() const;
    void setOauthTokenFromFd(const std::optional<String>& token);
    std::optional<String> apiKeyFromFd() const;
    void setApiKeyFromFd(const std::optional<String>& key);
    bool userMsgOptIn() const;
    void setUserMsgOptIn(bool val);
    String clientType() const;
    void setClientType(const String& type);

    // ================================================================
    // Settings / Configuration (stays in AppState)
    // ================================================================
    std::optional<String> flagSettingsPath() const;
    void setFlagSettingsPath(const std::optional<String>& path);
    std::optional<Json> flagSettingsInline() const;
    void setFlagSettingsInline(const std::optional<Json>& settings);
    std::vector<int> allowedSettingSources() const;
    void setAllowedSettingSources(const std::vector<int>& sources);
    bool preferThirdPartyAuthentication() const;
    std::optional<String> questionPreviewFormat() const;
    void setQuestionPreviewFormat(const std::optional<String>& fmt);
    std::vector<String> additionalDirectoriesForClaudeMd() const;
    void setAdditionalDirectoriesForClaudeMd(const std::vector<String>& dirs);
    std::optional<std::vector<String>> sdkBetas() const;
    void setSdkBetas(const std::vector<String>& betas);

    // ================================================================
    // Agent Coordination (stays in AppState)
    // ================================================================
    std::unordered_map<String, String> agentColorMap() const;
    String getNextAgentColor(const String& agentId);
    std::optional<String> mainThreadAgentType() const;
    void setMainThreadAgentType(const std::optional<String>& type);
    bool sdkAgentProgressSummariesEnabled() const;
    void setSdkAgentProgressSummariesEnabled(bool val);

    // ================================================================
    // Invoked Skills (stays in AppState)
    // ================================================================
    std::unordered_map<String, InvokedSkillInfo> invokedSkills() const;
    void addInvokedSkill(const String& skillName, const String& skillPath,
                         const String& content, const String& agentId = "");
    std::vector<InvokedSkillInfo> getInvokedSkillsForAgent(const String& agentId) const;
    void clearInvokedSkills(const std::vector<String>& preservedAgentIds = {});
    void clearInvokedSkillsForAgent(const String& agentId);

    // ================================================================
    // API Request Tracking (stays in AppState)
    // ================================================================
    std::optional<Json> lastAPIRequest() const;
    void setLastAPIRequest(const Json& req);
    std::optional<Json> lastAPIRequestMessages() const;
    void setLastAPIRequestMessages(const Json& msgs);
    std::optional<Json> lastClassifierRequests() const;
    void setLastClassifierRequests(const Json& reqs);
    std::optional<String> lastMainRequestId() const;
    void setLastMainRequestId(const String& id);
    std::optional<double> lastApiCompletionTimestamp() const;
    void setLastApiCompletionTimestamp(double ts);

    // ================================================================
    // Post-Compaction (stays in AppState)
    // ================================================================
    bool pendingPostCompaction() const;
    void markPostCompaction();
    bool consumePostCompaction();

    // ================================================================
    // Diagnostics (stays in AppState)
    // ================================================================
    void addToInMemoryErrorLog(const String& error);
    std::vector<ErrorLogEntry> inMemoryErrorLog() const;
    void addSlowOperation(const String& operation, double durationMs);
    std::vector<SlowOperation> slowOperations() const;

    // ================================================================
    // Hooks / Plugins (stays in AppState)
    // ================================================================
    std::optional<Json> initJsonSchema() const;
    void setInitJsonSchema(const Json& schema);
    std::optional<Json> registeredHooks() const;
    void setRegisteredHooks(const Json& hooks);
    void clearRegisteredHooks();
    std::vector<String> inlinePlugins() const;
    void setInlinePlugins(const std::vector<String>& plugins);
    std::optional<bool> chromeFlagOverride() const;
    void setChromeFlagOverride(std::optional<bool> val);
    bool useCoworkPlugins() const;
    void setUseCoworkPlugins(bool val);

    // ================================================================
    // Scheduled Tasks (stays in AppState)
    // ================================================================
    bool scheduledTasksEnabled() const;
    void setScheduledTasksEnabled(bool val);
    std::vector<SessionCronTask> sessionCronTasks() const;
    void addSessionCronTask(const SessionCronTask& task);
    void removeSessionCronTasks(const String& taskId);

    // ================================================================
    // Team (stays in AppState)
    // ================================================================
    std::unordered_set<String> sessionCreatedTeams() const;
    void addSessionCreatedTeam(const String& team);

    // ================================================================
    // Teleport (stays in AppState)
    // ================================================================
    std::optional<TeleportedSessionInfo> teleportedSessionInfo() const;
    void setTeleportedSessionInfo(const TeleportedSessionInfo& info);
    void markFirstTeleportMessageLogged();

    // ================================================================
    // Channels (stays in AppState)
    // ================================================================
    std::vector<String> allowedChannels() const;
    void setAllowedChannels(const std::vector<String>& channels);
    bool hasDevChannels() const;
    void setHasDevChannels(bool val);

    // ================================================================
    // Direct Connect (stays in AppState)
    // ================================================================
    std::optional<String> directConnectServerUrl() const;
    void setDirectConnectServerUrl(const std::optional<String>& url);

    // ================================================================
    // Plan (stays in AppState)
    // ================================================================
    std::unordered_map<String, String> planSlugCache() const;

    // ================================================================
    // LSP (forwarding to UIState)
    // ================================================================
    bool lspRecommendationShownThisSession() const { return ui_.lspRecommendationShownThisSession(); }
    void setLspRecommendationShownThisSession(bool val) { ui_.setLspRecommendationShownThisSession(val); }

    // ================================================================
    // Last Emitted Date (stays in AppState)
    // ================================================================
    std::optional<String> lastEmittedDate() const;
    void setLastEmittedDate(const String& date);

    // ================================================================
    // Session Duration (forwarding to SessionState)
    // ================================================================
    std::chrono::steady_clock::time_point sessionStartTime() const { return session_.sessionStartTime(); }
    void setSessionStartTime(std::chrono::steady_clock::time_point t) { session_.setSessionStartTime(t); }
    int sessionDurationSeconds() const { return session_.sessionDurationSeconds(); }

    // ================================================================
    // Model Usage (forwarding to MetricsState)
    // ================================================================
    ModelUsage getModelUsage(const String& model) const { return metrics_.getModelUsage(model); }
    void recordModelUsage(const String& model, int inputTokens, int outputTokens, double cost) { metrics_.recordModelUsage(model, inputTokens, outputTokens, cost); }

    // ================================================================
    // Cost Restore (forwarding to MetricsState)
    // ================================================================
    CostState getCostStateForRestore() const { return metrics_.getCostStateForRestore(); }
    void setCostStateForRestore(const CostState& state) { metrics_.setCostStateForRestore(state); }

private:
    AppState() = default;
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    // Sub-states
    MetricsState metrics_;
    PermissionState permission_;
    UIState ui_;
    SessionState session_;

    mutable std::mutex mutex_;

    // Model (stays in AppState)
    String mainLoopModel_;
    std::optional<String> mainLoopModelOverride_;
    String initialModel_;
    String fastModel_;
    std::optional<ModelStrings> modelStrings_;
    std::optional<std::vector<String>> sdkBetas_;

    // Cache
    std::unordered_map<String, std::optional<String>> systemPromptSectionCache_;
    String cachedClaudeMdContent_;
    String promptId_;

    // Prompt Cache
    std::optional<std::vector<String>> promptCache1hAllowlist_;
    std::optional<bool> promptCache1hEligible_;

    // Auth
    std::optional<String> sessionIngressToken_;
    std::optional<String> oauthTokenFromFd_;
    std::optional<String> apiKeyFromFd_;
    bool userMsgOptIn_ = false;
    String clientType_;

    // Settings
    std::optional<String> flagSettingsPath_;
    std::optional<Json> flagSettingsInline_;
    std::vector<int> allowedSettingSources_;
    std::optional<String> questionPreviewFormat_;
    std::vector<String> additionalDirectoriesForClaudeMd_;

    // Agent Coordination
    std::unordered_map<String, String> agentColorMap_;
    int agentColorIndex_ = 0;
    std::optional<String> mainThreadAgentType_;
    bool sdkAgentProgressSummariesEnabled_ = false;

    // Invoked Skills
    std::unordered_map<String, InvokedSkillInfo> invokedSkills_;

    // API Request Tracking
    std::optional<Json> lastAPIRequest_;
    std::optional<Json> lastAPIRequestMessages_;
    std::optional<Json> lastClassifierRequests_;
    std::optional<String> lastMainRequestId_;
    std::optional<double> lastApiCompletionTimestamp_;

    // Post-Compaction
    bool pendingPostCompaction_ = false;

    // Diagnostics
    std::vector<ErrorLogEntry> inMemoryErrorLog_;
    std::vector<SlowOperation> slowOperations_;

    // Hooks/Plugins
    std::optional<Json> initJsonSchema_;
    std::optional<Json> registeredHooks_;
    std::vector<String> inlinePlugins_;
    std::optional<bool> chromeFlagOverride_;
    bool useCoworkPlugins_ = false;

    // Scheduled Tasks
    bool scheduledTasksEnabled_ = false;
    std::vector<SessionCronTask> sessionCronTasks_;

    // Team
    std::unordered_set<String> sessionCreatedTeams_;

    // Teleport
    std::optional<TeleportedSessionInfo> teleportedSessionInfo_;

    // Channels
    std::vector<String> allowedChannels_;
    bool hasDevChannels_ = false;

    // Direct Connect
    std::optional<String> directConnectServerUrl_;

    // Plan
    std::unordered_map<String, String> planSlugCache_;

    // Last Emitted Date
    std::optional<String> lastEmittedDate_;
};

} // namespace claude
