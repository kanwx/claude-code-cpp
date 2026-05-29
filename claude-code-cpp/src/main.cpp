#include <iostream>
#include <string>
#include <memory>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <clocale>
#include <chrono>

#include <claude/core/AgentLoop.hpp>
#include <claude/core/TokenTracker.hpp>
#include <claude/core/CleanupRegistry.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/ToolContext.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/permission/PermissionSettings.hpp>
#include <claude/permission/PermissionStore.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <claude/config/AppConfig.hpp>
#include <claude/console/Spinner.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/PermissionPromptRenderer.hpp>
#include <claude/console/StatusLine.hpp>
#include <claude/console/PromptRenderer.hpp>
#include <claude/console/TurnDurationRenderer.hpp>
#include <claude/console/Completer.hpp>
#include <claude/command/CommandRegistry.hpp>

// 行编辑支持: 优先使用 GNU readline
#if __has_include(<readline/readline.h>)
    #include <readline/readline.h>
    #include <readline/history.h>
    #define USE_READLINE 1
#endif
#include <claude/command/CommandContext.hpp>
#include <claude/utils/Http.hpp>
#include <claude/console/MessageResponse.hpp>

// 命令头文件
#include <claude/command/impl/HelpCommand.hpp>
#include <claude/command/impl/CommitCommand.hpp>
#include <claude/command/impl/InitCommand.hpp>
#include <claude/command/impl/DoctorCommand.hpp>
#include <claude/command/impl/ResumeCommand.hpp>
#include <claude/command/impl/McpCommand.hpp>
#include <claude/command/impl/StatusCommand.hpp>
#include <claude/command/impl/HistoryCommand.hpp>
#include <claude/command/impl/ModelCommand.hpp>
#include <claude/command/impl/PermissionsCommand.hpp>
#include <claude/command/impl/SkillsCommand.hpp>
#include <claude/command/impl/HooksCommand.hpp>
#include <claude/command/impl/VimCommand.hpp>
#include <claude/command/impl/EffortCommand.hpp>
#include <claude/command/impl/ThemeCommand.hpp>
#include <claude/command/impl/KeybindingsCommand.hpp>
#include <claude/command/impl/StatsCommand.hpp>
#include <claude/command/impl/UpgradeCommand.hpp>
#include <claude/command/impl/VersionCommand.hpp>
#include <claude/command/impl/BugCommand.hpp>
#include <claude/command/impl/FeedbackCommand.hpp>
#include <claude/command/impl/ExportCommand.hpp>
#include <claude/command/impl/SessionCommand.hpp>
#include <claude/command/impl/OutputStyleCommand.hpp>
#include <claude/command/impl/LoginCommand.hpp>
#include <claude/command/impl/PrivacyCommand.hpp>
#include <claude/command/impl/BranchCommand.hpp>
#include <claude/command/impl/RewindCommand.hpp>
#include <claude/command/impl/TagCommand.hpp>
#include <claude/command/impl/PlanCommand.hpp>
#include <claude/command/impl/LogoutCommand.hpp>
#include <claude/command/impl/EnvCommand.hpp>
#include <claude/command/impl/FastCommand.hpp>
#include <claude/command/impl/DiffCommand.hpp>
#include <claude/command/impl/UsageCommand.hpp>
#include <claude/command/impl/SummaryCommand.hpp>
#include <claude/command/impl/IssueCommand.hpp>
#include <claude/command/impl/PrCommentsCommand.hpp>
#include <claude/command/impl/CompactCommand.hpp>
#include <claude/command/impl/ConfigCommand.hpp>
#include <claude/command/impl/MemoryCommand.hpp>
#include <claude/command/impl/ReviewCommand.hpp>
#include <claude/command/impl/SecurityReviewCommand.hpp>
#include <claude/command/impl/AutofixPrCommand.hpp>
#include <claude/command/impl/ThinkbackCommand.hpp>
#include <claude/command/impl/PluginCommand.hpp>
#include <claude/command/impl/IdeCommand.hpp>
#include <claude/command/impl/VoiceCommand.hpp>
#include <claude/command/impl/BridgeCommand.hpp>
#include <claude/command/impl/ProactiveCommand.hpp>
#include <claude/command/impl/ChromeCommand.hpp>
#include <claude/command/impl/MobileCommand.hpp>
#include <claude/command/impl/WorkflowsCommand.hpp>
#include <claude/command/impl/DebugCommand.hpp>
#include <claude/command/impl/DocCommand.hpp>
#include <claude/command/impl/CommitPushPrCommand.hpp>
#include <claude/command/impl/ContextCommand.hpp>
#include <claude/command/impl/FilesCommand.hpp>
#include <claude/command/impl/TasksCommand.hpp>
#include <claude/command/impl/AgentsCommand.hpp>
#include <claude/command/impl/RenameCommand.hpp>
#include <claude/command/impl/SwarmCommand.hpp>
#include <claude/command/impl/CopyCommand.hpp>
#include <claude/command/impl/AddDirCommand.hpp>
#include <claude/command/impl/TeleportCommand.hpp>
#include <claude/command/impl/AdvisorCommand.hpp>
#include <claude/command/impl/BughunterCommand.hpp>
#include <claude/command/impl/ReleaseNotesCommand.hpp>
#include <claude/command/impl/DesktopCommand.hpp>
#include <claude/command/impl/Upgrade2Command.hpp>
#include <claude/command/impl/LangCommand.hpp>
#include <claude/command/impl/AdditionalCommands.hpp>
#include <claude/command/impl/MoreCommands.hpp>
#include <claude/command/impl/FinalCommands.hpp>
#include <claude/command/impl/FinalTwoCommands.hpp>
#include <claude/utils/I18n.hpp>
#include <claude/mcp/McpManager.hpp>
#include <claude/mcp/McpClient.hpp>
#include <claude/context/ContextInjector.hpp>
#include <claude/context/GitContext.hpp>
#include <claude/context/ClaudeMdLoader.hpp>
#include <claude/context/SystemPromptBuilder.hpp>
#include <claude/constants/Prompts.hpp>
#include <claude/services/OAuthService.hpp>

