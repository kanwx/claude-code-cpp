#pragma once
#include <claude/core/Types.hpp>
#include <chrono>
#include <vector>

namespace claude::compact {

class MicroCompact {
public:
    /// Tool names whose results should be compacted first when under pressure.
    static const std::vector<String> HIGH_COMPACT_PRIORITY;

    /// Tool names whose results are HIGH importance (compact last, preserve longest).
    static const std::vector<String> HIGH_IMPORTANCE_TOOLS;

    /// Tool names whose results are LOW importance (compact first).
    static const std::vector<String> LOW_IMPORTANCE_TOOLS;

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

    /// Importance-aware compaction: compact low-importance tools first,
    /// then medium, then high. Respects cache breakpoints.
    /// Returns number of messages compacted.
    static int applyByImportance(std::vector<Message>& history, double usageRatio, int keepLast = 5);

    /// Get importance score for a tool name (0=lowest, 10=highest).
    static int importanceScore(const String& toolName);

    static bool shouldCompact(const Message& msg, std::chrono::minutes ageThreshold);
    static String createPlaceholder(const String& toolName, size_t originalSize);
};

} // namespace claude::compact
