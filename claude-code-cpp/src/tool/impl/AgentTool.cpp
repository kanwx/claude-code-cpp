#include <claude/tool/impl/AgentTool.hpp>
#include <claude/tool/AgentTypes.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/core/TokenTracker.hpp>
#include <claude/core/BackgroundAgentHandle.hpp>
#include <claude/core/UnifiedTaskStore.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/api/ApiClient.hpp>
#include <sstream>
#include <memory>

namespace claude {

String AgentTool::description() const {
    return "Launch a new agent to handle complex, multi-step tasks. Each agent type has "
           "specific capabilities and tools available to it.\n\n"
           "Available agent types: \n"
           "- Explore (fast codebase search, read-only, temp=0.3)\n"
           "- Plan (architecture design, read-only, temp=0.5)\n"
           "- general-purpose (full-featured agent with write access, temp=1.0)\n"
           "- verification (code review and bug detection, read-only, temp=0.2)\n"
           "- claudeCodeGuide (help with Claude Code features, read-only, temp=0.4)\n"
           "- statuslineSetup (configure status line settings, temp=0.5)\n"
           "- code-review (review code quality, read-only, temp=0.3)\n"
           "- security-audit (OWASP security scan, read-only, temp=0.2)\n"
           "- test-generator (generate tests, temp=0.5)\n\n"
           "When calling Agent, specify a subagent_type that matches the task. "
           "Agents are valuable for parallelizing independent queries or for protecting "
           "the main context window from excessive results, but should not be used "
           "excessively when not needed. Use run_in_background=true to launch parallel agents.";
}

String AgentTool::inputSchema() const {
    auto names = AgentTypeRegistry::instance().getTypeNames();
    String enumStr = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) enumStr += ",";
        enumStr += "\"" + names[i] + "\"";
    }
    enumStr += "]";

    Json schema = Json::parse(R"~(
        {
            "type": "object",
            "properties": {
                "prompt": {"type": "string", "description": "The task or question for the agent"},
                "subagent_type": {
                    "type": "string",
                    "description": "Type of sub-agent to launch (default: general-purpose)"
                },
                "description": {"type": "string", "description": "Short description of the task (3-5 words)"},
                "run_in_background": {"type": "boolean", "description": "Set to true to run agent in background (returns task ID immediately)"},
                "model": {"type": "string", "enum": ["sonnet", "opus", "haiku"], "description": "Optional model override for this agent"},
                "isolation": {"type": "string", "enum": ["worktree"], "description": "Isolation mode. 'worktree' creates a temporary git worktree"}
            },
            "required": ["prompt"]
        }
    )~");
    schema["properties"]["subagent_type"]["enum"] = names;
    return schema.dump();
}

