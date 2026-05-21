#include <claude/core/compact/MicroCompact.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude::compact {

const std::vector<String> MicroCompact::HIGH_COMPACT_PRIORITY = {
    "Read", "Bash", "Glob", "Grep", "WebFetch", "WebSearch", "Edit", "Write"
};

int MicroCompact::apply(std::vector<Message>& history) {
    return apply(history, std::chrono::minutes(60), 3);
}

int MicroCompact::apply(std::vector<Message>& history, std::chrono::minutes ageThreshold, int keepLast) {
    if (history.size() <= static_cast<size_t>(keepLast + 1)) return 0;

    int compacted = 0;
    auto now = std::chrono::steady_clock::now();
    size_t skipFrom = history.size() - static_cast<size_t>(keepLast);

    for (size_t i = 1; i < skipFrom; ++i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::ToolResult) continue;

        // Skip compacting messages that carry cache_control breakpoints
        // Compact would invalidate the cache and increase cost
        if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) {
            continue;
        }

        auto age = std::chrono::duration_cast<std::chrono::minutes>(now - msg.timestamp);
        if (age >= ageThreshold && msg.content.length() > 500) {
            String placeholder = createPlaceholder("tool", msg.content.length());
            msg.content = placeholder;
            compacted++;
        }
    }

    if (compacted > 0) {
        spdlog::debug("MicroCompact: compacted {} old tool results", compacted);
    }
    return compacted;
}

int MicroCompact::applyByPressure(std::vector<Message>& history, double usageRatio, int keepLast) {
    if (history.size() <= static_cast<size_t>(keepLast + 1)) return 0;
    if (usageRatio < 0.70) return 0;

    // Determine size threshold based on pressure
    size_t sizeThreshold;
    if (usageRatio >= 0.85) {
        sizeThreshold = 3000;
    } else {
        sizeThreshold = 10000;
    }

    int compacted = 0;
    size_t skipFrom = history.size() - static_cast<size_t>(keepLast);

    for (size_t i = 1; i < skipFrom; ++i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::ToolResult) continue;
        if (msg.content.length() <= sizeThreshold) continue;
        // Skip compacting messages that carry cache_control breakpoints
        // Compact would invalidate the cache and increase cost
        if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) {
            continue;
        }

        // Don't re-compact already compacted messages
        if (msg.content.find("tool result content cleared") != String::npos) continue;

        String placeholder = createPlaceholder("tool", msg.content.length());
        msg.content = placeholder;
        compacted++;
    }

    if (compacted > 0) {
        spdlog::info("MicroCompact (pressure): compacted {} large tool results (ratio={}%, threshold={} chars)",
            compacted, static_cast<int>(usageRatio * 100), sizeThreshold);
    }
    return compacted;
}

int MicroCompact::applyByToolName(std::vector<Message>& history, const std::vector<String>& toolNames, int keepLast) {
    if (history.size() <= static_cast<size_t>(keepLast + 1)) return 0;

    // Build a lookup set for fast matching; empty means compact all tool results
    std::vector<String> names = toolNames;
    const bool filterByTool = !names.empty();

    int compacted = 0;
    size_t skipFrom = history.size() - static_cast<size_t>(keepLast);

    for (size_t i = 1; i < skipFrom; ++i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::ToolResult) continue;

        // Skip already-compacted messages
        if (msg.content.find("tool result content cleared") != String::npos) continue;

        // Skip results smaller than 500 chars
        if (msg.content.length() <= 500) continue;

        // Skip messages with cache_control breakpoints
        if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) {
            continue;
        }

        // Determine the tool name for this message
        // Prefer toolResults[].toolName; fall back to heuristic from content
        String detectedTool;
        if (!msg.toolResults.empty()) {
            detectedTool = msg.toolResults[0].toolName;
        } else {
            // Heuristic: look for common tool output patterns in content
            // e.g. "<tool_name>Read</tool_name>" or leading tool identifier
            auto gtPos = msg.content.find("<tool_name>");
            if (gtPos != String::npos) {
                auto start = gtPos + 11;
                auto end = msg.content.find("</tool_name>", start);
                if (end != String::npos) {
                    detectedTool = msg.content.substr(start, end - start);
                }
            }
        }

        // If filtering by tool name, check if this tool matches
        if (filterByTool) {
            bool matches = false;
            for (const auto& name : names) {
                if (detectedTool == name) { matches = true; break; }
            }
            if (!matches) continue;
        }

        String placeholder = createPlaceholder(detectedTool.empty() ? "tool" : detectedTool, msg.content.length());
        msg.content = placeholder;
        compacted++;
    }

    if (compacted > 0) {
        if (filterByTool) {
            spdlog::info("MicroCompact (tool): compacted {} tool results for targeted tools",
                compacted);
        } else {
            spdlog::info("MicroCompact (tool): compacted {} tool results (all tools)",
                compacted);
        }
    }
    return compacted;
}

bool MicroCompact::shouldCompact(const Message& msg, std::chrono::minutes ageThreshold) {
    if (msg.role != MessageRole::ToolResult) return false;
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::minutes>(now - msg.timestamp);
    return age >= ageThreshold && msg.content.length() > 500;
}

String MicroCompact::createPlaceholder(const String& toolName, size_t originalSize) {
    return "[Old tool result content cleared — original size: " +
           std::to_string(originalSize) + " chars]";
}

} // namespace claude::compact
