#include <claude/core/AgentLoopImpl.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/core/compact/MicroCompact.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/compact/SessionMemoryCompact.hpp>
#include <claude/core/compact/ApiMicroCompact.hpp>
#include <spdlog/spdlog.h>
#include <cstdio>
#include <set>

namespace claude {

namespace {

/// Dump a message summary line for compact diagnostics.
/// Format: idx=<N> role=<role> blocks=<N> toolUseIds=[...] toolResultIds=[...] textLen=<N> preview="..."
static void dumpCompactMessage(const Message& msg, size_t idx, FILE* out) {
    const char* roleStr = "?";
    switch (msg.role) {
        case MessageRole::System:    roleStr = "system"; break;
        case MessageRole::User:      roleStr = "user"; break;
        case MessageRole::Assistant: roleStr = "assistant"; break;
        case MessageRole::ToolResult:roleStr = "tool_result"; break;
    }

    // Collect tool_use / tool_result IDs
    std::vector<String> toolUseIds, toolResultIds;
    for (auto& tc : msg.toolCalls) toolUseIds.push_back(tc.id);
    for (auto& tr : msg.toolResults) toolResultIds.push_back(tr.callId);

    size_t textLen = msg.content.size();
    // Build IDs string
    String tuStr, trStr;
    for (size_t k = 0; k < toolUseIds.size(); ++k) {
        if (k) tuStr += ",";
        tuStr += toolUseIds[k];
    }
    for (size_t k = 0; k < toolResultIds.size(); ++k) {
        if (k) trStr += ",";
        trStr += toolResultIds[k];
    }

    // Truncate preview
    String preview = msg.content.size() > 80
        ? msg.content.substr(0, 80) + "..." : msg.content;
    // Escape newlines
    for (auto& c : preview) if (c == '\n') c = '\\';

    fprintf(out,
        "  idx=%-4zu role=%-12s toolUseIds=[%s] toolResultIds=[%s] textLen=%-6zu preview=\"%s\"\n",
        idx, roleStr, tuStr.c_str(), trStr.c_str(), textLen, preview.c_str());
}

/// Dump compact diagnostics: before/after message structure.
static void dumpCompactDiagnostics(const std::vector<Message>& history,
                                   const char* label,
                                   size_t maxLines = 24) {
    FILE* out = stderr;
    size_t n = history.size();
    fprintf(out, "\n[%s] total_messages=%zu\n", label, n);
    if (n <= maxLines) {
        for (size_t i = 0; i < n; ++i) {
            dumpCompactMessage(history[i], i, out);
        }
    } else {
        // First 5
        fprintf(out, "  --- first 5 ---\n");
        for (size_t i = 0; i < 5 && i < n; ++i) {
            dumpCompactMessage(history[i], i, out);
        }
        // Last (maxLines - 5)
        size_t show = maxLines - 5;
        fprintf(out, "  --- last %zu ---\n", show);
        for (size_t i = n - show; i < n; ++i) {
            dumpCompactMessage(history[i], i, out);
        }
    }
    fprintf(out, "\n");
}

/// Validate compacted history structure.
/// Checks for critical structural issues that would corrupt tool ownership
/// or violate API protocol invariants.  Prints detailed diagnostics on failure.
bool validateCompactedHistory(const std::vector<Message>& history,
                              const char* context) {
    bool ok = true;
    for (size_t i = 0; i < history.size(); ++i) {
        const auto& msg = history[i];

        // Check 1: no empty messages
        if (msg.content.empty() && msg.toolCalls.empty() &&
            msg.toolResults.empty()) {
            fprintf(stderr, "[ERROR] %s: message[%zu] is EMPTY (no content, "
                    "no toolCalls, no toolResults)\n", context, i);
            ok = false;
        }

        // Check 2: no consecutive assistant messages
        if (i > 0 && msg.role == MessageRole::Assistant &&
            history[i - 1].role == MessageRole::Assistant) {
            fprintf(stderr,
                "[ERROR] %s: consecutive assistant messages at [%zu, %zu]\n"
                "  prev[%zu]: toolUseIds=%zu textLen=%zu\n"
                "  cur [%zu]: toolUseIds=%zu textLen=%zu\n",
                context, i - 1, i,
                i - 1, history[i - 1].toolCalls.size(), history[i - 1].content.size(),
                i, msg.toolCalls.size(), msg.content.size());
            ok = false;
        }

        // Check 2b: no consecutive user messages
        if (i > 0 && msg.role == MessageRole::User &&
            history[i - 1].role == MessageRole::User) {
            fprintf(stderr,
                "[ERROR] %s: consecutive user messages at [%zu, %zu]\n"
                "  prev[%zu]: textLen=%zu\n"
                "  cur [%zu]: textLen=%zu\n",
                context, i - 1, i,
                i - 1, history[i - 1].content.size(),
                i, msg.content.size());
            ok = false;
        }
    }

    // Check 3: no orphaned tool_use (assistant with toolCalls must have
    // matching toolResults in subsequent messages before the next assistant)
    for (size_t i = 0; i < history.size(); ++i) {
        if (history[i].role != MessageRole::Assistant) continue;
        if (history[i].toolCalls.empty()) continue;

        // Collect expected call IDs
        std::vector<String> expectedIds;
        for (auto& tc : history[i].toolCalls) {
            expectedIds.push_back(tc.id);
        }

        // Search forward for matching toolResults (before next assistant)
        std::set<String> foundIds;
        size_t nextAsst = history.size();
        for (size_t j = i + 1; j < history.size(); ++j) {
            if (history[j].role == MessageRole::Assistant) {
                nextAsst = j;
                break;
            }
            for (auto& tr : history[j].toolResults) {
                foundIds.insert(tr.callId);
            }
        }

        for (auto& id : expectedIds) {
            if (foundIds.count(id) == 0) {
                fprintf(stderr,
                    "[ERROR] %s: orphan tool_use '%s' at assistant[%zu]\n"
                    "  no matching tool_result found in messages [%zu, %zu)\n"
                    "  next assistant at [%zu]\n",
                    context, id.c_str(), i,
                    i + 1, nextAsst, nextAsst);
                ok = false;
            }
        }
    }

    if (ok) {
        spdlog::debug("{}: VALIDATION_AFTER_COMPACT PASS", context);
    } else {
        spdlog::error("{}: VALIDATION_AFTER_COMPACT FAIL", context);
        dumpCompactDiagnostics(history, context, 24);
    }
    return ok;
}

} // anonymous namespace

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