// Extracted bootstrap modules
#include <claude/bootstrap/SignalHandler.hpp>
#include <claude/bootstrap/AgentRunner.hpp>

// AppState needed for modelStrings() in both FTXUI and readline paths
#include <claude/bootstrap/AppState.hpp>

// FTXUI support (optional)
#ifdef HAS_FTXUI
#include <claude/ui/FtxuiRepl.hpp>
#endif

// Session persistence (always available)
namespace claude { namespace session { void saveSession(AgentLoop* loop); } }

// Readline support functions (only when readline is available)
#ifdef USE_READLINE
namespace claude { namespace readline_support {
    void initReadline(CommandRegistry* registry, Completer* completer);
}}
extern claude::CommandRegistry* g_commandRegistry;
extern claude::Completer* g_completer;
void saveHistory(const claude::String& entry);
#endif

using namespace claude;

// Bring new formatters into scope
using claude::MessageResponse;

/// 主应用类
class ClaudeCodeApp {
public:
    int run(int argc, char* argv[]) {
        // 解析命令行
        if (!parseArgs(argc, argv)) {
            return 0;
        }

        // 初始化
        init();

        // 运行 REPL 或单次查询
        if (interactive_) {
#ifdef HAS_FTXUI
            if (useFtxui_) {
                runFtxuiRepl();
            } else
#endif
            {
                runRepl();
            }
        } else if (!prompt_.empty()) {
            runOnce(prompt_);
        }

        return 0;
    }

private:
    bool parseArgs(int argc, char* argv[]) {
        CLI::App app{"Claude Code C++ - AI coding assistant", "claude"};

        app.add_option("-p,--prompt", prompt_, "Run with a single prompt");
        app.add_flag("-i,--interactive", interactive_, "Start interactive mode");
        app.add_flag("-v,--verbose", verbose_, "Verbose output");
        app.add_option("--model", model_, "Model to use");
        app.add_option("--provider", provider_, "API provider (openai/anthropic)");
        app.add_flag("--dangerously-skip-permissions", dangerouslySkipPermissions_,
                     "Skip all permission checks (dangerous)");
        app.add_flag("--auto-mode", autoMode_,
                     "Enable auto mode (AI classifier decides permissions)");
        app.add_option("--permission-mode", permissionModeStr_,
                       "Permission mode: default|acceptEdits|bypassPermissions|dontAsk|plan|auto");
        app.add_option("--allowedTools", allowedToolsStr_,
                       "Comma-separated list of allowed tools (restricts available tools)")
            ->expected(1)->delimiter(',');
        app.add_option("--disallowedTools", disallowedToolsStr_,
                       "Comma-separated list of disallowed tools")
            ->expected(1)->delimiter(',');
        app.add_option("--max-turns", maxTurns_,
                       "Maximum agent loop iterations (prevents runaway costs)");
        app.add_flag("--continue", continueSession_,
                     "Resume the most recent conversation session");
        app.add_option("--system-prompt", systemPromptOverride_,
                       "Override the default system prompt (replaces everything)");
        app.add_option("--append-system-prompt", appendSystemPrompt_,
                       "Append to the default system prompt");
#ifdef HAS_FTXUI
        bool noFtxui = false;
        app.add_flag("--no-ftxui", noFtxui, "Disable FTXUI, use readline mode instead");
#endif

        try {
            app.parse(argc, argv);
#ifdef HAS_FTXUI
            if (noFtxui) useFtxui_ = false;
            // Auto-fallback to readline when stdout is not a TTY
            if (useFtxui_ && !isatty(STDOUT_FILENO)) {
                useFtxui_ = false;
            }
#endif
        } catch (const CLI::ParseError& e) {
            return app.exit(e);
        }

        // 如果没有指定，默认交互模式
        if (!interactive_ && prompt_.empty()) {
            interactive_ = true;
        }

        return true;
    }

