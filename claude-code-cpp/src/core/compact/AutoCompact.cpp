#include <claude/core/compact/AutoCompact.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/core/compact/MessageGrouping.hpp>
#include <claude/core/compact/SessionMemoryCompact.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <set>

namespace claude::compact {

namespace {

/// Walk backward from `desiredStart` to find a safe turn boundary:
/// a message whose role is User (not ToolResult, not Assistant).
/// Returns the safe start index, clamped to [1, history.size()).
/// Caps expansion at MAX_SAFE_BOUNDARY_EXPAND to avoid keeping
/// too many extra messages.
size_t findSafeTurnBoundary(const std::vector<Message>& history,
                            size_t desiredKeepFrom) {
    if (desiredKeepFrom <= 1) return 1;
    constexpr size_t MAX_EXPAND = 10;
    size_t safe = desiredKeepFrom;
    size_t limit = (desiredKeepFrom > MAX_EXPAND)
                       ? desiredKeepFrom - MAX_EXPAND
                       : 1;
    while (safe > limit) {
        if (history[safe].role == MessageRole::User) {
            return safe;
        }
        --safe;
    }
    // Best effort: return original if no user message found within window
    return desiredKeepFrom;
}

} // anonymous namespace

AutoCompact::AutoCompact(ApiClient& apiClient, int contextWindow)
    : apiClient_(apiClient), contextWindow_(contextWindow) {}

bool AutoCompact::shouldTrigger(long currentTokens) const {
    if (isCircuitOpen()) return false;
    return static_cast<double>(currentTokens) / contextWindow_ >= threshold_;
}

int AutoCompact::getWarningLevel(long currentTokens) const {
    double pct = static_cast<double>(currentTokens) / contextWindow_;
    if (pct >= threshold_) return 2;
    if (pct >= warningThreshold_) return 1;
    return 0;
}

std::optional<std::vector<Message>> AutoCompact::compact(std::vector<Message>& history) {
    if (history.size() <= static_cast<size_t>(keepRecentMessages_ + 1)) {
        return std::nullopt;
    }

    // Use MessageGrouping for smarter topic-based compression
    auto groups = MessageGrouping::group(history);

    // Calculate how many tokens we need to reclaim
    long currentTokens = 0;
    for (const auto& msg : history) {
        currentTokens += static_cast<long>(msg.content.size()) / 4;
    }
    long targetTokens = static_cast<long>(contextWindow_ * (threshold_ - 0.10));
    long tokensToReclaim = currentTokens - targetTokens;

    if (tokensToReclaim <= 0) {
        // Not enough pressure, just do basic trimming
        tokensToReclaim = currentTokens / 3;
    }

    // Get compactable groups sorted by token size
    auto compactableIndices = MessageGrouping::getCompactableGroups(groups, tokensToReclaim);

    // If grouping didn't find enough, fall back to compressing everything before recent
    if (compactableIndices.empty() && history.size() > static_cast<size_t>(keepRecentMessages_ + 1)) {
        // Keep system prompt + last N messages, expanded to safe turn boundary
        std::vector<Message> toCompress;
        std::vector<Message> toKeep;

        toKeep.push_back(history[0]); // system prompt
        size_t desiredKeepFrom = history.size() - static_cast<size_t>(keepRecentMessages_);
        size_t keepFrom = findSafeTurnBoundary(history, desiredKeepFrom);

        // If the boundary message is not a User, no safe boundary was found
        // within the expansion window.  Defer compact rather than risking a
        // mid-turn truncation that corrupts tool ownership.
        if (keepFrom >= history.size() ||
            history[keepFrom].role != MessageRole::User) {
            spdlog::warn("AutoCompact: no safe turn boundary found within "
                         "expansion window (desired={}, found={}, role={}) — "
                         "deferring compact",
                         desiredKeepFrom, keepFrom,
                         keepFrom < history.size()
                             ? static_cast<int>(history[keepFrom].role)
                             : -1);
            return std::nullopt;
        }

        for (size_t i = 1; i < history.size(); ++i) {
            if (i >= keepFrom) {
                toKeep.push_back(std::move(history[i]));
            } else {
                toCompress.push_back(std::move(history[i]));
            }
        }

        if (toCompress.empty()) return std::nullopt;
        return compressAndRebuild(toCompress, toKeep);
    }

    // Selective compression: compress identified groups + any non-important
    // messages between them to keep the compressed section contiguous.
    // If we only compress the selected groups, non-selected groups between
    // them remain as original messages, breaking user/assistant alternation.
    std::vector<Message> toCompress;
    std::vector<Message> toKeep;
    std::set<size_t> compactableSet(compactableIndices.begin(), compactableIndices.end());

    // Find the highest endIndex among selected groups
    size_t maxCompressEnd = 0;
    for (size_t gIdx : compactableIndices) {
        if (groups[gIdx].endIndex > maxCompressEnd) {
            maxCompressEnd = groups[gIdx].endIndex;
        }
    }

    // Build a set of important-group indices for fast lookup
    std::set<size_t> importantMsgIndices;
    for (const auto& g : groups) {
        if (g.isImportant) {
            for (size_t i = g.startIndex; i <= g.endIndex; ++i) {
                importantMsgIndices.insert(i);
            }
        }
    }

    toKeep.push_back(history[0]); // system prompt
    for (size_t i = 1; i < history.size(); ++i) {
        // Compress if: in a selected group, OR below the max compress boundary
        // (but never compress important messages)
        bool inSelected = false;
        for (size_t gIdx : compactableIndices) {
            if (i >= groups[gIdx].startIndex && i <= groups[gIdx].endIndex) {
                inSelected = true;
                break;
            }
        }
        bool inFillRange = (i <= maxCompressEnd) && !importantMsgIndices.count(i);

        if (inSelected || inFillRange) {
            toCompress.push_back(std::move(history[i]));
        } else {
            toKeep.push_back(std::move(history[i]));
        }
    }

    if (toCompress.empty()) return std::nullopt;
    return compressAndRebuild(toCompress, toKeep);
}

std::optional<std::vector<Message>> AutoCompact::compressAndRebuild(
    const std::vector<Message>& toCompress,
    std::vector<Message>& toKeep
) {
    // Build compression prompt using CompactPrompt
    std::ostringstream summaryText;
    for (const auto& msg : toCompress) {
        switch (msg.role) {
            case MessageRole::User: summaryText << "[User] "; break;
            case MessageRole::Assistant: summaryText << "[Assistant] "; break;
            case MessageRole::ToolResult: summaryText << "[Tool Result] "; break;
            default: continue;
        }
        String content = msg.content;
        // Increase truncation limit from 500 → 3000 to preserve more detail
        if (content.length() > 3000) content = content.substr(0, 2997) + "...";
        summaryText << content << "\n\n";
    }

    String summaryPrompt = CompactPrompt::getBasePrompt() +
        "\n\n<conversation>\n" + summaryText.str() + "\n</conversation>\n\n" +
        "Provide a detailed summary following the format above.";

    // Call LLM for compression
    try {
        auto messages = Json::array({
            {{"role", "user"}, {"content", summaryPrompt}}
        });
        auto response = apiClient_.call(messages, {});
        if (response) {
            String compressed;

            // Handle Anthropic format (content blocks)
            if ((*response).contains("content") && (*response)["content"].is_array()) {
                for (const auto& block : (*response)["content"]) {
                    if (block.value("type", "") == "text") {
                        compressed += block["text"].get<String>();
                    }
                }
            }
            // Handle OpenAI format (choices)
            else if ((*response).contains("choices") && !(*response)["choices"].empty()) {
                compressed = (*response)["choices"][0]["message"]["content"].get<String>();
            }

            if (!compressed.empty()) {
                // Extract session memory
                auto facts = SessionMemoryCompact::extractKeyFacts(toCompress);

                std::vector<Message> newHistory;
                newHistory.push_back(toKeep[0]); // system prompt

                // Inject session memory + summary as a single user message
                // to avoid consecutive user messages in pre-cleanup state.
                String combinedUserContent;
                if (!facts.empty()) {
                    String memoryBlock = SessionMemoryCompact::buildMemoryBlock(facts);
                    combinedUserContent = "[Session memory from prior conversation]\n" + memoryBlock + "\n\n";
                }
                combinedUserContent += "[Previous conversation summary]\n\n" + compressed;
                newHistory.push_back(Message::user(combinedUserContent));

                for (size_t i = 1; i < toKeep.size(); ++i) {
                    newHistory.push_back(std::move(toKeep[i]));
                }

                spdlog::debug("AutoCompact: compressed {} messages into summary", toCompress.size());
                recordSuccess();
                return newHistory;
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("AutoCompact: LLM call failed: {}", e.what());
        recordFailure();
    }

    recordFailure();
    return std::nullopt;
}

} // namespace claude::compact
