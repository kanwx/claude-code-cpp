#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/core/UnifiedTaskStore.hpp"
#include "claude/ui/FtxuiMarkdown.hpp"
#include "claude/ui/ThinkingFilter.hpp"
#include "claude/console/CreativeVerbs.hpp"
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
                for (auto& m : progressMsgs) {
                    m.messageId = MessageIdGenerator::next();
                }
                screen_->Post([this, msgs = std::move(progressMsgs)]() {
                    messages_.erase(
                        std::remove_if(messages_.begin(), messages_.end(),
                            [](const DisplayMessage& m) { return m.type == DisplayMessage::Type::AgentProgress; }),
                        messages_.end()
                    );
                    for (auto& m : msgs) {
                        messages_.push_back(std::move(m));
                    }
                });
            } else if (runningCount == 0) {
                screen_->Post([this]() {
                    messages_.erase(
                        std::remove_if(messages_.begin(), messages_.end(),
                            [](const DisplayMessage& m) { return m.type == DisplayMessage::Type::AgentProgress; }),
                        messages_.end()
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

    int durationMs = 0;
    if (startTime_.time_since_epoch().count() > 0) {
        durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
    }

    screen_->Post([this, success, err = String(error), durationMs]() {
        // Clear streaming text — pipeline commits it via StreamEnd
        streamingText_.clear();
        streamingRenderer_.reset();

        // If AnswerEnd didn't fire (error case), commit via pipeline's StreamEnd
        if (isStreaming_) {
            StreamEvent endEvent;
            endEvent.type = StreamEvent::Type::StreamEnd;
            endEvent.success = success;
            endEvent.text = err;
            if (messagePipeline_.processEvent(endEvent)) {
                messages_ = ThinkingFilter::apply(messagePipeline_.getDisplayMessages());
            }
        }

        if (!success && !err.empty() && !isStreaming_) {
            StreamEvent errEvent;
            errEvent.type = StreamEvent::Type::ErrorMessage;
            errEvent.text = err;
            if (messagePipeline_.processEvent(errEvent)) {
                messages_ = ThinkingFilter::apply(messagePipeline_.getDisplayMessages());
            }
        }

        isStreaming_ = false;
        isThinking_ = false;

        if (success && durationMs > 2000) {
            int seconds = durationMs / 1000;
            String tmsg = console::CreativeVerbs::randomCreativeVerb() + " for " + formatElapsed(seconds);
            StreamEvent durEvent;
            durEvent.type = StreamEvent::Type::TurnDuration;
            durEvent.text = std::move(tmsg);
            if (messagePipeline_.processEvent(durEvent)) {
                messages_ = ThinkingFilter::apply(messagePipeline_.getDisplayMessages());
            }
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