    void init() {
        // Register terminal restore as FIRST cleanup (runs LAST due to reverse-order execution)
        CleanupRegistry::registerCleanup("restore-terminal", CleanupRegistry::Category::UI,
            []() { restoreTerminal(); });

        // 注册清理函数
        CleanupRegistry::registerCleanup("save-permissions", CleanupRegistry::Category::Persistence,
            [this]() {
                if (permissionSettings_) {
                    auto configPath = config_ ? config_->getLocalConfigPath() : std::filesystem::path();
                    if (!configPath.empty()) {
                        permissionSettings_->saveToFile(configPath.parent_path() / "permissions.json");
                    }
                }
            });

        CleanupRegistry::registerCleanup("cleanup-temp-files", CleanupRegistry::Category::Filesystem,
            []() {
                // Clean up tool result temp files from /tmp/claude-result-*
                try {
                    for (const auto& entry : std::filesystem::directory_iterator("/tmp")) {
                        if (entry.path().filename().string().starts_with("claude-result-")) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                } catch (...) {}
            });

        // 加载配置
        config_ = std::make_unique<AppConfig>();
        config_->load();

        // 设置日志级别 — dual-sink: stderr (errors only) + file (debug level)
        auto logDir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.claude/logs";
        std::filesystem::create_directories(logDir);

        auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        stderrSink->set_level(verbose_ ? spdlog::level::debug : spdlog::level::err);

        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logDir + "/claude-cli.log", true);
        fileSink->set_level(spdlog::level::debug);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        auto logger = std::make_shared<spdlog::logger>("default", spdlog::sinks_init_list{stderrSink, fileSink});
        logger->set_level(spdlog::level::debug);
        spdlog::set_default_logger(logger);

        // 初始化 HTTP 代理 (从环境变量)
        auto proxyConfig = Http::loadProxyFromEnv();
        if (proxyConfig.enabled()) {
            Http::setProxy(proxyConfig);
            spdlog::debug("HTTP proxy configured: {}:{}", proxyConfig.host, proxyConfig.port);

            auto caCert = Http::getCaCertPath();
            if (caCert) {
                spdlog::debug("CA certificate: {}", *caCert);
            }
        }

        // 初始化权限
        permissionSettings_ = std::make_unique<PermissionSettings>();
        permissionEngine_ = std::make_unique<RuleEngine>(*permissionSettings_);

        // Load persisted permission decisions from ~/.claude/permissions.json
        PermissionStore::instance().load();
        PermissionStore::instance().loadDefaultRules();

        // 应用权限模式
        if (dangerouslySkipPermissions_) {
            permissionSettings_->setMode(PermissionMode::Bypass);
            spdlog::warn("Permission checks bypassed - this is dangerous!");
        } else if (autoMode_ || permissionModeStr_ == "auto") {
            permissionSettings_->setMode(PermissionMode::Auto);
            permissionEngine_->yoloClassifier().setEnabled(true);
            spdlog::debug("Auto mode enabled (AI classifier will decide permissions)");
        } else if (!permissionModeStr_.empty()) {
            auto mode = parsePermissionMode(permissionModeStr_);
            if (mode) {
                permissionSettings_->setMode(*mode);
                if (*mode == PermissionMode::Auto) {
                    permissionEngine_->yoloClassifier().setEnabled(true);
                    spdlog::debug("Auto mode enabled (AI classifier will decide permissions)");
                }
            } else {
                spdlog::warn("Unknown permission mode: {}", permissionModeStr_);
            }
        }

        // 从配置文件读取 autoMode 规则
        if (permissionSettings_->getCurrentMode() == PermissionMode::Auto) {
            auto permConfig = config_->getPermissionConfig();
            AutoModeRules rules;
            rules.allow = permConfig.allow;
            rules.softDeny = permConfig.deny;
            permissionEngine_->yoloClassifier().setAutoModeRules(rules);
        }

        // 初始化工具
        toolRegistry_ = std::make_unique<ToolRegistry>();
        toolRegistry_->registerBuiltinTools();

        // 初始化命令
        initCommands();

        // 初始化 API 客户端 (via agent_runner)
        initApiClient();

        // 初始化 AgentLoop (via agent_runner)
        initAgentLoop();

        // Auto-start MCP servers from config (via agent_runner)
        mcpManager_ = agent_runner::initMcp(*toolRegistry_);

        spdlog::debug("Claude Code C++ initialized");
    }

