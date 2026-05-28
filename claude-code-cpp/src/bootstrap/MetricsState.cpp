#include <claude/bootstrap/MetricsState.hpp>
#include <chrono>

namespace claude {

void MetricsState::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCostUSD_ = 0.0;
    totalInputTokens_ = 0;
    totalOutputTokens_ = 0;
    totalCacheReadInputTokens_ = 0;
    totalCacheCreationInputTokens_ = 0;
    totalWebSearchRequests_ = 0;
    hasUnknownModelCost_ = false;

    totalAPIDuration_ = 0.0;
    totalAPIDurationWithoutRetries_ = 0.0;
    totalToolDuration_ = 0.0;
    turnHookDurationMs_ = 0.0;
    turnHookCount_ = 0;
    turnToolDurationMs_ = 0.0;
    turnToolCount_ = 0;
    turnClassifierDurationMs_ = 0.0;
    turnClassifierCount_ = 0;

    turnOutputTokens_ = 0;
    currentTurnTokenBudget_ = 0;
    budgetContinuationCount_ = 0;

    lastInteractionTime_ = 0.0;
    activeTimeAccumulator_ = 0.0;

    totalLinesAdded_ = 0;
    totalLinesRemoved_ = 0;

    modelUsage_.clear();
}

// === Cost / Tokens ===

double MetricsState::totalCostUSD() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCostUSD_;
}

void MetricsState::addCost(double cost) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCostUSD_ += cost;
}

void MetricsState::setTotalCostUSD(double cost) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCostUSD_ = cost;
}

void MetricsState::resetCostState() {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCostUSD_ = 0.0;
    totalInputTokens_ = 0;
    totalOutputTokens_ = 0;
    totalCacheReadInputTokens_ = 0;
    totalCacheCreationInputTokens_ = 0;
    totalWebSearchRequests_ = 0;
    hasUnknownModelCost_ = false;
    modelUsage_.clear();
}

int MetricsState::totalInputTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalInputTokens_;
}

void MetricsState::addInputTokens(int tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalInputTokens_ += tokens;
}

int MetricsState::totalOutputTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalOutputTokens_;
}

void MetricsState::addOutputTokens(int tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalOutputTokens_ += tokens;
}

int MetricsState::totalCacheReadInputTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCacheReadInputTokens_;
}

void MetricsState::addCacheReadInputTokens(int tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCacheReadInputTokens_ += tokens;
}

int MetricsState::totalCacheCreationInputTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCacheCreationInputTokens_;
}

void MetricsState::addCacheCreationInputTokens(int tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCacheCreationInputTokens_ += tokens;
}

int MetricsState::totalWebSearchRequests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalWebSearchRequests_;
}

void MetricsState::addWebSearchRequests(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalWebSearchRequests_ += count;
}

bool MetricsState::hasUnknownModelCost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasUnknownModelCost_;
}

void MetricsState::setHasUnknownModelCost(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    hasUnknownModelCost_ = val;
}

// === Duration Metrics ===

double MetricsState::totalAPIDuration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalAPIDuration_;
}

void MetricsState::addToTotalDuration(double durationMs, double durationWithoutRetriesMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalAPIDuration_ += durationMs;
    totalAPIDurationWithoutRetries_ += durationWithoutRetriesMs > 0 ? durationWithoutRetriesMs : durationMs;
}

double MetricsState::totalAPIDurationWithoutRetries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalAPIDurationWithoutRetries_;
}

double MetricsState::totalToolDuration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalToolDuration_;
}

void MetricsState::addToToolDuration(double durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalToolDuration_ += durationMs;
}

void MetricsState::resetTotalDurationState() {
    std::lock_guard<std::mutex> lock(mutex_);
    totalAPIDuration_ = 0.0;
    totalAPIDurationWithoutRetries_ = 0.0;
    totalToolDuration_ = 0.0;
}

double MetricsState::turnHookDurationMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnHookDurationMs_;
}

void MetricsState::addToTurnHookDuration(double durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    turnHookDurationMs_ += durationMs;
    turnHookCount_++;
}

void MetricsState::resetTurnHookDuration() {
    std::lock_guard<std::mutex> lock(mutex_);
    turnHookDurationMs_ = 0.0;
    turnHookCount_ = 0;
}

