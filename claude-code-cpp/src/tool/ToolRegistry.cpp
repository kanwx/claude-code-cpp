#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/impl/BashTool.hpp>
#include <claude/tool/impl/FileReadTool.hpp>
#include <claude/tool/impl/FileWriteTool.hpp>
#include <claude/tool/impl/FileEditTool.hpp>
#include <claude/tool/impl/GlobTool.hpp>
#include <claude/tool/impl/GrepTool.hpp>
#include <claude/tool/impl/WebFetchTool.hpp>
#include <claude/tool/impl/WebSearchTool.hpp>
#include <claude/tool/impl/AskUserQuestionTool.hpp>
#include <claude/tool/impl/TaskTools.hpp>
#include <claude/tool/impl/TaskOutputTool.hpp>
#include <claude/tool/impl/LSPTool.hpp>
#include <claude/tool/impl/EnterPlanModeTool.hpp>
#include <claude/tool/impl/ExitPlanModeTool.hpp>
#include <claude/tool/impl/WorktreeTools.hpp>
#include <claude/tool/impl/TodoWriteTool.hpp>
#include <claude/tool/impl/AgentTool.hpp>
#include <claude/tool/impl/NotebookEditTool.hpp>
#include <claude/tool/impl/BriefTool.hpp>
#include <claude/tool/impl/SkillTool.hpp>
#include <claude/tool/impl/SleepTool.hpp>
#include <claude/tool/impl/ScheduleCronTool.hpp>
#include <claude/tool/impl/REPLSessionTool.hpp>
#include <claude/tool/impl/ConfigTool.hpp>
#include <claude/tool/impl/SendMessageTool.hpp>
#include <claude/tool/impl/ToolSearchTool.hpp>
#include <claude/tool/impl/MCPTools.hpp>
#include <claude/tool/impl/MCPTool.hpp>
#include <claude/tool/impl/RagTool.hpp>
#include <claude/tool/impl/OntologyTools.hpp>
#include <claude/tool/CognitiveTools.hpp>

