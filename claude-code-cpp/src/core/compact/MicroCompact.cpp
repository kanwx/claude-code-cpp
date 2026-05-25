#include <claude/core/compact/MicroCompact.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude::compact {

const std::vector<String> MicroCompact::HIGH_COMPACT_PRIORITY = {
    "Read", "Bash", "Glob", "Grep", "WebFetch", "WebSearch", "Edit", "Write"
};

const std::vector<String> MicroCompact::HIGH_IMPORTANCE_TOOLS = {
    "Edit", "Write", "NotebookEdit", "Agent"
};

const std::vector<String> MicroCompact::LOW_IMPORTANCE_TOOLS = {
    "Bash", "Glob", "Grep", "WebFetch", "WebSearch", "ListFiles"
};

int MicroCompact::importanceScore(const String& toolName) {
    // High importance: edits, writes, agents — these are the user's actual work
    for (const auto& t : HIGH_IMPORTANCE_TOOLS) {
        if (toolName == t) return 10;
    }
    // Medium importance: reads — context needed for current task
    if (toolName == "Read") return 7;
    // Low importance: search/list — can be re-derived
    for (const auto& t : LOW_IMPORTANCE_TOOLS) {
        if (toolName == t) return 3;
    }
    // Unknown tools get medium importance
    return 5;
}

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
        size_t contentLen = 0;
        for (const auto& tr : msg.toolResults) contentLen += tr.content.length();
        if (contentLen == 0) contentLen = msg.content.length();
        if (age >= ageThreshold && contentLen > 500) {
            String toolName;
            for (const auto& tr : msg.toolResults) { if (!tr.toolName.empty()) { toolName = tr.toolName; break; } }
            String placeholder = createPlaceholder(toolName.empty() ? "tool" : toolName, contentLen);
            msg.content = placeholder;
            for (auto& tr : msg.toolResults) { tr.content = placeholder; }
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

        // Content lives in msg.toolResults[].content for proper ToolResult messages
        size_t contentLen = 0;
        String toolName;
        for (const auto& tr : msg.toolResults) {
            contentLen += tr.content.length();
            if (toolName.empty()) toolName = tr.toolName;
        }
        // Fallback: some code paths may put content in msg.content directly
        if (contentLen == 0) contentLen = msg.content.length();

        if (contentLen <= sizeThreshold) continue;
        // Skip compacting messages that carry cache_control breakpoints
        // Compact would invalidate the cache and increase cost
        if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) {
            continue;
        }

        // Don't re-compact already compacted messages
        if (msg.content.find("tool result content cleared") != String::npos) continue;
        bool alreadyCompacted = false;
        for (const auto& tr : msg.toolResults) {
            if (tr.content.find("tool result content cleared") != String::npos) {
                alreadyCompacted = true; break;
            }
        }
        if (alreadyCompacted) continue;

        String placeholder = createPlaceholder(toolName.empty() ? "tool" : toolName, contentLen);
        msg.content = placeholder;
        for (auto& tr : msg.toolResults) { tr.content = placeholder; }
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

        // Skip already-compacted messages (check both msg.content and toolResults content)
        if (msg.content.find("tool result content cleared") != String::npos) continue;
        bool alreadyCompacted = false;
        for (const auto& tr : msg.toolResults) {
            if (tr.content.find("tool result content cleared") != String::npos) {
                alreadyCompacted = true; break;
            }
        }
        if (alreadyCompacted) continue;

        // Content lives in msg.toolResults[].content for proper ToolResult messages
        size_t contentLen = 0;
        String detectedTool;
        for (const auto& tr : msg.toolResults) {
            contentLen += tr.content.length();
            if (detectedTool.empty()) detectedTool = tr.toolName;
        }
        // Fallback: some code paths may put content in msg.content directly
        if (contentLen == 0) {
            contentLen = msg.content.length();
            // Heuristic: look for common tool output patterns in content
            auto gtPos = msg.content.find("<tool_name>");
            if (gtPos != String::npos) {
                auto start = gtPos + 11;
                auto end = msg.content.find("</tool_name>", start);
                if (end != String::npos) {
                    detectedTool = msg.content.substr(start, end - start);
                }
            }
        }

        // Skip results smaller than 500 chars
        if (contentLen <= 500) continue;

        // Skip messages with cache_control breakpoints
        if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) {
            continue;
        }

        // If filtering by tool name, check if this tool matches
        if (filterByTool) {
            bool matches = false;
            for (const auto& name : names) {
                if (detectedTool == name) { matches = true; break; }
            }
            if (!matches) continue;
        }

        String placeholder = createPlaceholder(detectedTool.empty() ? "tool" : detectedTool, contentLen);
        msg.content = placeholder;
        for (auto& tr : msg.toolResults) { tr.content = placeholder; }
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
    size_t contentLen = 0;
    for (const auto& tr : msg.toolResults) contentLen += tr.content.length();
    if (contentLen == 0) contentLen = msg.content.length();
    return age >= ageThreshold && contentLen > 500;
}

