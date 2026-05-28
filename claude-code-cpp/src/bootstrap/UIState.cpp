#include <claude/bootstrap/UIState.hpp>

namespace claude {

void UIState::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    isInteractive_ = false;
    isRemoteMode_ = false;
    isPlanMode_ = false;

    hasExitedPlanMode_ = false;
    needsPlanModeExitAttachment_ = false;
    needsAutoModeExitAttachment_ = false;
    kairosActive_ = false;

    afkModeLatched_.reset();
    fastModeLatched_.reset();
    cacheEditingHeaderLatched_.reset();
    thinkingClearLatched_.reset();

    lspRecommendationShownThisSession_ = false;
}

// === Interaction Mode ===

bool UIState::isInteractive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isInteractive_;
}

void UIState::setIsInteractive(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    isInteractive_ = val;
}

bool UIState::isRemoteMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isRemoteMode_;
}

void UIState::setIsRemoteMode(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    isRemoteMode_ = val;
}

bool UIState::isPlanMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isPlanMode_;
}

void UIState::setIsPlanMode(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    isPlanMode_ = val;
}

bool UIState::isNonInteractiveSession() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !isInteractive_;
}

// === Session Flags ===

bool UIState::hasExitedPlanMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasExitedPlanMode_;
}

void UIState::setHasExitedPlanMode(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    hasExitedPlanMode_ = val;
}

bool UIState::needsPlanModeExitAttachment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return needsPlanModeExitAttachment_;
}

void UIState::setNeedsPlanModeExitAttachment(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    needsPlanModeExitAttachment_ = val;
}

void UIState::handlePlanModeTransition(const String& fromMode, const String& toMode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fromMode == "plan" && toMode != "plan") {
        hasExitedPlanMode_ = true;
        needsPlanModeExitAttachment_ = true;
    }
}

bool UIState::needsAutoModeExitAttachment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return needsAutoModeExitAttachment_;
}

void UIState::setNeedsAutoModeExitAttachment(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    needsAutoModeExitAttachment_ = val;
}

void UIState::handleAutoModeTransition(const String& fromMode, const String& toMode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fromMode == "auto" && toMode != "auto") {
        needsAutoModeExitAttachment_ = true;
    }
}

bool UIState::hasKairosActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return kairosActive_;
}

void UIState::setKairosActive(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    kairosActive_ = val;
}

// === Beta Header Latches ===

std::optional<bool> UIState::afkModeLatched() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return afkModeLatched_;
}

void UIState::setAfkModeLatched(std::optional<bool> val) {
    std::lock_guard<std::mutex> lock(mutex_);
    afkModeLatched_ = val;
}

std::optional<bool> UIState::fastModeLatched() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fastModeLatched_;
}

void UIState::setFastModeLatched(std::optional<bool> val) {
    std::lock_guard<std::mutex> lock(mutex_);
    fastModeLatched_ = val;
}

std::optional<bool> UIState::cacheEditingHeaderLatched() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheEditingHeaderLatched_;
}

void UIState::setCacheEditingHeaderLatched(std::optional<bool> val) {
    std::lock_guard<std::mutex> lock(mutex_);
    cacheEditingHeaderLatched_ = val;
}

std::optional<bool> UIState::thinkingClearLatched() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return thinkingClearLatched_;
}

void UIState::setThinkingClearLatched(std::optional<bool> val) {
    std::lock_guard<std::mutex> lock(mutex_);
    thinkingClearLatched_ = val;
}

void UIState::clearBetaHeaderLatches() {
    std::lock_guard<std::mutex> lock(mutex_);
    afkModeLatched_.reset();
    fastModeLatched_.reset();
    cacheEditingHeaderLatched_.reset();
    thinkingClearLatched_.reset();
}

// === LSP ===

bool UIState::lspRecommendationShownThisSession() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lspRecommendationShownThisSession_;
}

void UIState::setLspRecommendationShownThisSession(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    lspRecommendationShownThisSession_ = val;
}

} // namespace claude