    void initCommands() {
        commandRegistry_ = std::make_unique<CommandRegistry>();

        // 注册所有命令 (来自 HelpCommand.hpp: Help, Clear, Cost, Exit)
        commandRegistry_->registerCommand(std::make_unique<HelpCommand>());
        commandRegistry_->registerCommand(std::make_unique<ClearCommand>());
        commandRegistry_->registerCommand(std::make_unique<CostCommand>());
        commandRegistry_->registerCommand(std::make_unique<ExitCommand>());

        // 来自 CommitCommand.hpp: Commit, Review, Compact, Config, Memory
        commandRegistry_->registerCommand(std::make_unique<CommitCommand>());
        commandRegistry_->registerCommand(std::make_unique<ReviewCommand>());
        commandRegistry_->registerCommand(std::make_unique<CompactCommand>());
        commandRegistry_->registerCommand(std::make_unique<ConfigCommand>());
        commandRegistry_->registerCommand(std::make_unique<MemoryCommand>());

        // 其他命令
        commandRegistry_->registerCommand(std::make_unique<InitCommand>());
        commandRegistry_->registerCommand(std::make_unique<DoctorCommand>());
        commandRegistry_->registerCommand(std::make_unique<ResumeCommand>());
        commandRegistry_->registerCommand(std::make_unique<McpCommand>());
        commandRegistry_->registerCommand(std::make_unique<StatusCommand>());
        commandRegistry_->registerCommand(std::make_unique<HistoryCommand>());
        commandRegistry_->registerCommand(std::make_unique<ModelCommand>());
        commandRegistry_->registerCommand(std::make_unique<PermissionsCommand>());
        commandRegistry_->registerCommand(std::make_unique<SkillsCommand>());
        commandRegistry_->registerCommand(std::make_unique<HooksCommand>());
        commandRegistry_->registerCommand(std::make_unique<VimCommand>());
        commandRegistry_->registerCommand(std::make_unique<EffortCommand>());
        commandRegistry_->registerCommand(std::make_unique<ThemeCommand>());
        commandRegistry_->registerCommand(std::make_unique<KeybindingsCommand>());
        commandRegistry_->registerCommand(std::make_unique<LangCommand>());
        commandRegistry_->registerCommand(std::make_unique<StatsCommand>());
        commandRegistry_->registerCommand(std::make_unique<UpgradeCommand>());
        commandRegistry_->registerCommand(std::make_unique<VersionCommand>());
        commandRegistry_->registerCommand(std::make_unique<BugCommand>());
        commandRegistry_->registerCommand(std::make_unique<FeedbackCommand>());
        commandRegistry_->registerCommand(std::make_unique<ExportCommand>());
        commandRegistry_->registerCommand(std::make_unique<SessionCommand>());
        commandRegistry_->registerCommand(std::make_unique<OutputStyleCommand>());
        commandRegistry_->registerCommand(std::make_unique<LoginCommand>());
        commandRegistry_->registerCommand(std::make_unique<PrivacyCommand>());
        commandRegistry_->registerCommand(std::make_unique<BranchCommand>());
        commandRegistry_->registerCommand(std::make_unique<RewindCommand>());
        commandRegistry_->registerCommand(std::make_unique<TagCommand>());

        // P1 命令
        commandRegistry_->registerCommand(std::make_unique<PlanCommand>());
        commandRegistry_->registerCommand(std::make_unique<LogoutCommand>());
        commandRegistry_->registerCommand(std::make_unique<EnvCommand>());
        commandRegistry_->registerCommand(std::make_unique<FastCommand>());

        // P2 命令
        commandRegistry_->registerCommand(std::make_unique<DiffCommand>());
        commandRegistry_->registerCommand(std::make_unique<UsageCommand>());
        commandRegistry_->registerCommand(std::make_unique<SummaryCommand>());
        commandRegistry_->registerCommand(std::make_unique<IssueCommand>());
        commandRegistry_->registerCommand(std::make_unique<PrCommentsCommand>());
        commandRegistry_->registerCommand(std::make_unique<SecurityReviewCommand>());
        commandRegistry_->registerCommand(std::make_unique<AutofixPrCommand>());
        commandRegistry_->registerCommand(std::make_unique<ThinkbackCommand>());
        commandRegistry_->registerCommand(std::make_unique<PluginCommand>());

        // P3 命令
        commandRegistry_->registerCommand(std::make_unique<IdeCommand>());
        commandRegistry_->registerCommand(std::make_unique<VoiceCommand>());
        commandRegistry_->registerCommand(std::make_unique<BridgeCommand>());
        commandRegistry_->registerCommand(std::make_unique<ProactiveCommand>());
        commandRegistry_->registerCommand(std::make_unique<ChromeCommand>());
        commandRegistry_->registerCommand(std::make_unique<MobileCommand>());
        commandRegistry_->registerCommand(std::make_unique<WorkflowsCommand>());

        // P6 命令
        commandRegistry_->registerCommand(std::make_unique<DebugCommand>());
        commandRegistry_->registerCommand(std::make_unique<DocCommand>());

        // P1 高价值命令
        commandRegistry_->registerCommand(std::make_unique<CommitPushPrCommand>());
        commandRegistry_->registerCommand(std::make_unique<ContextCommand>());
        commandRegistry_->registerCommand(std::make_unique<FilesCommand>());
        commandRegistry_->registerCommand(std::make_unique<TasksCommand>());
        commandRegistry_->registerCommand(std::make_unique<AgentsCommand>());

        // P2 常用命令
        commandRegistry_->registerCommand(std::make_unique<RenameCommand>());
        commandRegistry_->registerCommand(std::make_unique<SwarmCommand>());
        commandRegistry_->registerCommand(std::make_unique<CopyCommand>());
        commandRegistry_->registerCommand(std::make_unique<AddDirCommand>());
        commandRegistry_->registerCommand(std::make_unique<TeleportCommand>());
        commandRegistry_->registerCommand(std::make_unique<AdvisorCommand>());
        commandRegistry_->registerCommand(std::make_unique<BughunterCommand>());
        commandRegistry_->registerCommand(std::make_unique<ReleaseNotesCommand>());

        // P3 平台特性命令
        commandRegistry_->registerCommand(std::make_unique<DesktopCommand>());
        commandRegistry_->registerCommand(std::make_unique<Upgrade2Command>());

        // 额外命令
        commandRegistry_->registerCommand(std::make_unique<BtwCommand>());
        commandRegistry_->registerCommand(std::make_unique<InstallGithubAppCommand>());
        commandRegistry_->registerCommand(std::make_unique<InstallSlackAppCommand>());
        commandRegistry_->registerCommand(std::make_unique<RateLimitCommand>());
        commandRegistry_->registerCommand(std::make_unique<RemoteEnvCommand>());
        commandRegistry_->registerCommand(std::make_unique<StickersCommand>());
        commandRegistry_->registerCommand(std::make_unique<TerminalSetupCommand>());
        commandRegistry_->registerCommand(std::make_unique<PassesCommand>());

        // 更多命令
        commandRegistry_->registerCommand(std::make_unique<ReloadPluginsCommand>());
        commandRegistry_->registerCommand(std::make_unique<ResetLimitsCommand>());
        commandRegistry_->registerCommand(std::make_unique<ColorCommand>());
        commandRegistry_->registerCommand(std::make_unique<HeapdumpCommand>());
        commandRegistry_->registerCommand(std::make_unique<UltrareviewCommand>());
        commandRegistry_->registerCommand(std::make_unique<RemoteSetupCommand>());
        commandRegistry_->registerCommand(std::make_unique<ThinkbackPlayCommand>());
        commandRegistry_->registerCommand(std::make_unique<StatuslineCommand>());

        // 最后的命令
        commandRegistry_->registerCommand(std::make_unique<ForkCommand>());
        commandRegistry_->registerCommand(std::make_unique<BuddyCommand>());
        commandRegistry_->registerCommand(std::make_unique<TorchCommand>());
        commandRegistry_->registerCommand(std::make_unique<PeersCommand>());
        commandRegistry_->registerCommand(std::make_unique<UltralanCommand>());
        commandRegistry_->registerCommand(std::make_unique<AssistantCommand>());
        commandRegistry_->registerCommand(std::make_unique<BriefCommand>());
        commandRegistry_->registerCommand(std::make_unique<TmpDialogCommand>());

        // 最终命令
        commandRegistry_->registerCommand(std::make_unique<OverflowTestCommand>());
        commandRegistry_->registerCommand(std::make_unique<CtxInspectCommand>());

        spdlog::debug("Registered {} commands", commandRegistry_->size());
    }

