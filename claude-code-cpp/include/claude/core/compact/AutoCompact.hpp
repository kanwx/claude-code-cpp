#pragma once
#include <claude/core/Types.hpp>
#include <claude/api/ApiClient.hpp>
#include <optional>
#include <vector>

namespace claude::compact {

class AutoCompact {
public:
    explicit AutoCompact(ApiClient& apiClient, int contextWindow);
    bool shouldTrigger(long currentTokens) const;
    std::optional<std::vector<Message>> compact(std::vector<Message>& history);
    int getWarningLevel(long currentTokens) const;
    void setThreshold(double t) { threshold_ = t; }
    void setKeepRecentMessages(int n) { keepRecentMessages_ = n; }
    void setMaxCompactTokens(int t) { maxCompactTokens_ = t; }

    // Circuit breaker
    static constexpr int MAX_CONSECUTIVE_FAILURES = 3;
    int getConsecutiveFailures() const { return consecutiveFailures_; }
    bool isCircuitOpen() const { return consecutiveFailures_ >= MAX_CONSECUTIVE_FAILURES; }
    void recordFailure() { ++consecutiveFailures_; }
    void recordSuccess() { consecutiveFailures_ = 0; }

private:
    /// Build compressed history from messages to compress + messages to keep
    std::optional<std::vector<Message>> compressAndRebuild(
        const std::vector<Message>& toCompress,
        std::vector<Message>& toKeep
    );

    ApiClient& apiClient_;
    int contextWindow_;
    double threshold_ = 0.82;    // Trigger auto-compact at 82% context (was 93%)
    double warningThreshold_ = 0.65;  // Warning at 65% context (was 80%)
    int keepRecentMessages_ = 5;
    int maxCompactTokens_ = 50000;
    int consecutiveFailures_ = 0;
};

} // namespace claude::compact
