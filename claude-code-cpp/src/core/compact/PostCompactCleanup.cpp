#include <claude/core/compact/PostCompactCleanup.hpp>
#include <algorithm>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace claude::compact {

void PostCompactCleanup::cleanup(std::vector<Message>& history) {
    int removed = removeEmptyMessages(history);
    removed += deduplicateSystemMessages(history);
    removed += removeOrphanedResults(history);
    int fixed = enforceAlternation(history);

    // Add compact boundary marker so the model knows what was preserved
    history.push_back(Message::user(
        "[System: Context was compacted. The most recent messages are preserved. "
         "Older conversation has been summarized. You still have access to "
         "session memory and the current working context.]"));
    history.push_back(Message::assistant(
        "Understood. I'll continue working with the compressed context, "
        "referencing session memory as needed."));

    if (removed > 0 || fixed > 0) {
        spdlog::debug("PostCompactCleanup: cleaned {} messages, fixed {} alternation issues",
            removed, fixed);
    }
}

int PostCompactCleanup::removeOrphanedResults(std::vector<Message>& history) {
    std::unordered_set<String> callIds;
    for (const auto& msg : history) {
        for (const auto& tc : msg.toolCalls) {
            callIds.insert(tc.id);
        }
    }

    size_t before = history.size();
    history.erase(
        std::remove_if(history.begin(), history.end(),
            [&](const Message& msg) {
                if (msg.role != MessageRole::ToolResult) return false;
                for (const auto& tr : msg.toolResults) {
                    if (callIds.count(tr.callId)) return false;
                }
                return true;
            }),
        history.end());

    return static_cast<int>(before - history.size());
}

int PostCompactCleanup::deduplicateSystemMessages(std::vector<Message>& history) {
    int removed = 0;
    auto it = history.begin();
    while (it != history.end()) {
        if (it->role == MessageRole::System) {
            auto next = std::next(it);
            while (next != history.end() && next->role == MessageRole::System) {
                it = history.erase(next);
                next = std::next(it);
                removed++;
            }
        }
        ++it;
    }
    return removed;
}

int PostCompactCleanup::removeEmptyMessages(std::vector<Message>& history) {
    size_t before = history.size();
    history.erase(
        std::remove_if(history.begin(), history.end(),
            [](const Message& msg) {
                return msg.content.empty() && msg.toolCalls.empty() && msg.toolResults.empty();
            }),
        history.end());
    return static_cast<int>(before - history.size());
}

int PostCompactCleanup::enforceAlternation(std::vector<Message>& history) {
    if (history.empty()) return 0;

    int fixes = 0;
    // After the system message, we need strict user/assistant alternation.
    // For Anthropic API: tool_result messages follow an assistant with tool_calls,
    // then must be followed by assistant or user.
    // Strategy: merge consecutive same-role messages, or insert filler.

    size_t startIdx = 0;
    if (history[0].role == MessageRole::System) {
        startIdx = 1;
    }

    for (size_t i = startIdx; i < history.size(); ++i) {
        auto& current = history[i];
        auto& prev = history[i - 1];

        // Determine effective roles for alternation
        // ToolResult follows assistant (valid), then next must be user or assistant
        auto effectiveRole = [](const Message& msg) -> MessageRole {
            if (msg.role == MessageRole::ToolResult) return MessageRole::User;
            return msg.role;
        };

        MessageRole curEff = effectiveRole(current);
        MessageRole prevEff = effectiveRole(prev);

        // ToolResult after Assistant is always valid (tool results follow tool calls)
        if (current.role == MessageRole::ToolResult && prev.role == MessageRole::Assistant) {
            continue;
        }

        // Same effective role after another — merge or insert
        if (curEff == prevEff && i > startIdx) {
            // Merge consecutive user messages
            if (curEff == MessageRole::User) {
                if (!current.content.empty()) {
                    if (!prev.content.empty()) {
                        prev.content += "\n\n";
                    }
                    prev.content += current.content;
                }
                // Merge toolResults so paired tool_use/tool_result invariant holds
                for (auto& tr : current.toolResults) {
                    prev.toolResults.push_back(std::move(tr));
                }
                history.erase(history.begin() + static_cast<ptrdiff_t>(i));
                i--; // re-check this position
                fixes++;
                continue;
            }

            // Merge consecutive assistant messages
            if (curEff == MessageRole::Assistant) {
                prev.content += "\n\n" + current.content;
                // Merge tool calls too
                for (auto& tc : current.toolCalls) {
                    prev.toolCalls.push_back(std::move(tc));
                }
                history.erase(history.begin() + static_cast<ptrdiff_t>(i));
                i--;
                fixes++;
                continue;
            }
        }

        // User/Assistant out of order: insert filler assistant to maintain alternation
        // e.g., user → user → insert assistant between them
        if (curEff == MessageRole::User && prevEff == MessageRole::User && i > startIdx) {
            history.insert(history.begin() + static_cast<ptrdiff_t>(i),
                Message::assistant("[Continuing from previous response]"));
            fixes++;
            i++; // skip the inserted message
            continue;
        }

        // assistant after tool_result without a user in between is fine
        // (Anthropic allows assistant after tool_result)
    }

    // Ensure the first non-system message is user
    if (startIdx < history.size() && history[startIdx].role != MessageRole::User) {
        history.insert(history.begin() + static_cast<ptrdiff_t>(startIdx),
            Message::user("[Conversation continued from prior context]"));
        fixes++;
    }

    if (fixes > 0) {
        spdlog::debug("PostCompactCleanup: enforced alternation with {} fixes", fixes);
    }
    return fixes;
}

} // namespace claude::compact