    void initApiClient() {
        auto apiConfig = config_->getApiConfig();

        // 覆盖配置
        String provider = apiConfig.provider;
        String model = apiConfig.model;
        String apiKey = apiConfig.apiKey;
        String baseUrl = apiConfig.baseUrl;

        if (!provider_.empty()) {
            provider = provider_;
        }
        if (!model_.empty()) {
            model = model_;
        }

        // 从环境变量获取 API Key (优先级: 参数 > 环境变量 > OAuth > 配置)
        if (apiKey.empty()) {
            if (provider == "anthropic") {
                const char* key = std::getenv("ANTHROPIC_API_KEY");
                if (!key) key = std::getenv("CLAUDE_API_KEY");
                if (key) apiKey = key;
            } else {
                const char* key = std::getenv("OPENAI_API_KEY");
                if (key) apiKey = key;
            }
        }

        // Fallback to OAuth tokens when no env-var key is available
        if (apiKey.empty()) {
            auto& oauthMgr = oauth::OAuthManager::instance();
            String oauthProvider = (provider == "anthropic") ? "anthropic" : "openai";
            if (oauthMgr.isAuthenticated(oauthProvider)) {
                auto& client = oauthMgr.getClient(oauthProvider);
                auto token = client.getCurrentToken();
                if (token && !token->isExpired()) {
                    apiKey = token->accessToken;
                    spdlog::debug("Using OAuth token from /login for {}", oauthProvider);
                } else if (token && token->isExpired() && !token->refreshToken.empty()) {
                    // Attempt refresh
                    auto refreshed = client.refreshToken(token->refreshToken);
                    if (refreshed) {
                        apiKey = refreshed->accessToken;
                        spdlog::debug("Refreshed and using OAuth token for {}", oauthProvider);
                    }
                }
            }
        }

        // 验证 API Key (本地模型可以为空)
        if (apiKey.empty()) {
            spdlog::debug("No API key configured. Using empty key (for local models).");
        }

        // Create API client via agent_runner
        agent_runner::ApiClientParams params;
        params.provider = provider;
        params.model = model;
        params.apiKey = apiKey;
        params.baseUrl = baseUrl;
        params.maxTokens = config_->getMaxTokens();

        auto holder = agent_runner::createApiClient(params);
        apiClient_ = holder.raw;
        apiClientHolder_ = std::move(holder.owned);
    }

