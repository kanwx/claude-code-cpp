#include <claude/core/compact/ApiMicroCompact.hpp>
#include <spdlog/spdlog.h>

namespace claude::compact {

bool ApiMicroCompact::shouldTrigger(const Usage& usage, int contextWindow) {
    return usage.promptTokens > 0 &&
           static_cast<double>(usage.promptTokens) / contextWindow > 0.85;
}

long ApiMicroCompact::compact(std::vector<Message>& history) {
    long reclaimed = 0;
    const long TOKENS_PER_CHAR = 4;

    // Remove oldest tool results (not in last 5 messages)
    size_t skipFrom = history.size() > 5 ? history.size() - 5 : 0;
    for (size_t i = 1; i < skipFrom; ++i) {
        auto& msg = history[i];
        if (msg.role == MessageRole::ToolResult && msg.content.length() > 200) {
            // Skip compacting messages that carry cache_control breakpoints
            // Compact would invalidate the cache and increase cost
            if (msg.metadata.count("cache_control") || msg.metadata.count("cache_breakpoint")) {
                continue;
            }
            long saved = static_cast<long>(msg.content.length()) / TOKENS_PER_CHAR;
            msg.content = "[Tool result compacted — " + std::to_string(msg.content.length()) + " chars]";
            reclaimed += saved;
        }
    }

    if (reclaimed > 0) {
        spdlog::debug("ApiMicroCompact: reclaimed ~{} tokens", reclaimed);
    }
    return reclaimed;
}

} // namespace claude::compact
