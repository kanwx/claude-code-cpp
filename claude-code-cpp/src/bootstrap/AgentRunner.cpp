#include <claude/bootstrap/AgentRunner.hpp>
#include <claude/bootstrap/SignalHandler.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/core/TokenTracker.hpp>
#include <claude/api/ApiClient.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <claude/config/AppConfig.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/ToolContext.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/permission/PermissionSettings.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/MessageResponse.hpp>
#include <claude/console/Spinner.hpp>
#include <claude/context/ContextInjector.hpp>
#include <claude/context/GitContext.hpp>
#include <claude/context/ClaudeMdLoader.hpp>
#include <claude/context/SystemPromptBuilder.hpp>
#include <claude/mcp/McpManager.hpp>
#include <claude/mcp/McpClient.hpp>
#include <claude/services/OAuthService.hpp>

#ifdef HAS_FTXUI
#include <claude/ui/FtxuiRepl.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include <claude/ui/renderers/ReadToolRenderer.hpp>
#include <claude/ui/renderers/BashToolRenderer.hpp>
#include <claude/ui/renderers/EditToolRenderer.hpp>
#include <claude/ui/renderers/WriteToolRenderer.hpp>
#include <claude/ui/renderers/GrepToolRenderer.hpp>
#include <claude/ui/renderers/GlobToolRenderer.hpp>
#include <claude/ui/renderers/AgentToolRenderer.hpp>
#include <claude/ui/renderers/WebFetchToolRenderer.hpp>
#include <claude/ui/renderers/WebSearchToolRenderer.hpp>
#include <claude/ui/renderers/LspToolRenderer.hpp>
#include <claude/ui/PermissionRendererRegistry.hpp>
#include <claude/ui/permissions/DefaultPermissionRenderer.hpp>
#include <claude/ui/permissions/BashPermissionRenderer.hpp>
#include <claude/ui/permissions/FileEditPermissionRenderer.hpp>
#include <claude/ui/permissions/FileWritePermissionRenderer.hpp>
#include <claude/ui/permissions/FileReadPermissionRenderer.hpp>
#endif

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace claude {
namespace agent_runner {

ApiClientHolder createApiClient(const ApiClientParams& params) {
    ApiClientHolder holder;
    String provider = params.provider;
    String model = params.model;
    String apiKey = params.apiKey;
    String baseUrl = params.baseUrl;

    // Auto-detect provider type based on base URL
    bool isOpenAICompatible = false;
    if (!baseUrl.empty()) {
        bool isAnthropicAPI = (baseUrl.find("api.anthropic.com") != String::npos);
        isOpenAICompatible = !isAnthropicAPI;

        if (isOpenAICompatible && provider == "anthropic") {
            spdlog::debug("Custom base URL detected ({}), using OpenAI-compatible format", baseUrl);
            provider = "openai";
        }
    }

    if (provider == "anthropic") {
        auto client = std::make_unique<AnthropicClient>(apiKey);
        client->setModel(model);
        client->setMaxTokens(params.maxTokens);
        if (!baseUrl.empty()) {
            client->setBaseUrl(baseUrl);
        }
        holder.raw = client.get();
        holder.owned = std::move(client);
    } else {
        auto client = std::make_unique<OpenAIClient>(apiKey);
        client->setModel(model);
        client->setMaxTokens(params.maxTokens);
        if (!baseUrl.empty()) {
            client->setBaseUrl(baseUrl);
        }
        holder.raw = client.get();
        holder.owned = std::move(client);
    }

    spdlog::debug("Using {} provider with model {}", provider, model);
    return holder;
}

AgentLoopHolder createAgentLoop(const AgentLoopParams& params) {
    AgentLoopHolder holder;

#ifdef HAS_FTXUI
    // Register permission renderers (idempotent — singleton just overwrites)
    {
        auto& permRegistry = ui::PermissionRendererRegistry::instance();
        permRegistry.registerRenderer("Bash", std::make_unique<ui::BashPermissionRenderer>());
        permRegistry.registerRenderer("Edit", std::make_unique<ui::FileEditPermissionRenderer>());
        permRegistry.registerRenderer("Write", std::make_unique<ui::FileWritePermissionRenderer>());
        permRegistry.registerRenderer("Read", std::make_unique<ui::FileReadPermissionRenderer>());
    }
    // Register tool renderers (idempotent — singleton just overwrites)
    {
        auto& registry = ui::ToolRendererRegistry::instance();
        registry.registerRenderer("Read", std::make_unique<ui::ReadToolRenderer>());
        registry.registerRenderer("Bash", std::make_unique<ui::BashToolRenderer>());
        registry.registerRenderer("Edit", std::make_unique<ui::EditToolRenderer>());
        registry.registerRenderer("Write", std::make_unique<ui::WriteToolRenderer>());
        registry.registerRenderer("Grep", std::make_unique<ui::GrepToolRenderer>());
        registry.registerRenderer("Glob", std::make_unique<ui::GlobToolRenderer>());
        registry.registerRenderer("Agent", std::make_unique<ui::AgentToolRenderer>());
        registry.registerRenderer("WebFetch", std::make_unique<ui::WebFetchToolRenderer>());
        registry.registerRenderer("WebSearch", std::make_unique<ui::WebSearchToolRenderer>());
        registry.registerRenderer("LSP", std::make_unique<ui::LspToolRenderer>());
    }
#endif

    // --- Collect environment context ---
    auto workDir = std::filesystem::current_path();
    GitContext gitCtx = GitContext::collect(workDir);

    // Load CLAUDE.md hierarchy (all tiers: managed, user, project, local)
    holder.claudeMdLoader = std::make_unique<ClaudeMdLoader>();
    holder.claudeMdLoader->setCwd(workDir);
    auto claudeMdFiles = holder.claudeMdLoader->loadAll();
    String claudeMdContent = holder.claudeMdLoader->formatAsInstructions(claudeMdFiles);

    // Build system prompt using SystemPromptBuilder with full context
    EnvironmentInfo envInfo;
    envInfo.cwd = workDir.string();
    envInfo.isGit = gitCtx.isGitRepo;
    envInfo.platform =
#ifdef __APPLE__
        "macOS"
#elif defined(__linux__)
        "Linux"
#else
        "Unknown"
#endif
    ;
    envInfo.shell = std::getenv("SHELL") ? std::getenv("SHELL") : "/bin/bash";
#ifdef __APPLE__
    envInfo.osVersion = "macOS Darwin";
#elif defined(__linux__)
    envInfo.osVersion = "Linux";
#endif
    envInfo.modelId = params.apiClient->getModelName();

    // Collect enabled tools info
    std::vector<ToolInfo> enabledTools;
    for (const auto* tool : params.toolRegistry->getTools()) {
        enabledTools.push_back({tool->name(), tool->description()});
    }

    // Build system prompt via builder (includes CLAUDE.md, git context, environment, tools)
    SystemPromptBuilder builder;
    builder.withClaudeMd(claudeMdContent)
           .withGitContext(gitCtx)
           .withWorkDir(workDir.string())
           .withEnvironment(envInfo)
           .withEnabledTools(enabledTools)
           .withReplMode(params.interactive);

    // Apply CLI system prompt overrides via 5-tier resolution
    String systemPrompt;
    std::vector<TextBlockParam> systemBlocks;

    if (!params.systemPromptOverride.empty()) {
        // Tier 0: complete replacement
        systemPrompt = params.systemPromptOverride;
    } else {
        systemBlocks = builder.buildBlocks();
        systemPrompt = builder.build();

        // Append additional system prompt if specified
        if (!params.appendSystemPrompt.empty()) {
            systemPrompt += "\n\n" + params.appendSystemPrompt;
            TextBlockParam appendBlock;
            appendBlock.type = "text";
            appendBlock.text = params.appendSystemPrompt;
            systemBlocks.push_back(appendBlock);
        }
    }

    holder.tokenTracker = std::make_unique<TokenTracker>();

    holder.loop = std::make_unique<AgentLoop>(
        *params.apiClient,
        *params.toolRegistry,
        systemPrompt,
        *holder.tokenTracker
    );

    // Use block-based system prompt for proper prompt caching
    if (!systemBlocks.empty()) {
        holder.loop->setSystemBlocks(std::move(systemBlocks));
    }

    // --- Setup ContextInjector for per-turn context injection ---
    holder.contextInjector = std::make_unique<ContextInjector>();

    // Set git status
    GitStatusAttachment gitStatusAtt;
    gitStatusAtt.branch = gitCtx.branch;
    gitStatusAtt.mainBranch = "main";
    gitStatusAtt.status = gitCtx.status;
    gitStatusAtt.recentCommits = gitCtx.recentCommits;
    holder.contextInjector->setGitStatus(gitStatusAtt);

    // Set CLAUDE.md content
    if (!claudeMdContent.empty()) {
        holder.contextInjector->setClaudeMd(claudeMdContent);
    }

    // Load skills
    auto homeDir = std::getenv("HOME");
    if (homeDir) {
        auto skillsDir = std::filesystem::path(homeDir) / ".claude" / "skills";
        if (std::filesystem::exists(skillsDir)) {
            holder.contextInjector->loadSkills(skillsDir);
        }
    }

    // Load memory files from project .claude/memory/
    auto memDir = workDir / ".claude" / "memory";
    if (std::filesystem::exists(memDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(memDir)) {
            if (entry.path().extension() == ".md") {
                std::ifstream ifs(entry.path());
                if (ifs) {
                    String content((std::istreambuf_iterator<char>(ifs)),
                                   std::istreambuf_iterator<char>());
                    holder.contextInjector->addMemory(entry.path().string(), content);
                }
            }
        }
    }

    // Add system reminders (current session info)
    holder.contextInjector->addSystemReminder(
        "This is a C++ implementation of Claude Code. The agent has access to "
        "file operations, bash commands, and other tools through the ToolRegistry.");

    // Wire ContextInjector into AgentLoop
    holder.loop->setContextInjector(holder.contextInjector.get());

    // Wire up signal handler global for Ctrl+C cancel
    g_agentLoop = holder.loop.get();

    holder.loop->setPermissionEngine(params.permissionEngine);

    // Initialize auto-compact with context window size
    int contextWindow = 200000;
    String modelName = params.apiClient->getModelName();
    if (modelName.find("haiku") != String::npos) {
        contextWindow = 200000;
    } else if (modelName.find("opus") != String::npos) {
        contextWindow = 200000;
    }
    holder.loop->initAutoCompact(contextWindow);

    // Apply CLI flags to AgentLoop
    if (!params.allowedTools.empty()) {
        holder.loop->setAllowedTools(params.allowedTools);
        spdlog::debug("Tool allowlist: {} tools", params.allowedTools.size());
    }
    if (!params.disallowedTools.empty()) {
        holder.loop->setDisallowedTools(params.disallowedTools);
        spdlog::debug("Tool denylist: {} tools", params.disallowedTools.size());
    }
    if (params.maxTurns > 0) {
        holder.loop->setMaxIterations(params.maxTurns);
        spdlog::debug("Max turns: {}", params.maxTurns);
    }

    // Resume session if --continue flag is set
    if (params.continueSession) {
        resumeLastSession(*holder.loop);
    }

    spdlog::debug("AgentLoop initialized with context injection (git={}, CLAUDE.md={} chars, {} memories, {} tools)",
                  gitCtx.isGitRepo ? "yes" : "no",
                  claudeMdContent.size(),
                  std::filesystem::exists(memDir) ? "loaded" : "none",
                  enabledTools.size());

    return holder;
}

void setupCallbacks(AgentLoop& loop,
                     bool useFtxui,
                     Spinner* spinner,
                     FtxuiRepl* ftxuiRepl,
                     std::function<PermissionChoice(const PermissionRequest&)> permissionCallback) {
    // Tool event callback
    loop.setOnToolEvent([useFtxui, spinner, ftxuiRepl](const ToolEvent& event) {
        if (event.phase == ToolEventPhase::Start) {
            if (spinner) spinner->stop();
            if (!useFtxui) {
                // Show tool badge -- prefix with per-tool colored badge
                std::cout << "\n\033[s"
                          << AnsiStyle::DIM << "  ⎿ "
                          << AnsiStyle::RESET << MessageResponse::formatToolBadge(event.toolName)
                          << " " << AnsiStyle::DIM;
                String args = event.arguments;
                if (args.size() > 60) args = args.substr(0, 57) + "...";
                std::cout << args;
                std::cout << "\033[u" << std::flush;
            }
#ifdef HAS_FTXUI
            else if (ftxuiRepl) {
                ftxuiRepl->addToolMessage(event.toolName, event.arguments, "");
            }
#endif
        } else if (event.phase == ToolEventPhase::End) {
            // Tool completion — add result to UI
#ifdef HAS_FTXUI
            if (useFtxui && ftxuiRepl && !event.result.empty()) {
                ftxuiRepl->addToolMessage(event.toolName, "", event.result);
            }
#endif
        }
    });

    // Stream start callback
    loop.setOnStreamStart([spinner]() {
        if (spinner) spinner->stop();
    });

    // Thinking callback -- update thinking summary for FTXUI
    loop.setOnThinking([useFtxui, ftxuiRepl](const String& thinking) {
#ifdef HAS_FTXUI
        if (ftxuiRepl && useFtxui) {
            ftxuiRepl->updateThinkingSummary(thinking);
        }
#endif
    });

    // Content block stop callback
    loop.setOnContentBlockStop([useFtxui, ftxuiRepl](const String& blockType, int index, const String& content) {
#ifdef HAS_FTXUI
        if (useFtxui && ftxuiRepl) {
            if (blockType == "tool_use") {
                spdlog::debug("Content block stop: tool_use at index {}", index);
            } else if (blockType == "thinking") {
                if (!content.empty()) {
                    ftxuiRepl->addThinkingMessage(content);
                }
            }
        }
#endif
    });

    // Tool result streaming callback
    loop.setOnToolResult([useFtxui, ftxuiRepl](const String& toolName, const String& result, bool isError) {
        if (!useFtxui) {
            if (!result.empty()) {
                std::cout << "\n\033[s";  // New line + save cursor
                std::cout << AnsiStyle::DIM << "  ⎿ " << AnsiStyle::RESET;
                if (isError) {
                    std::cout << AnsiStyle::RED;
                }
                String display = result;
                if (display.length() > 500) {
                    display = display.substr(0, 500) + "\n... (truncated)";
                }
                std::cout << display << AnsiStyle::RESET;
                std::cout << "\033[u";  // Restore cursor
                std::cout << std::flush;
            }
        }
#ifdef HAS_FTXUI
        else if (ftxuiRepl) {
            ftxuiRepl->addToolMessage(toolName, "", result);
        }
#endif
    });

    // Unified stream event callback — must forward all event types to their
    // individual handlers since emitStreamEvent short-circuits when this is set.
    loop.setOnStreamEvent([useFtxui, ftxuiRepl](const StreamEvent& event) {
        switch (event.type) {
            case StreamEvent::Type::ToolChunkReady:
                if (!useFtxui) {
                    std::cout << AnsiStyle::DIM << "." << AnsiStyle::RESET << std::flush;
                }
                break;
            case StreamEvent::Type::ToolResultReady:
#ifdef HAS_FTXUI
                if (useFtxui && ftxuiRepl) {
                    ftxuiRepl->addToolMessage(event.toolName, "", event.toolResult);
                }
#endif
                break;
            default:
                break;
        }
    });

    // TAOR loop continue callback
    loop.setOnLoopContinue([useFtxui](int iteration, int maxIterations) {
        if (!useFtxui) {
            std::cout << "\n\033[s"
                      << AnsiStyle::DIM << "  ⟳ Continuing... (turn "
                      << iteration << ")" << AnsiStyle::RESET
                      << "\033[u" << std::flush;
        }
    });

    // Context compression warning callback
    loop.setOnCompactWarning([useFtxui, ftxuiRepl](int level, long currentTokens, long maxTokens) {
        double pct = static_cast<double>(currentTokens) / maxTokens * 100.0;
        String msg = level >= 2
            ? "Context window nearly full (" + std::to_string(static_cast<int>(pct)) + "%). Auto-compacting..."
            : "Context window usage at " + std::to_string(static_cast<int>(pct)) + "%";
#ifdef HAS_FTXUI
        if (useFtxui && ftxuiRepl) {
            ftxuiRepl->addSystemMessage(msg);
        } else
#endif
        {
            std::cout << "\n" << AnsiStyle::YELLOW << "⚠ " << msg
                      << AnsiStyle::RESET << "\n";
        }
    });

    // Permission confirmation callback
    loop.setOnPermissionRequest(std::move(permissionCallback));
}

bool resumeLastSession(AgentLoop& loop) {
    const char* home = std::getenv("HOME");
    if (!home) return false;

    auto sessionDir = std::filesystem::path(home) / ".claude" / "sessions";
    if (!std::filesystem::exists(sessionDir)) return false;

    // Find the most recent session file
    std::vector<std::filesystem::path> sessions;
    for (const auto& entry : std::filesystem::directory_iterator(sessionDir)) {
        if (entry.path().extension() == ".json") {
            sessions.push_back(entry.path());
        }
    }
    if (sessions.empty()) {
        spdlog::debug("No saved sessions found for --continue");
        return false;
    }

    std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) {
        return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
    });

    const auto& latestSession = sessions[0];
    try {
        std::ifstream ifs(latestSession);
        if (!ifs) return false;
        auto sessionJson = Json::parse(ifs);

        if (!sessionJson.contains("messages") || !sessionJson["messages"].is_array()) return false;

        std::vector<Message> loadedMessages;
        for (const auto& jm : sessionJson["messages"]) {
            if (!jm.is_object()) continue;
            String roleStr = jm.value("role", "user");
            MessageRole role = MessageRole::User;
            if (roleStr == "system") role = MessageRole::System;
            else if (roleStr == "assistant") role = MessageRole::Assistant;
            else if (roleStr == "tool") role = MessageRole::ToolResult;

            Message msg;
            msg.role = role;
            msg.content = jm.value("content", "");

            if (jm.contains("tool_calls") && jm["tool_calls"].is_array()) {
                for (const auto& tcj : jm["tool_calls"]) {
                    if (!tcj.is_object()) continue;
                    ToolCall tc;
                    tc.id = tcj.value("id", "");
                    if (tcj.contains("function") && tcj["function"].is_object()) {
                        tc.name = tcj["function"].value("name", "");
                        tc.arguments = tcj["function"].value("arguments", "");
                    }
                    msg.toolCalls.push_back(tc);
                }
            }

            if (jm.contains("tool_results") && jm["tool_results"].is_array()) {
                for (const auto& trj : jm["tool_results"]) {
                    if (!trj.is_object()) continue;
                    ToolResponse tr;
                    tr.callId = trj.value("tool_call_id", "");
                    tr.toolName = trj.value("name", "");
                    tr.content = trj.value("content", "");
                    tr.isError = trj.value("is_error", false);
                    msg.toolResults.push_back(tr);
                }
            }

            if (jm.contains("thinking")) {
                msg.thinking = jm["thinking"].get<String>();
            }

            loadedMessages.push_back(std::move(msg));
        }

        if (!loadedMessages.empty()) {
            // Replace history, preserving the system prompt
            auto& currentHistory = loop.getMessageHistory();
            String currentSystemPrompt;
            for (const auto& m : currentHistory) {
                if (m.role == MessageRole::System) {
                    currentSystemPrompt = m.content;
                    break;
                }
            }

            loadedMessages.erase(
                std::remove_if(loadedMessages.begin(), loadedMessages.end(),
                    [](const Message& m) { return m.role == MessageRole::System; }),
                loadedMessages.end());

            // Prepend system prompt
            loadedMessages.insert(loadedMessages.begin(),
                Message::system(currentSystemPrompt));

            loop.replaceHistory(std::move(loadedMessages));
            spdlog::debug("Resumed session from {}", latestSession.filename().string());
            return true;
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to resume session: {}", e.what());
    }
    return false;
}

std::shared_ptr<McpManager> initMcp(ToolRegistry& tools) {
    auto homeDir = std::getenv("HOME");
    if (!homeDir) return nullptr;

    auto mcpSettingsPath = std::filesystem::path(homeDir) / ".claude" / "mcp_settings.json";
    if (!std::filesystem::exists(mcpSettingsPath)) return nullptr;

    try {
        std::ifstream ifs(mcpSettingsPath);
        if (!ifs) return nullptr;
        auto settings = Json::parse(ifs);

        if (!settings.contains("mcpServers") || !settings["mcpServers"].is_object()) return nullptr;

        auto mcpManager = std::make_shared<McpManager>();
        int started = 0;

        for (auto& [name, serverConfig] : settings["mcpServers"].items()) {
            if (!serverConfig.is_object()) continue;

            try {
                auto client = createMcpClientFromConfig(serverConfig);
                if (client) {
                    mcpManager->addServer(name, std::move(client));
                    started++;
                    spdlog::debug("MCP server '{}' started", name);
                }
            } catch (const std::exception& e) {
                spdlog::warn("MCP server '{}' failed to start: {}", name, e.what());
            }
        }

        if (started > 0) {
            tools.registerMcpTools(mcpManager);
            spdlog::debug("MCP: {} server(s) started, tools registered", started);
            return mcpManager;
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load MCP settings: {}", e.what());
    }
    return nullptr;
}

} // namespace agent_runner
} // namespace claude