namespace claude {

ToolPtr ToolRegistry::createToolByName(const String& name) {
    if (name == "Read") return std::make_unique<FileReadTool>();
    if (name == "Write") return std::make_unique<FileWriteTool>();
    if (name == "Edit") return std::make_unique<FileEditTool>();
    if (name == "Glob") return std::make_unique<GlobTool>();
    if (name == "Grep") return std::make_unique<GrepTool>();
    if (name == "WebFetch") return std::make_unique<WebFetchTool>();
    if (name == "WebSearch") return std::make_unique<WebSearchTool>();
    if (name == "Bash") return std::make_unique<BashTool>();
    if (name == "LSP") return std::make_unique<LSPTool>();
    if (name == "Config") return std::make_unique<ConfigTool>();
    if (name == "Agent") return std::make_unique<AgentTool>();
    if (name == "AskUserQuestion") return std::make_unique<AskUserQuestionTool>();
    if (name == "TaskCreate") return std::make_unique<TaskCreateTool>();
    if (name == "TaskUpdate") return std::make_unique<TaskUpdateTool>();
    if (name == "TaskList") return std::make_unique<TaskListTool>();
    if (name == "TaskGet") return std::make_unique<TaskGetTool>();
    if (name == "TaskOutput") return std::make_unique<TaskOutputTool>();
    if (name == "TaskStop") return std::make_unique<TaskStopTool>();
    if (name == "EnterPlanMode") return std::make_unique<EnterPlanModeTool>();
    if (name == "ExitPlanMode") return std::make_unique<ExitPlanModeTool>();
    if (name == "TodoWrite") return std::make_unique<TodoWriteTool>();
    if (name == "NotebookEdit") return std::make_unique<NotebookEditTool>();
    return nullptr;
}

void ToolRegistry::registerBuiltinTools() {
    // Core tools
    registerTool(std::make_unique<BashTool>());
    registerTool(std::make_unique<FileReadTool>());
    registerTool(std::make_unique<FileWriteTool>());
    registerTool(std::make_unique<FileEditTool>());
    registerTool(std::make_unique<GlobTool>());
    registerTool(std::make_unique<GrepTool>());
    registerTool(std::make_unique<WebFetchTool>());
    registerTool(std::make_unique<WebSearchTool>());
    registerTool(std::make_unique<AskUserQuestionTool>());
    registerTool(std::make_unique<ToolSearchTool>());

    // Task tools
    registerTool(std::make_unique<TaskCreateTool>());
    registerTool(std::make_unique<TaskUpdateTool>());
    registerTool(std::make_unique<TaskListTool>());
    registerTool(std::make_unique<TaskGetTool>());
    registerTool(std::make_unique<TaskOutputTool>());
    registerTool(std::make_unique<TaskStopTool>());

    // LSP tool
    registerTool(std::make_unique<LSPTool>());

    // Plan mode tools
    registerTool(std::make_unique<EnterPlanModeTool>());
    registerTool(std::make_unique<ExitPlanModeTool>());

    // Worktree tools
    registerTool(std::make_unique<EnterWorktreeTool>());
    registerTool(std::make_unique<ExitWorktreeTool>());

    // Other tools
    registerTool(std::make_unique<TodoWriteTool>());
    registerTool(std::make_unique<AgentTool>());
    registerTool(std::make_unique<NotebookEditTool>());
    registerTool(std::make_unique<BriefTool>());
    registerTool(std::make_unique<SkillTool>());
    registerTool(std::make_unique<SleepTool>());
    registerTool(std::make_unique<ScheduleCronTool>());
    registerTool(std::make_unique<REPLSessionTool>());
    registerTool(std::make_unique<ScheduleWakeupTool>());
    registerTool(std::make_unique<CronCreateTool>());
    registerTool(std::make_unique<CronDeleteTool>());
    registerTool(std::make_unique<CronListTool>());
    registerTool(std::make_unique<ConfigTool>());
    registerTool(std::make_unique<SendMessageTool>());

    // MCP tools
    registerTool(std::make_unique<ListMcpResourcesTool>());
    registerTool(std::make_unique<ReadMcpResourceTool>());
    registerTool(std::make_unique<McpAuthTool>());

    // RAG tool
    registerTool(std::make_unique<RagTool>());

    // Ontology tools
    registerTool(std::make_unique<OntologySearchTool>());
    registerTool(std::make_unique<OntologyClassTool>());
    registerTool(std::make_unique<OntologyRelationTool>());
    registerTool(std::make_unique<OntologyPathTool>());

    spdlog::debug("Registered {} builtin tools", size());
}

void ToolRegistry::registerCognitiveTools(std::shared_ptr<McpClient> mcpClient) {
    if (!mcpClient) {
        spdlog::warn("Cannot register cognitive tools: MCP client is null");
        return;
    }

    // Cognitive tools - connect to cognitive backend via MCP
    registerTool(std::make_unique<CognitiveQueryTool>(mcpClient));
    registerTool(std::make_unique<CognitiveInferTool>(mcpClient));
    registerTool(std::make_unique<CognitiveSuggestTool>(mcpClient));
    registerTool(std::make_unique<CognitiveCreateTool>(mcpClient));
    registerTool(std::make_unique<CognitiveSearchTool>(mcpClient));
    registerTool(std::make_unique<CognitiveConsistencyTool>(mcpClient));
    registerTool(std::make_unique<CognitiveIntentTool>(mcpClient));
    registerTool(std::make_unique<CognitiveExplainTool>(mcpClient));
    registerTool(std::make_unique<CognitiveEmbeddingTool>(mcpClient));
    registerTool(std::make_unique<CognitiveLinkPredictionTool>(mcpClient));
    registerTool(std::make_unique<CognitiveTransitiveTool>(mcpClient));
    registerTool(std::make_unique<CognitivePathTool>(mcpClient));

    spdlog::debug("Registered 12 cognitive tools");
}

void ToolRegistry::registerMcpTools(std::shared_ptr<McpManager> manager) {
    if (!manager) {
        spdlog::warn("Cannot register MCP tools: manager is null");
        return;
    }

    mcpManager_ = manager;

    auto mcpTools = MCPTool::createAllFromManager(manager);
    int count = 0;
    for (auto& tool : mcpTools) {
        String toolName = tool->name();
        registerTool(std::move(tool));
        count++;
    }

    spdlog::debug("Registered {} dynamic MCP tools", count);
}

} // namespace claude
