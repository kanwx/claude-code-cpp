#include <claude/command/impl/ProactiveCommand.hpp>
#include <claude/command/CommandContext.hpp>
#include <claude/config/SettingsManager.hpp>
#include <sstream>
#include <cstdlib>

namespace claude {

String ProactiveCommand::execute(const String& args, CommandContext& context) {
    std::istringstream iss(args);
    String action;
    iss >> action;

    std::ostringstream oss;
    oss << "=== Proactive Mode ===\n\n";

    bool proactive = SettingsManager::instance().getProactiveMode();

    if (action.empty()) {
        if (proactive) {
            SettingsManager::instance().setProactiveMode(false);
            oss << "Status: Disabled (was enabled)\n";
        } else {
            SettingsManager::instance().setProactiveMode(true);
            oss << "Status: Enabled (was disabled)\n";
        }
        oss << "\nUsage: /proactive [on|off|status]\n";

    } else if (action == "on" || action == "enable") {
        SettingsManager::instance().setProactiveMode(true);
        oss << "Proactive mode enabled.\n\n";
        oss << "The agent will:\n";
        oss << "  - Wake up periodically and look for useful work\n";
        oss << "  - Take initiative: explore, verify, improve\n";
        oss << "  - Calibrate autonomy to terminal focus\n";
        oss << "  - Use Sleep to control wake-up pacing\n";

    } else if (action == "off" || action == "disable") {
        SettingsManager::instance().setProactiveMode(false);
        oss << "Proactive mode disabled.\n";

    } else if (action == "status") {
        oss << "Current: " << (proactive ? "enabled" : "disabled") << "\n";

    } else {
        oss << "Unknown action: " << action << "\n";
        oss << "Actions: on, off, status\n";
    }

    return oss.str();
}

} // namespace claude
