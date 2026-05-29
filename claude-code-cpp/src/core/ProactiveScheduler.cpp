#include <claude/core/ProactiveScheduler.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace claude {

ProactiveScheduler::ProactiveScheduler() = default;

ProactiveScheduler::~ProactiveScheduler() {
    stop();
}

void ProactiveScheduler::start(int initialDelaySeconds) {
    if (running_.load(std::memory_order_acquire)) {
        spdlog::warn("ProactiveScheduler: already running");
        return;
    }

    running_.store(true, std::memory_order_release);
    nextWakeUpSeconds_.store(initialDelaySeconds, std::memory_order_release);
    tickCount_ = 0;

    thread_ = std::thread(&ProactiveScheduler::schedulerLoop, this);
    spdlog::debug("ProactiveScheduler: started (initial delay: {}s, default interval: {}s)",
                 initialDelaySeconds, defaultIntervalSeconds_);
}

void ProactiveScheduler::stop() {
    if (!running_.load(std::memory_order_acquire)) return;

    running_.store(false, std::memory_order_release);
    cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }

    spdlog::debug("ProactiveScheduler: stopped after {} ticks", tickCount_);
}

void ProactiveScheduler::setNextWakeUp(int seconds) {
    if (seconds < 1) seconds = 1;
    if (seconds > 3600) seconds = 3600;
    nextWakeUpSeconds_.store(seconds, std::memory_order_release);
    cv_.notify_all();  // Wake up the scheduler to apply the new delay
}

void ProactiveScheduler::schedulerLoop() {
    while (running_.load(std::memory_order_acquire)) {
        int delaySeconds = nextWakeUpSeconds_.load(std::memory_order_acquire);
        if (delaySeconds <= 0) {
            delaySeconds = defaultIntervalSeconds_;
        }

        // Wait for the delay, but wake early if stop() is called or Sleep overrides
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(delaySeconds),
                         [this] { return !running_.load(std::memory_order_acquire); });
        }

        if (!running_.load(std::memory_order_acquire)) break;

        // Reset nextWakeUp to default for the subsequent tick
        nextWakeUpSeconds_.store(0, std::memory_order_release);

        // Build and send tick message
        tickCount_++;
        String tickMsg = buildTickMessage();

        if (tickCallback_) {
            spdlog::debug("ProactiveScheduler: sending tick #{}", tickCount_);
            tickCallback_(tickMsg);
        } else {
            spdlog::warn("ProactiveScheduler: no tick callback set, tick #{} dropped",
                         tickCount_);
        }
    }
}

String ProactiveScheduler::buildTickMessage() const {
    // Build a <tick> message with current time, matching the proactive prompt format
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    struct tm tm_buf;
    localtime_r(&now_time_t, &tm_buf);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::ostringstream oss;
    oss << "<tick>\n";
    oss << "<time>" << buf << "</time>\n";
    oss << "<count>" << tickCount_ << "</count>\n";
    oss << "</tick>";
    return oss.str();
}

} // namespace claude