        // Backoff: if compact failed on this same history size, skip until
        // history changes.  Prevents the fail-loop where compact repeatedly
        // fails and burns API calls without making progress.
        if (impl_->lastFailedCompactSize > 0 &&
            historySnapshot.size() == impl_->lastFailedCompactSize) {
            spdlog::debug("Auto-compact: skipping (backoff — last compact failed "
                          "at same history size={})", historySnapshot.size());
            return false;
        }

        // Diagnostic: dump pre-compact state
        spdlog::debug("COMPACT_BEFORE: {} messages, usage={:.1f}%",
            historySnapshot.size(),
            static_cast<double>(currentTokens) / contextWindow * 100.0);
        dumpCompactDiagnostics(historySnapshot, "COMPACT_BEFORE_LAST12", 12);

        auto newHistory = impl_->autoCompact->compact(historySnapshot);
        if (newHistory) {
            impl_->compactionRecentlyRan = true;

            // Pre-cleanup validation: check if compressAndRebuild output
            // is already structurally invalid before cleanup touches it.
            bool preCleanupOk = validateCompactedHistory(
                *newHistory, "before_cleanup_auto_compact");

            // Post-compact cleanup
            compact::PostCompactCleanup::cleanup(*newHistory);

            // Validate compacted history structure BEFORE replacing live history.
            // If validation fails, discard the compact result and keep the
            // original history intact — a corrupt compact must not poison the
            // conversation.
            if (!validateCompactedHistory(*newHistory, "after_auto_compact")) {
                spdlog::error("Auto-compact: validation failed — discarding "
                              "compacted result, keeping original history "
                              "(pre_cleanup_ok={})", preCleanupOk);
                // Record failure for backoff: don't retry compact on same history size
                impl_->lastFailedCompactSize = historySnapshot.size();
                return false;
            }

            // Compact succeeded — clear backoff
            impl_->lastFailedCompactSize = 0;

            spdlog::debug("COMPACT_AFTER: {} messages -> {} messages, "
                          "VALIDATION_AFTER_COMPACT PASS",
                          historySnapshot.size(), newHistory->size());

            // Session memory is already injected by compressAndRebuild() —
            // no duplicate extraction here.

            std::lock_guard lock(impl_->historyMutex);
            size_t oldSize = impl_->messageHistory.size();
            impl_->messageHistory = std::move(*newHistory);

            long estimatedNewTokens = 0;
            for (const auto& msg : impl_->messageHistory) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 3;
                for (const auto& tc : msg.toolCalls) {
                    estimatedNewTokens += static_cast<long>(tc.arguments.size()) / 3;
                }
                for (const auto& tr : msg.toolResults) {
                    estimatedNewTokens += static_cast<long>(tr.content.size()) / 3;
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
    impl_->compactionRecentlyRan = true;
    bool fallbackPreCleanupOk = validateCompactedHistory(
        newHistory, "before_cleanup_fallback_compact");
    compact::PostCompactCleanup::cleanup(newHistory);

    // Validate compacted history structure BEFORE replacing live history.
    if (!validateCompactedHistory(newHistory, "after_fallback_compact")) {
        spdlog::error("Auto-compact (fallback): validation failed — discarding "
                      "compacted result, keeping original history "
                      "(pre_cleanup_ok={})", fallbackPreCleanupOk);
        // Record failure for backoff
        size_t snapSize = toCompressSnapshot.size() + recentMsgsSnapshot.size() + 1;
        impl_->lastFailedCompactSize = snapSize;
        return false;
    }
    impl_->lastFailedCompactSize = 0;

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
        dumpCompactDiagnostics(historySnapshot, "REACTIVE_COMPACT_BEFORE_LAST12", 12);
        auto newHistory = impl_->autoCompact->compact(historySnapshot);
        if (newHistory) {
            impl_->compactionRecentlyRan = true;
            bool preCleanupOk = validateCompactedHistory(
                *newHistory, "before_cleanup_reactive_compact");
            compact::PostCompactCleanup::cleanup(*newHistory);
            if (!validateCompactedHistory(*newHistory, "after_reactive_compact")) {
                spdlog::error("Reactive compact: validation failed — discarding "
                              "compacted result, keeping original history "
                              "(pre_cleanup_ok={})", preCleanupOk);
                impl_->lastFailedCompactSize = historySnapshot.size();
                return false;
            }
            impl_->lastFailedCompactSize = 0;
            std::lock_guard lock(impl_->historyMutex);
            impl_->messageHistory = std::move(*newHistory);

            // Adjust token tracker
            long estimatedNewTokens = 0;
            for (const auto& msg : impl_->messageHistory) {
                estimatedNewTokens += static_cast<long>(msg.content.size()) / 3;
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