    void initAgentLoop() {
        agent_runner::AgentLoopParams params;
        params.apiClient = apiClient_;
        params.toolRegistry = toolRegistry_.get();
        params.config = config_.get();
        params.permissionSettings = permissionSettings_.get();
        params.permissionEngine = permissionEngine_.get();
        params.systemPromptOverride = systemPromptOverride_;
        params.appendSystemPrompt = appendSystemPrompt_;
        params.provider = provider_;
        params.model = model_;
        params.allowedTools = allowedToolsStr_;
        params.disallowedTools = disallowedToolsStr_;
        params.maxTurns = maxTurns_;
        params.continueSession = continueSession_;
        params.interactive = interactive_;

        auto holder = agent_runner::createAgentLoop(params);
        agentLoop_ = std::move(holder.loop);
        tokenTracker_ = std::move(holder.tokenTracker);
        contextInjector_ = std::move(holder.contextInjector);
        claudeMdLoader_ = std::move(holder.claudeMdLoader);

        // 设置回调
        agent_runner::setupCallbacks(*agentLoop_, useFtxui_, spinner_.get(),
#ifdef HAS_FTXUI
            ftxuiRepl_.get(),
#else
            nullptr,
#endif
            [this](const PermissionRequest& req) -> PermissionChoice {
                return promptPermission(req);
            });
    }

