#include <claude/core/AgentLoopImpl.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/core/compact/MicroCompact.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/compact/SessionMemoryCompact.hpp>
#include <claude/core/compact/ApiMicroCompact.hpp>
#include <spdlog/spdlog.h>

namespace claude {

// ============================================================================
// Compact operations
// ============================================================================

void AgentLoop::applyMicrocompact() {
    std::lock_guard lock(impl_->historyMutex);
    // Use MicroCompact for age-based tool result clearing
    int compacted = compact::MicroCompact::apply(impl_->messageHistory);
    if (compacted > 0) {
        spdlog::debug("Microcompact: cleared {} old tool result content fields", compacted);
    }

    // Context-pressure-based micro-compact: compact large results when window is filling
    double usageRatio = impl_->tokenTracker.getUsagePercentage();
    if (usageRatio >= 0.70) {
        int pressureCompacted = compact::MicroCompact::applyByPressure(impl_->messageHistory, usageRatio);
        if (pressureCompacted > 0) {
            compacted += pressureCompacted;
        }
    }

    // Also check for API streaming micro-compact (prompt >85% of context window)
    long promptTokens = impl_->tokenTracker.getInputTokens();
    long apiContextWindow = impl_->tokenTracker.getUsagePercentage() > 0
        ? static_cast<long>(impl_->tokenTracker.getTotalTokens() / impl_->tokenTracker.getUsagePercentage())
        : TokenTracker::DEFAULT_CONTEXT_WINDOW;
    if (apiContextWindow > 0 && compact::ApiMicroCompact::shouldTrigger(
            Usage{promptTokens, 0, 0}, static_cast<int>(apiContextWindow))) {
        long reclaimed = compact::ApiMicroCompact::compact(impl_->messageHistory);
        if (reclaimed > 0) {
            spdlog::debug("ApiMicroCompact: reclaimed ~{} tokens", reclaimed);
        }
    }
}

bool AgentLoop::applyAutoCompact() {
    // ========== Compact warning hook ==========
    long currentTokens = impl_->tokenTracker.getInputTokens();
    long contextWindow = impl_->tokenTracker.getUsagePercentage() > 0
        ? static_cast<long>(impl_->tokenTracker.getTotalTokens() / impl_->tokenTracker.getUsagePercentage())
        : TokenTracker::DEFAULT_CONTEXT_WINDOW;
    impl_->compactWarningHook.check(currentTokens, contextWindow);

    // ========== Auto-compact: use AutoCompact class if initialized ==========
    // autoCompact is set once via initAutoCompact() before the loop starts,
    // so reading it without lock is safe. We guard the check under callbackMutex
    // to be formally correct with respect to concurrent initAutoCompact() calls.
    bool hasAutoCompact = false;
    {
        std::lock_guard lock(impl_->callbackMutex);
        hasAutoCompact = impl_->autoCompact.has_value();
    }
    if (hasAutoCompact && impl_->autoCompact->shouldTrigger(currentTokens)) {
        spdlog::debug("Auto-compact triggered: usage at {:.1f}% of context window",
            static_cast<double>(currentTokens) / contextWindow * 100.0);

        std::vector<Message> historySnapshot;
        {
            std::lock_guard lock(impl_->historyMutex);
            historySnapshot = impl_->messageHistory;
        }
        auto newHistory = impl_->autoCompact->compact(historySnapshot);
        if (newHistory) {
            // Post-compact cleanup
            compact::PostCompactCleanup::cleanup(*newHistory);

            // Extract session memory from compacted messages
            auto facts = compact::SessionMemoryCompact::extractKeyFacts(historySnapshot);
            if (!facts.empty()) {
                String memoryBlock = compact::SessionMemoryCompact::buildMemoryBlock(facts);
                spdlog::debug("Auto-compact: extracted {} key facts into memory block", facts.size());
                if (newHistory->size() > 2) {
                    newHistory->insert(newHistory->end() - 2,
                        Message::user("[Session memory from prior conversation]\n" + memoryBlock));
                }
            }

            std::lock_guard lock(impl_->historyMutex);
            size_t oldSize = impl_->messageHistory.size();
            impl_->messageHistory = std::move(*newHistory);

            long estimatedNewTokens = 0;
            for (const auto& msg : impl_->messageHistory) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
                for (const auto& tc : msg.toolCalls) {
                    estimatedNewTokens += static_cast<long>(tc.arguments.size()) / 4;
                }
                for (const auto& tr : msg.toolResults) {
                    estimatedNewTokens += static_cast<long>(tr.content.size()) / 4;
                }
            }
            impl_->tokenTracker.adjustAfterCompaction(estimatedNewTokens);

            spdlog::debug("Auto-compact completed: {} messages -> {} messages",
                oldSize, impl_->messageHistory.size());
            return true;
        }
    }

    // ========== Fallback: use LLM-based compaction when autoCompact is not initialized ==========
    if (!impl_->tokenTracker.shouldAutoCompact()) {
        return false;
    }

    // Snapshot history under lock, then release lock for the API call
    std::vector<Message> toCompressSnapshot;
    std::vector<Message> recentMsgsSnapshot;
    Message systemPromptMsg;
    {
        std::lock_guard lock(impl_->historyMutex);
        if (impl_->messageHistory.size() <= 3) {
            spdlog::debug("Auto-compact: too few messages to compress");
            return false;
        }

        size_t keepRecent = 5;
        if (impl_->messageHistory.size() <= keepRecent + 1) {
            spdlog::debug("Auto-compact: not enough messages beyond recent to compress");
            return false;
        }

        systemPromptMsg = impl_->messageHistory[0];
        toCompressSnapshot.assign(impl_->messageHistory.begin() + 1,
            impl_->messageHistory.end() - keepRecent);
        recentMsgsSnapshot.assign(impl_->messageHistory.end() - keepRecent,
            impl_->messageHistory.end());
    }

