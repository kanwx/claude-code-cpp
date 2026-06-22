#include <claude/bootstrap/AgentRunner.hpp>
#include <claude/bootstrap/SignalHandler.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/core/TokenTracker.hpp>
#include <claude/core/ContentBlockParam.hpp>
#include <claude/api/ApiClient.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/stream/StreamBuffer.hpp>
#include <claude/stream/AnswerPostProcessor.hpp>
#include <claude/stream/ContentBlock.hpp>
#include <claude/ui/ContentBlockRenderer.hpp>
#include <claude/metrics/HeadlessContentBlockAccumulator.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <claude/config/AppConfig.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/ToolContext.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/permission/PermissionSettings.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/AnsiSuppress.hpp>
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
#include <mutex>

namespace claude {
namespace agent_runner {

// Protects ANSI-mode streaming output; recursive because StreamBuffer::feed()
// may call emit() which invokes the display callback, re-entering the lock.
static std::recursive_mutex ansiStreamMutex;

ApiClientHolder createApiClient(const ApiClientParams& params) {
    ApiClientHolder holder;
    String provider = params.provider;
    String model = params.model;
    String apiKey = params.apiKey;
    String baseUrl = params.baseUrl;

    // Auto-detect provider type based on base URL.
    // Some third-party providers (e.g. DeepSeek) expose Anthropic-compatible
    // endpoints at paths like /anthropic while using their own domain.
    bool isOpenAICompatible = false;
    if (!baseUrl.empty()) {
        // Anthropic endpoints: official API OR any URL with /anthropic path prefix
        bool isAnthropicAPI = (baseUrl.find("api.anthropic.com") != String::npos) ||
                              (baseUrl.find("/anthropic") != String::npos);
        // OpenAI endpoints: only auto-detect if the URL explicitly looks like OpenAI
        bool isOpenAIAPI = (baseUrl.find("api.openai.com") != String::npos) ||
                           (baseUrl.find("/v1/chat") != String::npos);
        // Only override provider when there's a clear signal.
        // If neither pattern matches, trust the configured provider.
        if (isAnthropicAPI && provider != "anthropic") {
            spdlog::debug("Anthropic-compatible base URL detected ({}), using anthropic format", baseUrl);
            provider = "anthropic";
        } else if (isOpenAIAPI && provider != "openai") {
            spdlog::debug("OpenAI-compatible base URL detected ({}), using openai format", baseUrl);
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
    // Tool renderers are no longer needed — ContentBlock-based rendering
    // handles all tool result display via renderFtxuiElement()
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
                     HeadlessContentBlockAccumulator* headlessAccumulator,
                     std::function<PermissionChoice(const PermissionRequest&)> permissionCallback) {
    // Tool event callback — now handled by new 5-layer pipeline via StreamToolEvent.
    // Kept as minimal callback for spinner stop and debug logging only.
    loop.setOnToolEvent([spinner](const ToolEvent& event) {
        if (event.phase == ToolEventPhase::Start) {
            if (spinner) spinner->stop();
        }
    });

    // Stream start callback
    loop.setOnStreamStart([spinner]() {
        if (spinner) spinner->stop();
    });

    // Thinking callback — now handled by new 5-layer pipeline via TypedStreamEvent.
    // Kept as no-op to prevent crashes from AgentLoop's callback dispatch.
    loop.setOnThinking([](const String&) {});

    // Content block stop callback — now handled by new 5-layer pipeline.
    // Kept as no-op to prevent duplicate thinking/tool rendering.
    loop.setOnContentBlockStop([](const String&, int, const String&) {});

    // Tool result streaming callback — now handled by new 5-layer pipeline.
    // Kept as no-op to prevent crashes from emitStreamEvent() fallback dispatch.
    loop.setOnToolResult([](const String&, const String&, bool) {});

    // Unified stream event callback — only handles non-pipeline events.
    // Tool results are now handled by the new 5-layer pipeline via StreamToolEvent.
    loop.setOnStreamEvent([useFtxui, ftxuiRepl](const StreamEvent& event) {
        switch (event.type) {
            case StreamEvent::Type::ToolResultReady:
                // Handled by new pipeline via onStreamToolEvent → StreamBuffer.
                // Old callback here is kept as a no-op to prevent crashes from
                // emitStreamEvent() fallback dispatch.
                break;
            default:
                break;
        }
    });

    // TAOR loop continue callback
    loop.setOnLoopContinue([useFtxui](int iteration, int /*maxIterations*/) {
        if (!useFtxui) {
            if (supportsAnsiStdout()) {
                std::cout << "\n\033[s"
                          << AnsiStyle::DIM << "  ⟳ Continuing... (turn "
                          << iteration << ")" << AnsiStyle::RESET
                          << "\033[u" << std::flush;
            } else {
                std::cout << "\n  ⟳ Continuing... (turn " << iteration << ")\n" << std::flush;
            }
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
            if (supportsAnsiStdout()) {
                std::cout << "\n" << AnsiStyle::YELLOW << "⚠ " << msg
                          << AnsiStyle::RESET << "\n";
            } else {
                std::cout << "\n⚠ " << msg << "\n";
            }
        }
    });

    // Permission confirmation callback
    loop.setOnPermissionRequest(std::move(permissionCallback));

    // ===== New 5-layer pipeline =====
    // Pipeline: TypedStreamEvent/StreamToolEvent → StreamBuffer → FtxuiRepl/ANSI
    auto streamBuffer = std::make_shared<StreamBuffer>();

    auto postProcessor = std::make_shared<AnswerPostProcessor>();  // B4

    // Track whether TextPartial already emitted text for the current paragraph
    // in ANSI/headless mode.  TextParagraph carries the FULL paragraph text,
    // so if we already printed TextPartial deltas we must not re-emit.
    auto ansiPrintedPartial = std::make_shared<bool>(false);

    streamBuffer->setDisplayCallback(
        [useFtxui, ftxuiRepl, postProcessor, headlessAccumulator, spinner,
         ansiPrintedPartial](DisplayEvent&& event) {
            if (useFtxui && ftxuiRepl) {
                // B4: Route through AnswerPostProcessor for tool grouping/reordering.
                // Reset processor on AnswerStart to clear stale state from
                // previous (possibly cancelled) streaming iterations.
                if (event.type == DisplayEventType::AnswerStart) {
                    postProcessor->reset();
                }
                if (event.type == DisplayEventType::AnswerEnd) {
                    // Phase 1: process the AnswerEnd event itself
                    auto proc = postProcessor->process(std::move(event));
                    ftxuiRepl->handleDisplayEvent(std::move(proc));
                    // Phase 2: finalize — group tools, reorder traces, emit tombstones
                    auto finalEvents = postProcessor->finalize();
                    for (auto& fe : finalEvents) {
                        ftxuiRepl->handleDisplayEvent(std::move(fe));
                    }
                    postProcessor->reset();
                } else {
                    auto proc = postProcessor->process(std::move(event));
                    ftxuiRepl->handleDisplayEvent(std::move(proc));
                }
            } else {
                // ANSI / plain CLI mode: render DisplayEvents directly to stdout.
                //
                // Coordination rules:
                // - Spinner writes to stderr (in-place \r updates), never to stdout.
                // - ThinkingBlock events are suppressed — Spinner handles thinking status.
                // - AnswerStart stops the spinner and clears its line with \n.
                // - TextPartial / TextParagraph writes directly to stdout.
                // - ToolProgress events use Spinner tool-context (no stdout line).
                // - AnswerEnd writes \n to close the turn.
                {
                    std::lock_guard lock(ansiStreamMutex);
                    switch (event.type) {
                        case DisplayEventType::AnswerStart:
                            // Stop spinner and move past its stderr line before
                            // any content is written to stdout.  This prevents
                            // AnswerText from appearing on the same line as the
                            // spinner frame.
                            if (spinner) spinner->stop();
                            *ansiPrintedPartial = false;
                            std::cout << "\n" << std::flush;
                            break;

                        case DisplayEventType::TextPartial:
                            std::cout << event.text << std::flush;
                            *ansiPrintedPartial = true;
                            break;

                        case DisplayEventType::TextParagraph:
                            // TextParagraph carries the full paragraph text.
                            // If TextPartial already emitted the content as
                            // incremental deltas, skip re-emission to avoid
                            // duplication.  Fallback: if no TextPartial fired
                            // (short paragraph), emit the full text here.
                            if (!*ansiPrintedPartial) {
                                std::cout << event.text << std::flush;
                            }
                            std::cout << "\n" << std::flush;
                            *ansiPrintedPartial = false;
                            break;

                        case DisplayEventType::ThinkingBlock:
                            // Suppressed: Spinner on stderr handles thinking status.
                            // Emitting "Thinking..." lines to stdout would interleave
                            // with TextPartial content and cause spam.
                            break;

                        case DisplayEventType::ToolProgress:
                            // Suppressed in ANSI mode: Spinner tool-context handles
                            // in-progress display on stderr with \r in-place updates.
                            break;

                        case DisplayEventType::ToolResult: {
                            ContentBlock cb;
                            cb.type = ContentBlock::ToolResult;
                            cb.toolName = event.toolName;
                            cb.summary = event.summary;
                            if (supportsAnsiStdout()) {
                                std::cout << ContentBlockRenderer::renderAnsi(cb)
                                          << "\n" << std::flush;
                            } else {
                                std::cout << ContentBlockRenderer::renderPlain(cb)
                                          << "\n" << std::flush;
                            }
                            break;
                        }
                        case DisplayEventType::ToolGroup: {
                            ContentBlock cb;
                            cb.type = ContentBlock::ToolGroup;
                            cb.toolName = event.toolName;
                            cb.summary = event.summary;
                            if (supportsAnsiStdout()) {
                                std::cout << ContentBlockRenderer::renderAnsi(cb)
                                          << "\n" << std::flush;
                            } else {
                                std::cout << ContentBlockRenderer::renderPlain(cb)
                                          << "\n" << std::flush;
                            }
                            break;
                        }
                        case DisplayEventType::Error:
                            if (spinner) spinner->stop();
                            if (supportsAnsiStdout()) {
                                std::cout << "\n" << AnsiStyle::RED
                                          << "✕ " << event.text
                                          << AnsiStyle::RESET << "\n" << std::flush;
                            } else {
                                std::cout << "\n✕ " << event.text << "\n" << std::flush;
                            }
                            break;

                        case DisplayEventType::AnswerEnd:
                            if (spinner) spinner->stop();
                            std::cout << std::flush;
                            break;

                        default:
                            break;
                    }
                }
                if (headlessAccumulator) {
                    headlessAccumulator->handleDisplayEvent(std::move(event));
                }
            }
        });

    // Wire TypedStreamEvent → StreamBuffer
    loop.setOnTypedEvent([streamBuffer](TypedStreamEvent&& event) {
        streamBuffer->feed(std::move(event));
    });

    // Wire StreamToolEvent → StreamBuffer
    loop.setOnStreamToolEvent([streamBuffer](StreamToolEvent&& event) {
        std::lock_guard lock(ansiStreamMutex);
        streamBuffer->feed(std::move(event));
    });
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
        for (auto& jm : sessionJson["messages"]) {
            if (!jm.is_object()) continue;

            // Convert old format if needed
            migrateLegacySession(jm);

            String roleStr = jm.value("role", "user");
            MessageRole role = MessageRole::User;
            if (roleStr == "system") role = MessageRole::System;
            else if (roleStr == "assistant") role = MessageRole::Assistant;
            else if (roleStr == "tool") role = MessageRole::ToolResult;

            Message msg;
            msg.role = role;

            if (jm["content"].is_array()) {
                // New format: extract into old Message fields for backward compat
                for (auto& bj : jm["content"]) {
                    String btype = bj.value("type", "");
                    if (btype == "text") {
                        msg.content += bj.value("text", "");
                    } else if (btype == "tool_use") {
                        ToolCall tc;
                        tc.id = bj.value("id", "");
                        tc.name = bj.value("name", "");
                        tc.arguments = bj.value("input", Json::object()).dump();
                        msg.toolCalls.push_back(std::move(tc));
                    } else if (btype == "tool_result") {
                        ToolResponse tr;
                        tr.callId = bj.value("tool_use_id", "");
                        tr.content = bj.value("content", "");
                        tr.isError = bj.value("is_error", false);
                        msg.toolResults.push_back(std::move(tr));
                    } else if (btype == "thinking") {
                        msg.thinking = bj.value("thinking", "");
                        msg.signature = bj.value("signature", "");
                    } else if (btype == "redacted_thinking") {
                        msg.redactedThinking.push_back(bj);
                    }
                }
            } else if (jm["content"].is_string()) {
                // Fallback: should not happen after migration, but safety
                msg.content = jm["content"].get<String>();
            }

            loadedMessages.push_back(std::move(msg));
        }

        if (!loadedMessages.empty()) {
            // Replace history, preserving the system prompt
            auto currentHistory = loop.getMessageHistory();
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
