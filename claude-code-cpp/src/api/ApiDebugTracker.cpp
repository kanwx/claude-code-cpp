#include <claude/api/ApiDebugTracker.hpp>
#include <spdlog/spdlog.h>

namespace claude {

ApiDebugTracker& ApiDebugTracker::instance() {
    static ApiDebugTracker tracker;
    return tracker;
}

void ApiDebugTracker::recordCall(const Entry& entry) {
    if (!enabled_) return;

    std::lock_guard lock(mutex_);
    entries_.push_back(entry);

    // Trim to max size
    while (entries_.size() > MAX_ENTRIES) {
        entries_.erase(entries_.begin());
    }
}

std::vector<ApiDebugTracker::Entry> ApiDebugTracker::getRecent(size_t maxCount) const {
    std::lock_guard lock(mutex_);

    size_t start = entries_.size() > maxCount ? entries_.size() - maxCount : 0;
    std::vector<Entry> result;
    for (size_t i = entries_.size(); i > start; --i) {
        result.push_back(entries_[i - 1]);
    }
    return result;
}

void ApiDebugTracker::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

Json ApiDebugTracker::getStats() const {
    std::lock_guard lock(mutex_);

    int totalCalls = 0;
    int successCalls = 0;
    int failedCalls = 0;
    long totalInputTokens = 0;
    long totalOutputTokens = 0;
    double totalDurationMs = 0;

    for (const auto& e : entries_) {
        totalCalls++;
        if (e.success) successCalls++;
        else failedCalls++;
        totalInputTokens += e.inputTokens;
        totalOutputTokens += e.outputTokens;

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(e.endTime - e.startTime).count();
        totalDurationMs += static_cast<double>(duration);
    }

    return {
        {"totalCalls", totalCalls},
        {"successCalls", successCalls},
        {"failedCalls", failedCalls},
        {"totalInputTokens", totalInputTokens},
        {"totalOutputTokens", totalOutputTokens},
        {"avgDurationMs", totalCalls > 0 ? totalDurationMs / totalCalls : 0},
        {"trackedEntries", entries_.size()}
    };
}

} // namespace claude
