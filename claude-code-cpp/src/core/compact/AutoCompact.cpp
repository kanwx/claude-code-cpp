#include <claude/core/compact/AutoCompact.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/core/compact/MessageGrouping.hpp>
#include <claude/core/compact/SessionMemoryCompact.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude::compact {

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
        // Keep system prompt + last N messages
        std::vector<Message> toCompress;
        std::vector<Message> toKeep;

        toKeep.push_back(history[0]); // system prompt
        size_t keepFrom = history.size() - static_cast<size_t>(keepRecentMessages_);
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

    // Selective compression: only compress the identified groups
    std::vector<Message> toCompress;
    std::vector<Message> toKeep;
    std::set<size_t> compactableSet(compactableIndices.begin(), compactableIndices.end());

    toKeep.push_back(history[0]); // system prompt
    for (size_t i = 1; i < history.size(); ++i) {
        // Check if this message belongs to a compactable group
        bool inCompactableGroup = false;
        for (size_t gIdx : compactableIndices) {
            if (i >= groups[gIdx].startIndex && i <= groups[gIdx].endIndex) {
                inCompactableGroup = true;
                break;
            }
        }
        if (inCompactableGroup) {
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

                // Inject session memory if available
                if (!facts.empty()) {
                    String memoryBlock = SessionMemoryCompact::buildMemoryBlock(facts);
                    newHistory.push_back(Message::user(
                        "[Session memory from prior conversation]\n" + memoryBlock));
                    newHistory.push_back(Message::assistant(
                        "I understand the session memory. I'll reference these facts as needed."));
                }

                newHistory.push_back(Message::user(
                    "[Previous conversation summary]\n\n" + compressed));
                newHistory.push_back(Message::assistant(
                    "I understand the conversation summary. I'll continue from here with full context of what we've discussed."));

                for (size_t i = 1; i < toKeep.size(); ++i) {
                    newHistory.push_back(std::move(toKeep[i]));
                }

                spdlog::info("AutoCompact: compressed {} messages into summary", toCompress.size());
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
