#pragma once

#include "../core/Types.hpp"
#include <nlohmann/json.hpp>
#include <mutex>
#include <chrono>
#include <vector>

namespace claude {

/// Captures API request/response data when debug mode is active.
/// Used by /debug dump api to show recent API interactions.
class ApiDebugTracker {
public:
    struct Entry {
        String method;       // "call" or "stream"
        String model;
        String provider;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point endTime;
        int inputTokens;
        int outputTokens;
        int httpStatus;
        bool success;
        String error;
    };

    static ApiDebugTracker& instance();

    /// Enable/disable tracking
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /// Record an API call
    void recordCall(const Entry& entry);

    /// Get recent entries (most recent first)
    std::vector<Entry> getRecent(size_t maxCount = 20) const;

    /// Clear all recorded entries
    void clear();

    /// Get summary stats
    Json getStats() const;

private:
    ApiDebugTracker() = default;
    bool enabled_ = false;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    static constexpr size_t MAX_ENTRIES = 100;
};

} // namespace claude