String MicroCompact::createPlaceholder(const String& toolName, size_t originalSize) {
    return "[Old tool result content cleared — original size: " +
           std::to_string(originalSize) + " chars]";
}

int MicroCompact::applyByImportance(std::vector<Message>& history, double usageRatio, int keepLast) {
    if (history.size() <= static_cast<size_t>(keepLast + 1)) return 0;
    if (usageRatio < 0.70) return 0;

    size_t skipFrom = history.size() - static_cast<size_t>(keepLast);

    // Collect compactable messages with their importance scores
    struct Candidate {
        size_t index;
        int score;
        size_t contentSize;
    };
    std::vector<Candidate> candidates;

    for (size_t i = 1; i < skipFrom; ++i) {
        auto& msg = history[i];
        if (msg.role != MessageRole::ToolResult) continue;
        if (msg.content.find("tool result content cleared") != String::npos) continue;
        bool alreadyCompacted = false;
        for (const auto& tr : msg.toolResults) {
            if (tr.content.find("tool result content cleared") != String::npos) {
                alreadyCompacted = true; break;
            }
        }
        if (alreadyCompacted) continue;
        if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) continue;

        size_t contentLen = 0;
        String toolName;
        for (const auto& tr : msg.toolResults) {
            contentLen += tr.content.length();
            if (toolName.empty()) toolName = tr.toolName;
        }
        if (contentLen == 0) contentLen = msg.content.length();
        if (contentLen <= 500) continue;

        candidates.push_back({i, importanceScore(toolName), contentLen});
    }

    if (candidates.empty()) return 0;

    // Sort by importance score (lowest first = compact first), then by size (largest first)
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.contentSize > b.contentSize;
    });

    // Determine how many to compact based on pressure
    int targetCompactions;
    if (usageRatio >= 0.90) {
        targetCompactions = static_cast<int>(candidates.size());
    } else if (usageRatio >= 0.85) {
        targetCompactions = static_cast<int>(candidates.size() * 0.75);
    } else {
        targetCompactions = static_cast<int>(candidates.size() * 0.5);
    }

    // Only compact low-importance results at lower pressure
    if (usageRatio < 0.85) {
        // Skip high-importance tools at lower pressure
        auto it = std::remove_if(candidates.begin(), candidates.begin() + targetCompactions,
            [](const Candidate& c) { return c.score >= 10; });
        targetCompactions = static_cast<int>(it - candidates.begin());
    }

    int compacted = 0;
    for (int i = 0; i < targetCompactions && i < static_cast<int>(candidates.size()); ++i) {
        auto& msg = history[candidates[i].index];
        String toolName;
        if (!msg.toolResults.empty()) toolName = msg.toolResults[0].toolName;
        String placeholder = createPlaceholder(toolName.empty() ? "tool" : toolName, candidates[i].contentSize);
        msg.content = placeholder;
        for (auto& tr : msg.toolResults) { tr.content = placeholder; }
        compacted++;
    }

    if (compacted > 0) {
        spdlog::info("MicroCompact (importance): compacted {} results (pressure={}%, scored by importance)",
            compacted, static_cast<int>(usageRatio * 100));
    }
    return compacted;
}

} // namespace claude::compact
