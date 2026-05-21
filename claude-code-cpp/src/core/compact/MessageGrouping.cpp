#include <claude/core/compact/MessageGrouping.hpp>
#include <algorithm>

namespace claude::compact {

std::vector<MessageGroup> MessageGrouping::group(const std::vector<Message>& history) {
    std::vector<MessageGroup> groups;
    if (history.empty()) return groups;

    size_t start = 1; // Skip system prompt
    int currentRound = history.size() > 1 ? history[1].apiRound : 0;
    long groupTokens = 0;
    const long TOKENS_PER_CHAR = 4;

    for (size_t i = 1; i < history.size(); ++i) {
        const auto& msg = history[i];
        long msgTokens = static_cast<long>(msg.content.length()) / TOKENS_PER_CHAR;
        // Also count tool call/result tokens
        for (const auto& tc : msg.toolCalls) {
            msgTokens += static_cast<long>(tc.arguments.length()) / TOKENS_PER_CHAR;
        }
        for (const auto& tr : msg.toolResults) {
            msgTokens += static_cast<long>(tr.content.length()) / TOKENS_PER_CHAR;
        }

        bool roundChanged = false;
        if (msg.apiRound != currentRound && currentRound >= 0 &&
            (msg.role == MessageRole::User || msg.role == MessageRole::Assistant)) {
            roundChanged = true;
        }

        if (roundChanged && i > start) {
            // Close current group at the API round boundary
            groups.push_back({start, i - 1, "", groupTokens, false,
                              history[start].apiRound, currentRound});
            start = i;
            groupTokens = 0;
        }

        currentRound = msg.apiRound;
        groupTokens += msgTokens;
    }

    // Final group
    if (start < history.size()) {
        groups.push_back({start, history.size() - 1, "", groupTokens, false,
                          history[start].apiRound, currentRound});
    }

    // Mark last 2 groups as important
    for (int i = static_cast<int>(groups.size()) - 1; i >= 0 && i >= static_cast<int>(groups.size()) - 2; --i) {
        groups[i].isImportant = true;
    }

    // Also mark groups that contain messages within last 5
    if (history.size() >= 5) {
        size_t recentStart = history.size() - 5;
        for (auto& g : groups) {
            if (g.endIndex >= recentStart) {
                g.isImportant = true;
            }
        }
    }

    return groups;
}

std::vector<size_t> MessageGrouping::getCompactableGroups(const std::vector<MessageGroup>& groups, long tokensToReclaim) {
    std::vector<std::pair<long, size_t>> candidates;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!groups[i].isImportant && groups[i].estimatedTokens > 0) {
            candidates.emplace_back(groups[i].estimatedTokens, i);
        }
    }

    // Sort by token size descending (compact largest first)
    std::sort(candidates.begin(), candidates.end(), std::greater<>());

    std::vector<size_t> result;
    long reclaimed = 0;
    for (const auto& [tokens, idx] : candidates) {
        if (reclaimed >= tokensToReclaim) break;
        result.push_back(idx);
        reclaimed += tokens;
    }

    return result;
}

} // namespace claude::compact