    // Build compression prompt from snapshot (no lock needed)
    String compressText;
    for (const auto& msg : toCompressSnapshot) {
        String role;
        switch (msg.role) {
            case MessageRole::User: role = "User"; break;
            case MessageRole::Assistant: role = "Assistant"; break;
            case MessageRole::ToolResult: role = "ToolResult"; break;
            default: role = "System"; break;
        }
        String content = msg.content;
        if (content.size() > 2000) {
            content = content.substr(0, 1997) + "...";
        }
        compressText += role + ": " + content + "\n\n";
    }

    String summaryPrompt = compact::CompactPrompt::getBasePrompt() +
        "\n\n<conversation>\n" + compressText + "\n</conversation>\n\n" +
        "Provide a detailed summary following the format above.";

    // Call LLM for summary (no lock during API call)
    Json summaryMessages = Json::array();
    summaryMessages.push_back({{"role", "user"}, {"content", summaryPrompt}});

    Json noTools = Json::array();

    auto llmResult = impl_->apiClient.call(summaryMessages, noTools);
    if (!llmResult) {
        spdlog::warn("Auto-compact LLM call failed: {}", llmResult.error());
        return false;
    }

    String summary;
    if (llmResult->contains("choices") && (*llmResult)["choices"].is_array() && !(*llmResult)["choices"].empty()) {
        const auto& firstChoice = (*llmResult)["choices"][0];
        if (firstChoice.is_object() && firstChoice.contains("message") && firstChoice["message"].is_object()
            && firstChoice["message"].contains("content") && firstChoice["message"]["content"].is_string()) {
            summary = firstChoice["message"]["content"].get<String>();
        }
    } else if (llmResult->contains("content") && (*llmResult)["content"].is_array() && !(*llmResult)["content"].empty()) {
        const auto& blocks = (*llmResult)["content"];
        for (const auto& block : blocks) {
            if (block.is_object() && block.value("type", "") == "text"
                && block.contains("text") && block["text"].is_string()) {
                summary += block["text"].get<String>();
            }
        }
    }

    if (summary.empty()) {
        spdlog::warn("Auto-compact: LLM returned empty summary");
        return false;
    }

    // Extract session memory from compacted messages
    auto facts = compact::SessionMemoryCompact::extractKeyFacts(toCompressSnapshot);

    // Build new history
    std::vector<Message> newHistory;
    newHistory.push_back(systemPromptMsg);

    if (!facts.empty()) {
        String memoryBlock = compact::SessionMemoryCompact::buildMemoryBlock(facts);
        newHistory.push_back(Message::user(
            "[Session memory from prior conversation]\n" + memoryBlock));
        newHistory.push_back(Message::assistant(
            "I understand the session memory. I'll reference these facts as needed."));
    }

    newHistory.push_back(Message::user(
        "[Auto-compact: Summary of prior conversation]\n\n" + summary));
    newHistory.push_back(Message::assistant(
        "I understand the conversation summary. I'll continue from here with full context of what we've discussed."));

    for (const auto& msg : recentMsgsSnapshot) {
        newHistory.push_back(msg);
    }

    // Post-compact cleanup on the new history
    compact::PostCompactCleanup::cleanup(newHistory);

    {
        std::lock_guard lock(impl_->historyMutex);
        size_t oldSize = impl_->messageHistory.size();
        impl_->messageHistory = std::move(newHistory);

        // Adjust token tracker
        long estimatedNewTokens = 0;
        for (const auto& msg : impl_->messageHistory) {
            estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
        }
        impl_->tokenTracker.adjustAfterCompaction(estimatedNewTokens);

        spdlog::debug("Auto-compact (fallback) completed: {} messages -> {} messages",
            oldSize, impl_->messageHistory.size());
    }

    return true;
}

bool AgentLoop::attemptReactiveCompact(long tokenGap) {
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->reactiveCompactAttempts >= MAX_REACTIVE_COMPACT_ATTEMPTS) {
            spdlog::warn("Reactive compact: max attempts ({}) reached", MAX_REACTIVE_COMPACT_ATTEMPTS);
            return false;
        }

        impl_->reactiveCompactAttempts++;
        spdlog::debug("Reactive compact: attempt {}/{} (413 prompt-too-long recovery, token gap: {})",
            impl_->reactiveCompactAttempts, MAX_REACTIVE_COMPACT_ATTEMPTS, tokenGap);
    }

    // Force compact regardless of threshold
    if (impl_->autoCompact) {
        std::vector<Message> historySnapshot;
        {
            std::lock_guard lock(impl_->historyMutex);
            historySnapshot = impl_->messageHistory;
        }
        auto newHistory = impl_->autoCompact->compact(historySnapshot);
        if (newHistory) {
            compact::PostCompactCleanup::cleanup(*newHistory);
            std::lock_guard lock(impl_->historyMutex);
            impl_->messageHistory = std::move(*newHistory);

            // Adjust token tracker
            long estimatedNewTokens = 0;
            for (const auto& msg : impl_->messageHistory) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 4;
            }
            impl_->tokenTracker.adjustAfterCompaction(estimatedNewTokens);
            return true;
        }
    }

    // Fallback: aggressive micro-compact
    {
        std::lock_guard lock(impl_->historyMutex);
        int compacted = compact::MicroCompact::applyByPressure(impl_->messageHistory, 0.50);
        if (compacted > 0) {
            spdlog::debug("Reactive compact (micro): cleared {} tool results", compacted);
            return true;
        }
    }

    return false;
}

} // namespace claude