int MetricsState::turnHookCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnHookCount_;
}

double MetricsState::turnToolDurationMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnToolDurationMs_;
}

void MetricsState::resetTurnToolDuration() {
    std::lock_guard<std::mutex> lock(mutex_);
    turnToolDurationMs_ = 0.0;
    turnToolCount_ = 0;
}

int MetricsState::turnToolCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnToolCount_;
}

double MetricsState::turnClassifierDurationMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnClassifierDurationMs_;
}

void MetricsState::addToTurnClassifierDuration(double durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    turnClassifierDurationMs_ += durationMs;
    turnClassifierCount_++;
}

void MetricsState::resetTurnClassifierDuration() {
    std::lock_guard<std::mutex> lock(mutex_);
    turnClassifierDurationMs_ = 0.0;
    turnClassifierCount_ = 0;
}

int MetricsState::turnClassifierCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnClassifierCount_;
}

// === Token Budget ===

int MetricsState::turnOutputTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turnOutputTokens_;
}

int MetricsState::currentTurnTokenBudget() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentTurnTokenBudget_;
}

void MetricsState::snapshotOutputTokensForTurn(int budget) {
    std::lock_guard<std::mutex> lock(mutex_);
    turnOutputTokens_ = totalOutputTokens_;
    currentTurnTokenBudget_ = budget;
}

int MetricsState::budgetContinuationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return budgetContinuationCount_;
}

void MetricsState::incrementBudgetContinuationCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    budgetContinuationCount_++;
}

// === Interaction Time ===

void MetricsState::updateLastInteractionTime(bool immediate) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    if (lastInteractionTime_ > 0) {
        double delta = static_cast<double>(ms) - lastInteractionTime_;
        if (delta > 0 && delta < 300000.0) {
            activeTimeAccumulator_ += delta;
        }
    }
    lastInteractionTime_ = static_cast<double>(ms);
    if (immediate) {
        flushInteractionTime();
    }
}

void MetricsState::flushInteractionTime() {
    activeTimeAccumulator_ = 0.0;
}

double MetricsState::lastInteractionTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastInteractionTime_;
}

// === Line Changes ===

int MetricsState::totalLinesAdded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalLinesAdded_;
}

int MetricsState::totalLinesRemoved() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalLinesRemoved_;
}

void MetricsState::addToTotalLinesChanged(int added, int removed) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalLinesAdded_ += added;
    totalLinesRemoved_ += removed;
}

// === Model Usage ===

ModelUsage MetricsState::getModelUsage(const String& model) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modelUsage_.find(model);
    if (it != modelUsage_.end()) return it->second;
    return {};
}

void MetricsState::recordModelUsage(const String& model, int inputTokens, int outputTokens, double cost) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& usage = modelUsage_[model];
    usage.inputTokens += inputTokens;
    usage.outputTokens += outputTokens;
    usage.requestCount++;
    usage.cost += cost;
}

// === Cost Restore ===

CostState MetricsState::getCostStateForRestore() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CostState state;
    state.totalCostUSD = totalCostUSD_;
    state.totalInputTokens = totalInputTokens_;
    state.totalOutputTokens = totalOutputTokens_;
    state.totalCacheReadInputTokens = totalCacheReadInputTokens_;
    state.totalCacheCreationInputTokens = totalCacheCreationInputTokens_;
    state.totalAPIDuration = totalAPIDuration_;
    state.totalAPIDurationWithoutRetries = totalAPIDurationWithoutRetries_;
    state.totalToolDuration = totalToolDuration_;
    state.modelUsage = modelUsage_;
    return state;
}

void MetricsState::setCostStateForRestore(const CostState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalCostUSD_ = state.totalCostUSD;
    totalInputTokens_ = state.totalInputTokens;
    totalOutputTokens_ = state.totalOutputTokens;
    totalCacheReadInputTokens_ = state.totalCacheReadInputTokens;
    totalCacheCreationInputTokens_ = state.totalCacheCreationInputTokens;
    totalAPIDuration_ = state.totalAPIDuration;
    totalAPIDurationWithoutRetries_ = state.totalAPIDurationWithoutRetries;
    totalToolDuration_ = state.totalToolDuration;
    modelUsage_ = state.modelUsage;
}

} // namespace claude
