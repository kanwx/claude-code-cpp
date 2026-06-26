#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/core/UnifiedTaskStore.hpp"
#include "claude/ui/FtxuiMarkdown.hpp"
#include "FtxuiColors.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {

using namespace ftxui_colors;

// Brand color static member — references the shared palette
const ftxui::Color FtxuiRepl::BrandOrange = MacPeach;

// ========== Helpers ==========

String FtxuiRepl::formatElapsed(int seconds) {
    if (seconds >= 60) {
        return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    }
    return std::to_string(seconds) + "s";
}

String FtxuiRepl::truncate(const String& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen - 3) + "...";
}

// ========== Refresh thread — spinner animation + safety net flush ==========

void FtxuiRepl::startRefreshThread() {
    if (refreshActive_.exchange(true)) return;
    refreshThread_ = std::thread([this]() { refreshLoop(); });
}

void FtxuiRepl::stopRefreshThread() {
    refreshActive_ = false;
    if (refreshThread_.joinable()) {
        refreshThread_.join();
    }
}

void FtxuiRepl::refreshLoop() {
    int bgCheckCounter = 0;

    while (refreshActive_ && running_) {
        // 50ms = 20fps for spinner animation
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bgCheckCounter++;

        if (!screen_ || !running_) break;

        // Check background task status every ~2 seconds (40 ticks at 50ms)
        if (bgCheckCounter >= 40) {
            bgCheckCounter = 0;
            auto& taskStore = UnifiedTaskStore::instance();
            auto tasks = taskStore.listTasks();
            int runningCount = 0;
            std::vector<DisplayMessage> progressMsgs;

            for (const auto& task : tasks) {
                if (task.status == UnifiedTask::Status::InProgress && task.agentHandle) {
                    runningCount++;
                    progressMsgs.push_back(DisplayMessage::makeAgentProgress(
                        task.agentType.empty() ? String("Agent") : task.agentType,
                        task.subject,
                        0,
                        task.totalTokens,
                        true
                    ));
                }
            }

            if (runningCount > 0 && !isStreaming_) {
                screen_->Post([this, msgs = std::move(progressMsgs)]() {
                    // Remove stale AgentProgress blocks, push fresh ones
                    contentBlocks_.erase(
                        std::remove_if(contentBlocks_.begin(), contentBlocks_.end(),
                            [](const ContentBlock& b) { return b.type == ContentBlock::AgentProgress; }),
                        contentBlocks_.end()
                    );
                    for (auto& m : msgs) {
                        ContentBlock cb;
                        cb.type = ContentBlock::AgentProgress;
                        cb.toolName = m.permissionToolName;
                        cb.text = m.text;
                        contentBlocks_.push_back(std::move(cb));
                    }
                });
            } else if (runningCount == 0) {
                screen_->Post([this]() {
                    contentBlocks_.erase(
                        std::remove_if(contentBlocks_.begin(), contentBlocks_.end(),
                            [](const ContentBlock& b) { return b.type == ContentBlock::AgentProgress; }),
                        contentBlocks_.end()
                    );
                });
            }
        }

        // Request animation frame for spinner tick
        screen_->RequestAnimationFrame();
    }
    refreshActive_ = false;
}

// ========== Streaming — the key to smooth output ==========

void FtxuiRepl::appendStreamText(const String& chunk) {
    // DEPRECATED: New pipeline handles text via handleDisplayEvent(TextPartial).
    // Kept as no-op to prevent crashes from AgentLoop callback dispatch.
}

void FtxuiRepl::finishStream(bool success, const String& error) {
    if (!screen_) return;

    screen_->Post([this, success, err = String(error)]() {
        // Clear streaming text — pipeline commits it via StreamEnd
        streamingText_.clear();
        streamingRenderer_.reset();

        isStreaming_ = false;
        isThinking_ = false;

        // Push stream-end markers as content blocks (old pipeline API)
        if (!success && !err.empty()) {
            ContentBlock cb;
            cb.type = ContentBlock::ErrorMessage;
            cb.text = err;
            contentBlocks_.push_back(std::move(cb));
        }
        // P1: TurnDuration is deferred from AnswerEnd to here.
        // AnswerEnd fires at API stream end (StreamBuffer::StreamEnd),
        // but the turn is not complete until tool execution finishes
        // and runStreaming() returns.  finishStream is called after
        // runStreaming() returns, so the wall-clock duration is correct.
        //
        // Guards: only create TurnDuration if a turn actually started
        // (turnStarted_) and hasn't already been emitted (turnDurationEmitted_).
        // This prevents: (a) TurnDuration for /clear and other non-turn
        // finishStream calls, (b) duplicate TurnDuration from late finalize
        // or repeated finishStream calls.
        if (turnStarted_ && !turnDurationEmitted_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - startTime_).count();
            if (elapsedMs > 0) {
                int seconds = static_cast<int>(elapsedMs / 1000);
                ContentBlock td;
                td.type = ContentBlock::TurnDuration;
                td.stableId = nextStableId_++;

                static const std::vector<String> kTurnVerbs = {
                    "Baked", "Brewed", "Churned", "Cogitated",
                    "Cooked", "Crunched", "Sauteed", "Worked",
                };
                size_t idx = turnVerbIndex_.fetch_add(1, std::memory_order_relaxed);
                String verb = kTurnVerbs[idx % kTurnVerbs.size()];

                td.text = verb + " for " + formatElapsed(seconds);
                contentBlocks_.push_back(std::move(td));
            }
            turnDurationEmitted_ = true;
            turnStarted_ = false;
        }

        stopRefreshThread();
    });
}

// ========== Thread-safe thinking update ==========

void FtxuiRepl::updateThinkingSummary(const String& summary) {
    // DEPRECATED: New pipeline handles thinking via handleDisplayEvent(ThinkingBlock).
}

void FtxuiRepl::addThinkingMessage(const String& chunk) {
    // DEPRECATED: New pipeline handles thinking via handleDisplayEvent(ThinkingBlock).
}

} // namespace claude

#endif // HAS_FTXUI
