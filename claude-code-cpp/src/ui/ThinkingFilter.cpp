#include "claude/ui/ThinkingFilter.hpp"

namespace claude {

// Tier 1: Remove all redacted thinking blocks.
// These contain no useful content and should never be shown to the user.
std::vector<DisplayMessage> ThinkingFilter::removeRedactedThinking(
    const std::vector<DisplayMessage>& messages) {
    std::vector<DisplayMessage> result;
    result.reserve(messages.size());
    for (const auto& msg : messages) {
        if (msg.type != DisplayMessage::Type::AssistantRedactedThinking) {
            result.push_back(msg);
        }
    }
    return result;
}

// Tier 2: Remove consecutive AssistantThinking messages at the tail.
// When an assistant turn ends with thinking blocks and no subsequent
// substantive output follows, those trailing thinking blocks are noise.
std::vector<DisplayMessage> ThinkingFilter::stripTrailingThinking(
    const std::vector<DisplayMessage>& messages) {
    if (messages.empty()) return {};

    // Walk backward from end, skipping consecutive AssistantThinking
    auto it = messages.rbegin();
    while (it != messages.rend() && it->type == DisplayMessage::Type::AssistantThinking) {
        ++it;
    }
    auto trailingCount = static_cast<size_t>(std::distance(messages.rbegin(), it));
    if (trailingCount == 0) return messages;

    // Return everything before the trailing thinking run
    return {messages.begin(), messages.end() - static_cast<ptrdiff_t>(trailingCount)};
}

// Tier 3: Remove orphaned thinking — runs of consecutive AssistantThinking
// that are NOT adjacent to any "substance" message. Substance types are
// those that carry real content the user needs to see.
std::vector<DisplayMessage> ThinkingFilter::removeOrphanedThinking(
    const std::vector<DisplayMessage>& messages) {
    auto isSubstance = [](DisplayMessage::Type type) {
        switch (type) {
            case DisplayMessage::Type::AssistantText:
            case DisplayMessage::Type::AssistantToolUse:
            case DisplayMessage::Type::UserToolResult:
            case DisplayMessage::Type::UserToolSuccess:
            case DisplayMessage::Type::UserToolError:
            case DisplayMessage::Type::UserToolRejected:
            case DisplayMessage::Type::UserToolCanceled:
            case DisplayMessage::Type::GroupedToolUse:
            case DisplayMessage::Type::CollapsedReadSearch:
            case DisplayMessage::Type::UserPrompt:
                return true;
            default:
                return false;
        }
    };

    auto isThinking = [](DisplayMessage::Type type) {
        return type == DisplayMessage::Type::AssistantThinking;
    };

    // Build a mask: true = keep this message
    std::vector<bool> keep(messages.size(), true);

    // Find runs of consecutive thinking messages and check if they
    // are adjacent to any substance message (before or after the run).
    size_t i = 0;
    while (i < messages.size()) {
        if (!isThinking(messages[i].type)) {
            ++i;
            continue;
        }

        // Start of a thinking run — find its extent
        size_t runStart = i;
        while (i < messages.size() && isThinking(messages[i].type)) {
            ++i;
        }
        size_t runEnd = i; // one-past-end

        // Check if adjacent to substance (before runStart or at runEnd)
        bool adjacentBefore = (runStart > 0 && isSubstance(messages[runStart - 1].type));
        bool adjacentAfter = (runEnd < messages.size() && isSubstance(messages[runEnd].type));

        if (!adjacentBefore && !adjacentAfter) {
            // Orphaned — mark for removal
            for (size_t j = runStart; j < runEnd; ++j) {
                keep[j] = false;
            }
        }
    }

    // Build filtered result
    std::vector<DisplayMessage> result;
    result.reserve(messages.size());
    for (size_t j = 0; j < messages.size(); ++j) {
        if (keep[j]) {
            result.push_back(messages[j]);
        }
    }
    return result;
}

// Apply all three tiers in order: redacted → trailing → orphaned
std::vector<DisplayMessage> ThinkingFilter::apply(
    const std::vector<DisplayMessage>& messages) {
    auto pass1 = removeRedactedThinking(messages);
    auto pass2 = stripTrailingThinking(pass1);
    return removeOrphanedThinking(pass2);
}

} // namespace claude
