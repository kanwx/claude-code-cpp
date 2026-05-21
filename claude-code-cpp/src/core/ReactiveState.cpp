#include "claude/core/ReactiveState.hpp"

namespace claude::reactive {

// ============================================================
// Model & Provider
// ============================================================

String accessors::currentModel(const AppState& state) {
    // Prefer override, then main loop model, then initial model
    String overrideVal = state.mainLoopModelOverride();
    if (!overrideVal.empty()) return overrideVal;

    String model = state.mainLoopModel();
    if (!model.empty()) return model;

    model = state.initialModel();
    if (!model.empty()) return model;

    // Fall back to modelStrings displayName
    auto ms = state.modelStrings();
    if (ms.has_value() && !ms->displayName.empty()) return ms->displayName;

    return "unknown";
}

String accessors::currentProvider(const AppState& state) {
    auto ms = state.modelStrings();
    if (ms.has_value() && !ms->provider.empty()) return ms->provider;
    return "firstParty";
}

// ============================================================
// Session State
// ============================================================

bool accessors::isStreaming(const AppState& state) {
    // AppState doesn't track streaming state directly.
    // Streaming is managed by the UI (FtxuiRepl) and agent loop.
    // Return false by default; the UI layer overrides this.
    (void)state;
    return false;
}

bool accessors::isCompactRunning(const AppState& state) {
    // Compact state is tracked by pendingPostCompaction flag
    return state.pendingPostCompaction();
}

int accessors::messageCount(const AppState& state) {
    // AppState doesn't expose message count directly.
    // Messages are managed by HistoryManager/ConversationPersistence.
    (void)state;
    return 0;
}

int accessors::apiCallCount(const AppState& state) {
    // Derive from total input tokens / typical prompt size as a rough proxy,
    // but AppState doesn't track call count directly.
    // Use the sum of model usage request counts if available.
    (void)state;
    return 0;
}

// ============================================================
// Token & Cost
// ============================================================

long accessors::inputTokens(const AppState& state) {
    return static_cast<long>(state.totalInputTokens());
}

long accessors::outputTokens(const AppState& state) {
    return static_cast<long>(state.totalOutputTokens());
}

double accessors::tokenUsagePercent(const AppState& state) {
    long used = inputTokens(state) + outputTokens(state);
    // Default context window is ~200k tokens; derive from budget if available
    long budget = static_cast<long>(state.currentTurnTokenBudget());
    if (budget <= 0) budget = 200000L;
    if (budget <= 0) return 0.0;
    return (static_cast<double>(used) / static_cast<double>(budget)) * 100.0;
}

bool accessors::shouldAutoCompact(const AppState& state) {
    double pct = tokenUsagePercent(state);
    return pct >= 80.0;
}

// ============================================================
// Mode Flags
// ============================================================

bool accessors::isVimMode(const AppState& state) {
    // Vim mode is a UI setting not stored in AppState.
    (void)state;
    return false;
}

bool accessors::isDebugMode(const AppState& state) {
    // Debug mode is a runtime flag not in AppState.
    (void)state;
    return false;
}

bool accessors::isFastMode(const AppState& state) {
    auto latched = state.fastModeLatched();
    if (latched.has_value()) return *latched;
    return false;
}

String accessors::currentEffort(const AppState& state) {
    // Effort level could be derived from fast mode or model override.
    if (isFastMode(state)) return "low";
    String overrideVal2 = state.mainLoopModelOverride();
    if (!overrideVal2.empty() && overrideVal2.find("haiku") != String::npos) return "low";
    return "high";
}

bool accessors::isBughunterMode(const AppState& state) {
    // Bughunter mode is not tracked in AppState.
    (void)state;
    return false;
}

// ============================================================
// UI State
// ============================================================

bool accessors::hasActivePermission(const AppState& state) {
    // Permission state is managed by the UI layer (FtxuiRepl).
    // AppState tracks permission mode but not active prompts.
    (void)state;
    return false;
}

String accessors::permissionTarget(const AppState& state) {
    (void)state;
    return "";
}

bool accessors::isModalOpen(const AppState& state) {
    // Modal state is UI-layer only.
    (void)state;
    return false;
}

// ============================================================
// DisplayState batch computation
// ============================================================

DisplayState DisplayState::fromAppState(const AppState& state) {
    DisplayState ds;
    ds.model = accessors::currentModel(state);
    ds.provider = accessors::currentProvider(state);
    ds.streaming = accessors::isStreaming(state);
    ds.inputTokens = accessors::inputTokens(state);
    ds.outputTokens = accessors::outputTokens(state);
    ds.usagePercent = accessors::tokenUsagePercent(state);
    ds.vimMode = accessors::isVimMode(state);
    ds.effort = accessors::currentEffort(state);
    ds.messages = accessors::messageCount(state);
    ds.apiCalls = accessors::apiCallCount(state);
    return ds;
}

} // namespace claude::reactive
