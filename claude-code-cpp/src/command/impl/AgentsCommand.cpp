#include <claude/command/impl/AgentsCommand.hpp>
#include <claude/command/CommandContext.hpp>
#include <claude/tool/AgentTypes.hpp>
#include <claude/utils/I18n.hpp>
#include <sstream>

namespace claude {

String AgentsCommand::execute(const String& args, CommandContext& context) {
    std::istringstream iss(args);
    String action;
    iss >> action;

    std::ostringstream oss;
    oss << "=== Built-in Agents ===\n\n";

    auto& registry = AgentTypeRegistry::instance();

    if (action.empty() || action == "list") {
        auto types = registry.getAllTypes();

        oss << "Available Agents:\n\n";
        for (const auto& agent : types) {
            oss << "  " << agent.name << "\n";
            oss << "    " << agent.description << "\n";
            oss << "    Temperature: " << agent.temperature
                << " | Max iterations: " << agent.maxIterations
                << " | Read-only: " << (agent.readOnly ? "yes" : "no") << "\n\n";
        }

        oss << "Usage:\n";
        oss << "  /agents list           - List all agents\n";
        oss << "  /agents show <name>    - Show agent details\n";
        oss << "  /agents run <name>     - Run an agent\n";

    } else if (action == "show") {
        String name;
        iss >> name;

        if (name.empty()) {
            oss << tr("error.param.agent_name_required") << "\n";
        } else {
            auto agent = registry.getType(name);
            if (agent) {
                oss << "Agent: " << agent->name << "\n\n";
                oss << "Display name: " << agent->displayName << "\n";
                oss << "Description: " << agent->description << "\n";
                oss << "Max iterations: " << agent->maxIterations << "\n";
                oss << "Max tokens: " << agent->maxTokens << "\n";
                oss << "Temperature: " << agent->temperature << "\n";
                oss << "Read-only: " << (agent->readOnly ? "yes" : "no") << "\n";
                oss << "Concurrency safe: " << (agent->concurrencySafe ? "yes" : "no") << "\n\n";

                oss << "Allowed Tools:\n";
                for (const auto& tool : agent->allowedTools) {
                    oss << "  + " << tool << "\n";
                }
            } else {
                oss << "Agent not found: " << name << "\n";
                oss << "Available: ";
                auto names = registry.getTypeNames();
                for (size_t i = 0; i < names.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << names[i];
                }
                oss << "\n";
            }
        }

    } else if (action == "run") {
        String name;
        iss >> name;

        if (name.empty()) {
            oss << tr("error.param.agent_name_required") << "\n";
        } else {
            auto agent = registry.getType(name);
            if (agent) {
                oss << "Starting agent: " << name << "\n\n";
                oss << "Note: Use the Agent tool to run agents.\n";
                oss << "Example: Agent({\"subagent_type\": \"" << name << "\"})\n";
            } else {
                oss << "Agent not found: " << name << "\n";
            }
        }

    } else {
        oss << "Unknown action: " << action << "\n";
        oss << "Actions: list, show, run\n";
    }

    return oss.str();
}

} // namespace claude
