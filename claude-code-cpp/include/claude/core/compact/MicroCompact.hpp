#pragma once
#include <claude/core/Types.hpp>
#include <chrono>
#include <vector>

namespace claude::compact {

class MicroCompact {
public:
    /// Tool names whose results should be compacted first when under pressure.
    static const std::vector<String> HIGH_COMPACT_PRIORITY;

    /// Default: time-based (60 min) + keep last 3
    static int apply(std::vector<Message>& history);

    /// Time-based with custom threshold
    static int apply(std::vector<Message>& history, std::chrono::minutes ageThreshold, int keepLast = 3);

    /// Context-pressure-based: compact large tool results based on usage ratio.
    /// usageRatio = currentTokens / contextWindow. Higher pressure = more aggressive.
    /// At >= 0.70: compact results > 10000 chars (keep last 5)
    /// At >= 0.85: compact results > 3000 chars (keep last 5)
    static int applyByPressure(std::vector<Message>& history, double usageRatio, int keepLast = 5);

    /// Tool-name-based: compact results from specific tool types.
    /// If toolNames is empty, compact all tool results.
    /// If toolNames is non-empty, only compact results from matching tools.
    /// Skips already-compacted results (containing "[Old tool result content cleared").
    /// Skips results smaller than 500 chars. Keeps last 5 messages.
    static int applyByToolName(std::vector<Message>& history, const std::vector<String>& toolNames, int keepLast = 5);

    static bool shouldCompact(const Message& msg, std::chrono::minutes ageThreshold);
    static String createPlaceholder(const String& toolName, size_t originalSize);
};

} // namespace claude::compact