    void runRepl() {

        // Enable status line at bottom of terminal — prefer display name from ModelStrings
        if (tokenTracker_) {
            statusLine_ = std::make_unique<StatusLine>(std::cout);
            String rawModel = config_->getModel();
            if (rawModel.empty()) rawModel = "glm-5";
            statusLine_->enable(rawModel, *tokenTracker_);
            auto ms = AppState::instance().modelStrings();
            statusLine_->setModelName(ms.has_value() ? ms->displayName : rawModel);
        }

        std::cout << "Type your message and press Enter. Type /help for commands.\n\n";

        // 设置 locale 支持 UTF-8
        setlocale(LC_CTYPE, "en_US.UTF-8");
        setlocale(LC_ALL, "en_US.UTF-8");

#ifdef USE_READLINE
        // Setup Completer with commands, working dir, and history
        CompleterConfig completerConfig;
        if (commandRegistry_) {
            for (const auto* cmd : commandRegistry_->getCommands()) {
                if (cmd) completerConfig.commands.push_back(cmd->name());
            }
        }
        completerConfig.workingDir = std::filesystem::current_path();
        completer_ = std::make_unique<Completer>(completerConfig);

        // Initialize readline (sets up completion, hints, loads history)
        readline_support::initReadline(commandRegistry_.get(), completer_.get());
#endif

        // REPL 循环
        while (true) {
            String input;

#ifdef USE_READLINE
            // 使用 readline (支持 UTF-8 和行编辑)
            char* line = readline("\001\033[1;32m\002❯ \001\033[0m\002");
            if (!line) {
                // EOF (Ctrl+D)
                session::saveSession(agentLoop_.get());
                std::cout << "\nGoodbye!\n";
                break;
            }
            input = line;
            free(line);

            // 换行执行命令（提示在上方，会自然滚动上去）
            std::cout << "\n";

            // 添加到历史（非空行）并保存到文件
            if (!input.empty() && input.find_first_not_of(" \t\n\r") != String::npos) {
                add_history(input.c_str());
                saveHistory(input);
            }
#else
            // 回退到 getline
            std::cout << PromptRenderer::render();
            std::cout.flush();
            std::getline(std::cin, input);

            if (std::cin.eof()) {
                session::saveSession(agentLoop_.get());
                std::cout << "\nGoodbye!\n";
                break;
            }
#endif

            // 处理斜杠命令
            if (!input.empty() && input[0] == '/') {
                if (handleSlashCommand(input)) {
                    continue;
                }
            }

            // 运行 Agent
            runOnce(input);
        }
    }

#ifdef HAS_FTXUI
    void runFtxuiRepl() {
        ftxuiRepl_ = std::make_unique<FtxuiRepl>();
        auto replPtr = ftxuiRepl_.get();

        // Link AppState for reactive state accessors
        ftxuiRepl_->setAppState(&AppState::instance());

        // Show model info in header — prefer display name from ModelStrings
        if (agentLoop_) {
            auto ms = AppState::instance().modelStrings();
            String info = ms.has_value() ? ms->displayName : (config_->getModel().empty() ? "glm-5" : config_->getModel());
            ftxuiRepl_->setModelInfo(info);
        }

        ftxuiRepl_->setOnSubmit([this, replPtr](const String& input) {
            // Join previous agent thread before starting a new one
            if (agentThread_.joinable()) agentThread_.join();
            // Agent must run on background thread — UI thread cannot block
            agentThread_ = std::thread([this, replPtr, input]() {
                g_agentStreaming.store(true, std::memory_order_release);
                try {
                    auto result = agentLoop_->runStreaming(input,
                        [replPtr](const String& token) {
                            replPtr->appendStreamText(token);
                        });
                    if (!result.has_value()) {
                        if (result.error() == "Cancelled by user") {
                            // UI already transitioned to idle in the ESC/Ctrl+C handler
                        } else {
                            replPtr->finishStream(false, result.error());
                        }
                    } else {
                        replPtr->finishStream(true, "");
                        if (tokenTracker_) {
                            replPtr->setContextInfo(
                                tokenTracker_->getInputTokens() + tokenTracker_->getOutputTokens(),
                                tokenTracker_->getContextWindow(),
                                tokenTracker_->estimateCost()
                            );
                        }
                    }
                } catch (const std::exception& e) {
                    replPtr->finishStream(false, String(e.what()));
                }
                g_agentStreaming.store(false, std::memory_order_release);
            });
        });

        ftxuiRepl_->setOnCancel([this]() {
            // Called from the UI thread when ESC or Ctrl+C is pressed during streaming
            if (agentLoop_) {
                agentLoop_->cancel();
            }
        });

        ftxuiRepl_->setOnCommand([this](const String& input) {
            return handleSlashCommand(input);
        });

        ftxuiRepl_->run();
        // Ensure agent thread is joined before FtxuiRepl is destroyed
        if (agentThread_.joinable()) agentThread_.join();
    }
#endif

    void runOnce(const String& input) {
        if (input.empty()) return;

        spinner_ = std::make_unique<Spinner>(std::cerr);
        spinnerStart_ = std::chrono::steady_clock::now();
        // Provide token count to spinner for progress display
        spinner_->setTokenProvider([this]() {
            return agentLoop_->getTokenTracker().getTotalTokens();
        });
        spinner_->start(tr("status.thinking"));

        // Mark streaming state so SIGINT can cancel instead of exit
        g_agentStreaming.store(true, std::memory_order_release);

        // In basic (non-FTXUI) mode, tool callbacks write directly to stdout,
        // which interleaves with streaming text and causes garbled output.
        // Solution: disable readline redisplay during streaming, and let
        // tool results flow after the current text block.
        auto result = agentLoop_->runStreaming(input, [](const String& token) {
            std::cout << token << std::flush;
        });

        g_agentStreaming.store(false, std::memory_order_release);
        spinner_->stop();

        // Reset Ctrl+C counter (any successful turn clears pending exit)
        g_ctrlCCount.store(0, std::memory_order_relaxed);

        if (!result) {
            if (result.error() == "Cancelled by user") {
                std::cout << "\n" << AnsiStyle::DIM << "Cancelled" << AnsiStyle::RESET << "\n";
            } else {
                // Error message with proper formatting
                String errorMsg = String(AnsiStyle::RED) + "Error: " + result.error() + AnsiStyle::RESET;
                std::cout << "\n"
                          << MessageResponse::format(errorMsg)
                          << "\n";
            }
        }

        std::cout << "\n";

        // Turn duration summary — matching TS style: ● Pondered in 23s · 4.2K tokens
        auto& tracker = agentLoop_->getTokenTracker();
        auto turnEnd = std::chrono::steady_clock::now();
        double turnDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            turnEnd - spinnerStart_).count() / 1000.0;
        std::cout << TurnDurationRenderer::render(turnDuration, tracker.getTotalTokens()) << "\n";

