#pragma once

#include "Types.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace claude {

class AgentLoop;

/// ProactiveScheduler - tick-based wake-up mechanism for autonomous mode.
///
/// When proactive mode is active, the scheduler runs a background thread that
/// periodically sends <tick> messages to the AgentLoop, enabling the agent to
/// wake up, look for useful work, and act autonomously.
///
/// The agent uses the Sleep tool to control pacing. When the agent calls Sleep,
/// the scheduler adjusts its next wake-up time instead of the thread blocking.
class ProactiveScheduler {
public:
    /// Callback type: the scheduler calls this to feed a tick message into the agent
    using TickCallback = std::function<void(const String& tickMessage)>;

    ProactiveScheduler();
    ~ProactiveScheduler();

    /// Set the tick callback (usually feeds into AgentLoop.run())
    void setTickCallback(TickCallback cb) { tickCallback_ = std::move(cb); }

    /// Start the scheduler. The first tick will be sent after initialDelay seconds.
    void start(int initialDelaySeconds = 5);

    /// Stop the scheduler and join the background thread.
    void stop();

    /// Is the scheduler running?
    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /// Set the next wake-up delay (called by SleepTool instead of blocking).
    /// Overrides the default interval. The scheduler will wake up after
    /// the specified number of seconds.
    void setNextWakeUp(int seconds);

    /// Set the default tick interval (seconds between ticks when no Sleep override)
    void setDefaultInterval(int seconds) { defaultIntervalSeconds_ = seconds; }

    /// Get the default interval
    int getDefaultInterval() const { return defaultIntervalSeconds_; }

private:
    void schedulerLoop();
    String buildTickMessage() const;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextWakeUpSeconds_{0};

    std::mutex mutex_;
    std::condition_variable cv_;
    TickCallback tickCallback_;
    int defaultIntervalSeconds_ = 60;  // Default: check every 60s
    int tickCount_ = 0;
};

} // namespace claude
