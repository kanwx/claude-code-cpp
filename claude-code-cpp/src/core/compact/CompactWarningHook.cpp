#include <claude/core/compact/CompactWarningHook.hpp>
#include <spdlog/spdlog.h>

namespace claude::compact {

void CompactWarningHook::check(long currentTokens, long maxTokens) {
    if (!callback_ || maxTokens <= 0) return;

    double pct = static_cast<double>(currentTokens) / maxTokens;
    int level = 0;
    if (pct >= 0.93) level = 2;
    else if (pct >= 0.80) level = 1;

    if (level > lastLevel_) {
        callback_(level, currentTokens, maxTokens);
        spdlog::debug("CompactWarning: level {} ({}% of context)", level, static_cast<int>(pct * 100));
    }
    lastLevel_ = level;
}

} // namespace claude::compact