String AgentTool::execute(const Json& input, ToolContext& context) {
    String prompt = input["prompt"];
    String subagentType = input.value("subagent_type", "general-purpose");
    String description = input.value("description", "");
    bool runInBackground = input.value("run_in_background", false);

    // Get agent type definition from registry
    auto typeDef = AgentTypeRegistry::instance().getType(subagentType);
    if (!typeDef) {
        return "Error: Unknown agent type '" + subagentType + "'. "
               "Available types: Explore, Plan, general-purpose, verification, claudeCodeGuide, statuslineSetup";
    }

    // Check for API client
    auto apiClient = context.get<ApiClient*>("apiClient");
    if (!apiClient) {
        // Simulation mode (no API key)
        std::ostringstream oss;
        oss << "=== " << typeDef->displayName << " (simulation mode) ===\n\n";
        oss << "Type: " << subagentType << "\n";
        oss << "Prompt: " << prompt << "\n";
        oss << "Tools: ";
        for (size_t i = 0; i < typeDef->allowedTools.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << typeDef->allowedTools[i];
        }
        oss << "\n\nConfigure an API client to enable full execution.\n";
        return oss.str();
    }

    // ===== Background execution =====
    if (runInBackground) {
        auto parentCb = context.get<std::function<PermissionChoice(const PermissionRequest&)>>(
            "parentPermissionCallback");

        ParentContextSnapshot snapshot;
        snapshot.workDir = context.workDir;
        snapshot.homeDir = context.homeDir;
        snapshot.debug = context.debug;
        snapshot.verbose = context.verbose;
        snapshot.nonInteractive = context.nonInteractive;
        snapshot.agentDepth = context.getOr<int>("agentDepth", 0);
        snapshot.permissionEngine = context.get<RuleEngine*>("permissionEngine").value_or(nullptr);
        snapshot.permissionCallback = parentCb.value_or(std::function<PermissionChoice(const PermissionRequest&)>());

        auto handle = BackgroundAgentHandle::launch(
            prompt, subagentType, *typeDef,
            **apiClient, snapshot
        );

        if (handle->status() == AgentRunStatus::Failed) {
            return "Error: " + handle->getResult();
        }

        handle->setDescription(description.empty() ? prompt.substr(0, 50) : description);

        // Create a task in UnifiedTaskStore to track this agent
        auto& taskStore = UnifiedTaskStore::instance();
        String taskId = taskStore.createTask(
            description.empty() ? prompt.substr(0, 50) : description,
            prompt
        );
        auto task = taskStore.getTask(taskId);
        if (task) {
            task->agentType = subagentType;
            task->prompt = prompt;
            taskStore.updateTask(taskId, *task);
        }
        taskStore.setTaskAgentHandle(taskId, handle);
        taskStore.claimTask(taskId, "background-agent");

        return "Launched background agent (task #" + taskId + ", type: " + subagentType + ")\n"
               "Use TaskOutput with task_id \"" + taskId + "\" to get results, or TaskList to see status.";
    }

    // ===== Synchronous execution =====
    std::ostringstream oss;
    oss << "=== " << typeDef->displayName << " ===\n\n";

    // Create isolated tool registry with allowed tools
    auto isolatedRegistry = std::make_unique<ToolRegistry>();
    for (const auto& toolName : typeDef->allowedTools) {
        auto tool = ToolRegistry::createToolByName(toolName);
        if (tool) {
            isolatedRegistry->registerTool(std::move(tool));
        } else {
            spdlog::warn("AgentTool: unknown tool '{}' for agent type '{}'", toolName, subagentType);
        }
    }

    // Create isolated tracker and agent
    auto isolatedTracker = std::make_unique<TokenTracker>();
    auto isolatedAgent = std::make_unique<AgentLoop>(
        **apiClient,
        *isolatedRegistry,
        typeDef->systemPrompt,
        *isolatedTracker
    );

    // Apply agent-type-specific overrides
    isolatedAgent->setMaxIterations(typeDef->maxIterations);
    isolatedAgent->setTemperature(typeDef->temperature);
    isolatedAgent->setMaxTokensOverride(typeDef->maxTokens);

    // Enforce agent nesting depth limit
    int currentDepth = context.getOr<int>("agentDepth", 0);
    if (currentDepth >= 3) {
        return "Error: Maximum agent nesting depth (3) reached. "
               "Cannot spawn another sub-agent at this depth.";
    }
    isolatedAgent->getToolContext().set("agentDepth", currentDepth + 1);

    // Set permission engine
    auto permEngine = context.get<RuleEngine*>("permissionEngine");
    if (permEngine) {
        isolatedAgent->setPermissionEngine(*permEngine);
    }

    // Permission callback: delegate to parent or use rule engine
    auto parentCallback = context.get<std::function<PermissionChoice(const PermissionRequest&)>>(
        "parentPermissionCallback");

    if (parentCallback && *parentCallback) {
        isolatedAgent->setOnPermissionRequest(*parentCallback);
    } else {
        auto engine = permEngine;
        isolatedAgent->setOnPermissionRequest([engine](const PermissionRequest& req) {
            if (req.toolName == "Read" || req.toolName == "Glob" ||
                req.toolName == "Grep" || req.toolName == "WebFetch" ||
                req.toolName == "WebSearch" || req.toolName == "LSP") {
                return PermissionChoice::AllowOnce;
            }
            if (engine) {
                return PermissionChoice::AllowOnce;
            }
            return PermissionChoice::DenyOnce;
        });
    }

    // Execute task
    auto result = isolatedAgent->run(prompt);

    if (result) {
        oss << *result;
    } else {
        oss << "Error: " << result.error();
    }

    // Stats
    oss << "\n\n---\n[" << typeDef->displayName << " completed — "
        << isolatedTracker->getTotalTokens() << " tokens]";

    return oss.str();
}

} // namespace claude
