#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/core/UnifiedTaskStore.hpp"
#include "claude/ui/FtxuiMarkdown.hpp"
#include "claude/console/CreativeVerbs.hpp"
#include "FtxuiColors.hpp"
#include <spdlog/spdlog.h>

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

        // Safety net: flush any stream buffer that appendStreamText didn't flush
        // This catches cases where tokens arrive faster than we can Post
        {
            String batch;
            {
                std::lock_guard lock(streamMutex_);
                if (!streamBuffer_.empty()) {
                    batch = std::move(streamBuffer_);
                    streamBuffer_.clear();
                }
            }
            if (!batch.empty()) {
                screen_->Post([this, b = std::move(batch)]() {
                    if (isThinking_) isThinking_ = false;
                    streamingText_ += b;
                    streamingRenderer_.append(b);
                });
            }
        }

        // Safety net: flush thinking buffer
        {
            String batch;
            {
                std::lock_guard lock(thinkingMutex_);
                if (!thinkingBuffer_.empty()) {
                    batch = std::move(thinkingBuffer_);
                    thinkingBuffer_.clear();
                }
            }
            if (!batch.empty()) {
                screen_->Post([this, b = std::move(batch)]() {
                    if (b.size() > 60) {
                        thinkingSummary_ = "..." + b.substr(b.size() - 57);
                    } else {
                        thinkingSummary_ = std::move(b);
                    }
                    lastOutputTime_ = std::chrono::steady_clock::now();
                });
            }
        }

        // Check background task status every ~2 seconds (40 ticks at 50ms)
        if (bgCheckCounter >= 40) {
            bgCheckCounter = 0;
            auto& taskStore = UnifiedTaskStore::instance();
            auto tasks = taskStore.listTasks();
            int runningCount = 0;
            std::ostringstream bgStatus;
            for (const auto& task : tasks) {
                if (task.status == UnifiedTask::Status::InProgress && task.agentHandle) {
                    runningCount++;
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - task.createdAt).count();
                    bgStatus << "  #" << task.id << " " << task.subject
                             << " (" << elapsed << "s)\n";
                }
            }
            if (runningCount > 0 && !isStreaming_) {
                String statusMsg = "Background tasks running:\n" + bgStatus.str();
                screen_->Post([this, msg = std::move(statusMsg)]() {
                    // Only add if different from last system message to avoid spam
                    if (messages_.empty() ||
                        messages_.back().type != DisplayMessage::Type::SystemInfo ||
                        messages_.back().text.find("Background tasks") == String::npos) {
                        auto dmsg = DisplayMessage::systemInfo(std::move(msg));
                        dmsg.messageId = MessageIdGenerator::next();
                        messages_.push_back(std::move(dmsg));
                    }
                });
            }
        }

        // Request animation frame for spinner tick
        // This is the CORRECT way to trigger redraw in FTXUI:
        // It sets animation_requested_ = true which makes RunOnceBlocking
        // return after a timeout instead of blocking indefinitely
        screen_->RequestAnimationFrame();
    }
    refreshActive_ = false;
}

// ========== Streaming — the key to smooth output ==========

void FtxuiRepl::appendStreamText(const String& chunk) {
    if (!screen_ || chunk.empty()) return;

    // === PRIMARY PATH: Immediate Post to UI thread ===
    // Each token is immediately Posted to the UI thread.
    // screen_->Post() sends a Task to the Receiver, which notify_one() wakes
    // RunOnceBlocking(). FTXUI then processes the task and draws one frame.
    screen_->Post([this, c = String(chunk)]() {
        if (isThinking_) isThinking_ = false;
        streamingText_ += c;
        streamingRenderer_.append(c);
        lastOutputTime_ = std::chrono::steady_clock::now();
    });
}

void FtxuiRepl::finishStream(bool success, const String& error) {
    if (!screen_) return;

    // Calculate duration
    int durationMs = 0;
    if (startTime_.time_since_epoch().count() > 0) {
        durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
    }

    // Final flush of thinking buffer
    {
        String batch;
        {
            std::lock_guard lock(thinkingMutex_);
            batch = std::move(thinkingBuffer_);
            thinkingBuffer_.clear();
        }
        if (!batch.empty()) {
            screen_->Post([this, b = std::move(batch)]() {
                if (b.size() > 60) {
                    thinkingSummary_ = "..." + b.substr(b.size() - 57);
                } else {
                    thinkingSummary_ = std::move(b);
                }
            });
        }
    }

    // State update on UI thread
    screen_->Post([this, success, err = String(error), durationMs]() {
        // If already transitioned to idle (e.g. by cancel handler), skip
        if (!isStreaming_ && !isThinking_) return;

        isStreaming_ = false;
        isThinking_ = false;

        String finalText = std::move(streamingText_);
        streamingText_.clear();
        streamingRenderer_.reset();

        if (success && !finalText.empty()) {
            auto msg = DisplayMessage::assistantText(std::move(finalText));
            msg.messageId = MessageIdGenerator::next();
            messages_.push_back(std::move(msg));
        } else if (!success) {
            if (!finalText.empty()) {
                auto msg = DisplayMessage::assistantText(std::move(finalText));
                msg.messageId = MessageIdGenerator::next();
                messages_.push_back(std::move(msg));
            }
            auto emsg = DisplayMessage::systemError("Error: " + err);
            emsg.messageId = MessageIdGenerator::next();
            messages_.push_back(std::move(emsg));
        }

        // Turn duration message (>2s)
        if (success && durationMs > 2000) {
            int seconds = durationMs / 1000;
            String tmsg = console::CreativeVerbs::randomCreativeVerb() + " for " + formatElapsed(seconds);
            auto dmsg = DisplayMessage::turnDuration(std::move(tmsg));
            dmsg.messageId = MessageIdGenerator::next();
            messages_.push_back(std::move(dmsg));
        }

        // Stop refresh thread after stream ends
        stopRefreshThread();
    });
}

// ========== Thread-safe thinking update ==========

void FtxuiRepl::updateThinkingSummary(const String& summary) {
    if (!screen_) return;
    {
        std::lock_guard lock(thinkingMutex_);
        thinkingBuffer_ += summary;
    }
}

void FtxuiRepl::addThinkingMessage(const String& fullText) {
    if (!screen_) return;
    screen_->Post([this, t = String(fullText)]() {
        thinkingText_ = t;
        auto msg = DisplayMessage::assistantThinking(std::move(t), /*collapsed=*/true);
        msg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(msg));
    });
}

} // namespace claude

#endif // HAS_FTXUI
