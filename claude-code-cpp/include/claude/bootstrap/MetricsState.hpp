#pragma once

#include "../core/Types.hpp"
#include <map>
#include <mutex>

namespace claude {

/// Per-model usage statistics
struct ModelUsage {
    int inputTokens = 0;
    int outputTokens = 0;
    int cacheReadInputTokens = 0;
    int cacheCreationInputTokens = 0;
    int webSearchRequests = 0;
    int requestCount = 0;
    double cost = 0.0;
};

/// Snapshot of cost/token state for session resume
struct CostState {
    double totalCostUSD = 0.0;
    int totalInputTokens = 0;
    int totalOutputTokens = 0;
    int totalCacheReadInputTokens = 0;
    int totalCacheCreationInputTokens = 0;
    double totalAPIDuration = 0.0;
    double totalAPIDurationWithoutRetries = 0.0;
    double totalToolDuration = 0.0;
    std::map<String, ModelUsage> modelUsage;
};

/// Metrics sub-state: cost, tokens, durations, lines, model usage
class MetricsState {
public:
    void reset();

    // Cost / Tokens
    double totalCostUSD() const;
    void addCost(double cost);
    void setTotalCostUSD(double cost);
    void resetCostState();
    int totalInputTokens() const;
    void addInputTokens(int tokens);
    int totalOutputTokens() const;
    void addOutputTokens(int tokens);
    int totalCacheReadInputTokens() const;
    void addCacheReadInputTokens(int tokens);
    int totalCacheCreationInputTokens() const;
    void addCacheCreationInputTokens(int tokens);
    int totalWebSearchRequests() const;
    void addWebSearchRequests(int count);
    bool hasUnknownModelCost() const;
    void setHasUnknownModelCost(bool val);

    // Duration metrics
    double totalAPIDuration() const;
    void addToTotalDuration(double durationMs, double durationWithoutRetriesMs = 0.0);
    double totalAPIDurationWithoutRetries() const;
    double totalToolDuration() const;
    void addToToolDuration(double durationMs);
    void resetTotalDurationState();

    double turnHookDurationMs() const;
    void addToTurnHookDuration(double durationMs);
    void resetTurnHookDuration();
    int turnHookCount() const;

    double turnToolDurationMs() const;
    void resetTurnToolDuration();
    int turnToolCount() const;

    double turnClassifierDurationMs() const;
    void addToTurnClassifierDuration(double durationMs);
    void resetTurnClassifierDuration();
    int turnClassifierCount() const;

    // Token Budget
    int turnOutputTokens() const;
    int currentTurnTokenBudget() const;
    void snapshotOutputTokensForTurn(int budget);
    int budgetContinuationCount() const;
    void incrementBudgetContinuationCount();

    // Interaction time
    void updateLastInteractionTime(bool immediate = false);
    void flushInteractionTime();
    double lastInteractionTime() const;

    // Line changes
    int totalLinesAdded() const;
    int totalLinesRemoved() const;
    void addToTotalLinesChanged(int added, int removed);

    // Model usage
    ModelUsage getModelUsage(const String& model) const;
    void recordModelUsage(const String& model, int inputTokens, int outputTokens, double cost);

    // Cost restore
    CostState getCostStateForRestore() const;
    void setCostStateForRestore(const CostState& state);

private:
    mutable std::mutex mutex_;

    double totalCostUSD_ = 0.0;
    int totalInputTokens_ = 0;
    int totalOutputTokens_ = 0;
    int totalCacheReadInputTokens_ = 0;
    int totalCacheCreationInputTokens_ = 0;
    int totalWebSearchRequests_ = 0;
    bool hasUnknownModelCost_ = false;

    double totalAPIDuration_ = 0.0;
    double totalAPIDurationWithoutRetries_ = 0.0;
    double totalToolDuration_ = 0.0;
    double turnHookDurationMs_ = 0.0;
    int turnHookCount_ = 0;
    double turnToolDurationMs_ = 0.0;
    int turnToolCount_ = 0;
    double turnClassifierDurationMs_ = 0.0;
    int turnClassifierCount_ = 0;

    int turnOutputTokens_ = 0;
    int currentTurnTokenBudget_ = 0;
    int budgetContinuationCount_ = 0;

    double lastInteractionTime_ = 0.0;
    double activeTimeAccumulator_ = 0.0;

    int totalLinesAdded_ = 0;
    int totalLinesRemoved_ = 0;

    std::map<String, ModelUsage> modelUsage_;
};

} // namespace claude
