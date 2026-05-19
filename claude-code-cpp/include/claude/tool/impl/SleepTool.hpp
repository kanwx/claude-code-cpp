#pragma once

#include "../Tool.hpp"
#include <chrono>
#include <thread>

namespace claude {

/// SleepTool - Wait for a specified duration.
///
/// In proactive mode, uses a scheduler callback to set the next wake-up
/// instead of blocking the thread. In normal mode, sleeps directly.
class SleepTool : public Tool {
public:
    String name() const override { return "Sleep"; }

    String description() const override {
        return "Wait for a specified duration. In proactive mode, schedules "
               "the next wake-up instead of blocking.";
    }

    String inputSchema() const override {
        return R"({"type":"object","properties":{"seconds":{"type":"number","description":"Seconds to sleep","minimum":1,"maximum":600}},"required":["seconds"]})";
    }

    String execute(const Json& input, ToolContext& context) override {
        double seconds = input.value("seconds", 1.0);

        if (seconds < 1) seconds = 1;
        if (seconds > 600) seconds = 600;

        // In proactive mode, use the scheduler callback instead of blocking
        auto schedulerCb = context.get<std::function<void(int)>>("proactiveScheduleWakeUp");
        if (schedulerCb && *schedulerCb) {
            (*schedulerCb)(static_cast<int>(seconds));
            std::ostringstream oss;
            oss << "Scheduled next wake-up in " << seconds << " seconds.";
            return oss.str();
        }

        // Normal mode: block the thread
        auto ms = static_cast<int>(seconds * 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));

        std::ostringstream oss;
        oss << "Slept for " << seconds << " seconds.";
        return oss.str();
    }
};

} // namespace claude
