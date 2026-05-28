#pragma once

#include "../core/Types.hpp"
#include <mutex>

namespace claude {

/// UI/mode sub-state: interactive, remote, plan mode, beta latches, session flags
class UIState {
public:
    void reset();

    bool isInteractive() const;
    void setIsInteractive(bool val);
    bool isRemoteMode() const;
    void setIsRemoteMode(bool val);
    bool isPlanMode() const;
    void setIsPlanMode(bool val);
    bool isNonInteractiveSession() const;

    bool hasExitedPlanMode() const;
    void setHasExitedPlanMode(bool val);
    bool needsPlanModeExitAttachment() const;
    void setNeedsPlanModeExitAttachment(bool val);
    void handlePlanModeTransition(const String& fromMode, const String& toMode);
    bool needsAutoModeExitAttachment() const;
    void setNeedsAutoModeExitAttachment(bool val);
    void handleAutoModeTransition(const String& fromMode, const String& toMode);
    bool hasKairosActive() const;
    void setKairosActive(bool val);

    std::optional<bool> afkModeLatched() const;
    void setAfkModeLatched(std::optional<bool> val);
    std::optional<bool> fastModeLatched() const;
    void setFastModeLatched(std::optional<bool> val);
    std::optional<bool> cacheEditingHeaderLatched() const;
    void setCacheEditingHeaderLatched(std::optional<bool> val);
    std::optional<bool> thinkingClearLatched() const;
    void setThinkingClearLatched(std::optional<bool> val);
    void clearBetaHeaderLatches();

    bool lspRecommendationShownThisSession() const;
    void setLspRecommendationShownThisSession(bool val);

private:
    mutable std::mutex mutex_;

    bool isInteractive_ = false;
    bool isRemoteMode_ = false;
    bool isPlanMode_ = false;

    bool hasExitedPlanMode_ = false;
    bool needsPlanModeExitAttachment_ = false;
    bool needsAutoModeExitAttachment_ = false;
    bool kairosActive_ = false;

    std::optional<bool> afkModeLatched_;
    std::optional<bool> fastModeLatched_;
    std::optional<bool> cacheEditingHeaderLatched_;
    std::optional<bool> thinkingClearLatched_;

    bool lspRecommendationShownThisSession_ = false;
};

} // namespace claude