        // Refresh status line with updated token/cost info
        if (statusLine_) statusLine_->refresh();
    }

    bool handleSlashCommand(const String& input) {
        // 解析命令
        size_t space = input.find(' ');
        String cmd = (space == String::npos) ? input.substr(1) : input.substr(1, space - 1);
        String args = (space == String::npos) ? "" : input.substr(space + 1);

        // 内置命令
        if (cmd == "exit" || cmd == "quit" || cmd == "q") {
            session::saveSession(agentLoop_.get());
            std::cout << "Goodbye!\n";
            CleanupRegistry::runCleanupFunctions();  // includes restoreTerminal()
            _Exit(0);
        }

        if (cmd == "clear" || cmd == "c") {
            agentLoop_->reset();
            std::cout << "Conversation cleared.\n";
            return true;
        }

        // 通过 CommandRegistry 执行
        CommandContext context;
        context.agentLoop = agentLoop_.get();
        context.tools = toolRegistry_.get();
        context.permissionEngine = permissionEngine_.get();
        context.workDir = std::filesystem::current_path();

        auto result = commandRegistry_->execute(cmd, args, context);
        if (result) {
            std::cout << *result << "\n";
            return true;
        }

        // 未知命令
        std::cout << "Unknown command: /" << cmd << "\n";
        std::cout << "Type /help for available commands.\n";
        return true;
    }

    PermissionChoice promptPermission(const PermissionRequest& req) {
#ifdef HAS_FTXUI
        // FTXUI mode: delegate to FtxuiRepl which shows arrow-key selection
        if (useFtxui_ && ftxuiRepl_) {
            return ftxuiRepl_->promptPermission(req.toolName, req.activityDescription);
        }
#endif

        // Readline mode: use PermissionPromptRenderer for bordered box + arrow-key selection
        if (spinner_) spinner_->stop();

        PermissionPromptRenderer renderer(std::cout);
        return renderer.render(req.toolName, req.activityDescription);
    }

private:
    // 命令行参数
    String prompt_;
    bool interactive_ = false;
    bool verbose_ = false;
    bool dangerouslySkipPermissions_ = false;
    bool autoMode_ = false;
#ifdef HAS_FTXUI
    bool useFtxui_ = true;
#else
    bool useFtxui_ = false;
#endif
    String model_;
    String provider_;
    String permissionModeStr_;
    std::vector<String> allowedToolsStr_;
    std::vector<String> disallowedToolsStr_;
    int maxTurns_ = 0;
    bool continueSession_ = false;
    String systemPromptOverride_;
    String appendSystemPrompt_;

    // 组件
    std::unique_ptr<AppConfig> config_;
    std::unique_ptr<PermissionSettings> permissionSettings_;
    std::unique_ptr<RuleEngine> permissionEngine_;
    std::unique_ptr<ToolRegistry> toolRegistry_;
    std::unique_ptr<CommandRegistry> commandRegistry_;
    std::unique_ptr<TokenTracker> tokenTracker_;
    std::unique_ptr<AgentLoop> agentLoop_;
    std::unique_ptr<ContextInjector> contextInjector_;
    std::unique_ptr<ClaudeMdLoader> claudeMdLoader_;
    std::unique_ptr<Spinner> spinner_;
    std::chrono::steady_clock::time_point spinnerStart_;
    std::thread agentThread_;
    std::unique_ptr<StatusLine> statusLine_;
    std::unique_ptr<Completer> completer_;

#ifdef HAS_FTXUI
    std::unique_ptr<FtxuiRepl> ftxuiRepl_;
#endif

    ApiClient* apiClient_ = nullptr;
    std::unique_ptr<ApiClient> apiClientHolder_;
    std::shared_ptr<McpManager> mcpManager_;

    static String truncateToolInput(const String& input, size_t maxLen) {
        if (input.size() <= maxLen) return input;
        return input.substr(0, maxLen - 3) + "...";
    }

    static String truncateThinking(const String& s, size_t maxLen) {
        if (s.size() <= maxLen) return s;
        // Find last newline or space before maxLen for clean truncation
        size_t cut = s.rfind('\n', maxLen);
        if (cut == String::npos) cut = s.rfind(' ', maxLen);
        if (cut == String::npos) cut = maxLen - 3;
        return s.substr(0, cut) + "...";
    }
};

int main(int argc, char* argv[]) {
    // Register signal handlers
    installSignalHandlers();

    try {
        ClaudeCodeApp app;
        int rc = app.run(argc, argv);
        CleanupRegistry::runCleanupFunctions();
        std::_Exit(rc);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        CleanupRegistry::runCleanupFunctions();
        std::_Exit(1);
    }
}
