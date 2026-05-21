#pragma once
#include <claude/core/Types.hpp>
#include <vector>

namespace claude::compact {

struct MessageGroup {
    size_t startIndex;
    size_t endIndex;
    String topic;
    long estimatedTokens;
    bool isImportant;
    int apiRoundStart = 0;  // First API round in this group
    int apiRoundEnd = 0;    // Last API round in this group
};

class MessageGrouping {
public:
    static std::vector<MessageGroup> group(const std::vector<Message>& history);
    static std::vector<size_t> getCompactableGroups(const std::vector<MessageGroup>& groups, long tokensToReclaim);
};

} // namespace claude::compact
