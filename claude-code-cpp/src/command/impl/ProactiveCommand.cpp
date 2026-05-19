#include <claude/command/impl/ProactiveCommand.hpp>
#include <claude/command/CommandContext.hpp>
#include <sstream>
#include <cstdlib>

namespace claude {

// Use the same env var as SystemPromptBuilder::isProactiveActive()
static const char* PROACTIVE_ENV_VAR = "CLAUDE_CODE_PROACTIVE_MODE";

String ProactiveCommand::execute(const String& args, CommandContext& context) {
    std::istringstream iss(args);
    String action;
    iss >> action;

    std::ostringstream oss;
    oss << "=== Proactive Mode ===\n\n";

    bool proactive = false;
    const char* env = std::getenv(PROACTIVE_ENV_VAR);
    if (env) {
        String val(env);
        std::transform(val.begin(), val.end(), val.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        proactive = (val == "1" || val == "true" || val == "yes");
    }

    if (action.empty()) {
        if (proactive) {
            unsetenv(PROACTIVE_ENV_VAR);
            oss << "Status: Disabled (was enabled)\n";
        } else {
            setenv(PROACTIVE_ENV_VAR, "1", 1);
            oss << "Status: Enabled (was disabled)\n";
        }
        oss << "\nUsage: /proactive [on|off|status]\n";

    } else if (action == "on" || action == "enable") {
        setenv(PROACTIVE_ENV_VAR, "1", 1);
        oss << "Proactive mode enabled.\n\n";
        oss << "The agent will:\n";
        oss << "  - Wake up periodically and look for useful work\n";
        oss << "  - Take initiative: explore, verify, improve\n";
        oss << "  - Calibrate autonomy to terminal focus\n";
        oss << "  - Use Sleep to control wake-up pacing\n";

    } else if (action == "off" || action == "disable") {
        unsetenv(PROACTIVE_ENV_VAR);
        oss << "Proactive mode disabled.\n";

    } else if (action == "status") {
        oss << "Current: " << (proactive ? "enabled" : "disabled") << "\n";
        oss << "Env var: " << PROACTIVE_ENV_VAR << "\n";

    } else {
        oss << "Unknown action: " << action << "\n";
        oss << "Actions: on, off, status\n";
    }

    return oss.str();
}

} // namespace claude
