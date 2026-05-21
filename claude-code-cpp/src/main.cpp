#include <iostream>
#include <string>
#include <memory>
#include <filesystem>
#include <fstream>
#include <thread>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <clocale>
#include <csignal>
#include <atomic>
#include <chrono>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

#include <claude/core/AgentLoop.hpp>
#include <claude/core/TokenTracker.hpp>
#include <claude/core/CleanupRegistry.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/ToolContext.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/permission/PermissionSettings.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <claude/config/AppConfig.hpp>
#include <claude/console/BannerPrinter.hpp>
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

// FTXUI support (optional)
#ifdef HAS_FTXUI
#include <claude/ui/FtxuiRepl.hpp>
#include <claude/bootstrap/AppState.hpp>
#endif

using namespace claude;

// Bring new formatters into scope
using claude::Figures;
using claude::MessageResponse;

// Signal handler globals — context-aware Ctrl+C handling
static std::atomic<bool> g_agentStreaming{false};
static AgentLoop* g_agentLoop = nullptr;
static std::atomic<bool> g_interruptRequested{false};  // Set by SIGINT, checked by agent loop
static std::atomic<int> g_ctrlCCount{0};
static std::chrono::steady_clock::time_point g_lastCtrlCTime{};

#ifdef USE_READLINE
// readline 命令补全
static CommandRegistry* g_commandRegistry = nullptr;
static Completer* g_completer = nullptr;
static int g_lastHintLines = 0;  // 记录上次显示了多少行提示

// 历史文件路径
static std::filesystem::path getHistoryFilePath() {
    const char* home = std::getenv("HOME");
    if (!home) return {};
    auto path = std::filesystem::path(home) / ".claude";
    std::filesystem::create_directories(path);
    return path / "history.txt";
}

// 加载历史
static void loadHistory() {
    auto historyPath = getHistoryFilePath();
    if (historyPath.empty()) return;

    std::ifstream file(historyPath);
    if (!file) return;

    String line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            add_history(line.c_str());
        }
    }
}

// 保存历史
static void saveHistory(const String& entry) {
    auto historyPath = getHistoryFilePath();
    if (historyPath.empty()) return;

    std::ofstream file(historyPath, std::ios::app);
    if (file) {
        file << entry << "\n";
    }
}

// 补全生成函数（Tab 补全用）— uses Completer for /commands, @file, and history
static char* completionGenerator(const char* text, int state) {
    static std::vector<String> matches;
    static size_t matchIndex;

    if (state == 0) {
        matches.clear();
        matchIndex = 0;

        String prefix = text;

        // Slash commands from CommandRegistry
        if (prefix.starts_with("/") && g_commandRegistry) {
            auto allCommands = g_commandRegistry->getCommands();
            for (const auto* cmd : allCommands) {
                if (cmd) {
                    String cmdWithSlash = "/" + cmd->name();
                    if (cmdWithSlash.find(prefix) == 0) {
                        matches.push_back(cmdWithSlash);
                    }
                }
            }
        }

        // File/command/history from Completer
        if (g_completer && !prefix.empty()) {
            auto result = g_completer->getSuggestions(prefix, static_cast<int>(prefix.size()));
            for (const auto& suggestion : result.suggestions) {
                String display = suggestion.displayText;
                // Avoid duplicates with slash commands
                if (!display.starts_with("/") || !prefix.starts_with("/")) {
                    matches.push_back(display);
                }
            }
        }

        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    }

    if (matchIndex < matches.size()) {
        return strdup(matches[matchIndex++].c_str());
    }
    return nullptr;
}

// Tab 补全函数 — delegates to Completer for /commands, @files, and history
static char** commandCompletion(const char* text, int start, int /*end*/) {
    rl_completion_append_character = '\0';
    // Handle slash commands, @files, and history via Completer
    if (text[0] == '/' || text[0] == '@' || start > 0) {
        return rl_completion_matches(text, completionGenerator);
    }
    return nullptr;
}

// 计算字符串的显示宽度（考虑UTF-8多字节字符）
static int displayWidth(const char* str, int len) {
    int width = 0;
    for (int i = 0; i < len && str[i]; ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            // ASCII: 1 byte, 1 column
            width += 1;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8: usually 2 columns for CJK, 1 for others
            width += 2;  // Assume CJK for simplicity
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8: usually 2 columns (CJK)
            width += 2;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8: 2 columns (emoji etc)
            width += 2;
            i += 4;
        } else {
            i += 1;
        }
    }
    return width;
}

// 获取终端宽度
static int getTerminalWidth() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80;  // Default
}

// 记录上次显示的行数，用于正确清除
static int g_lastDisplayLines = 1;

// 自定义 redisplay - 显示联想命令在下方
static void hintRedisplay() {
    // During streaming, skip redisplay entirely — streaming text writes
    // to stdout and any cursor manipulation from readline would garble it.
    if (g_agentStreaming.load(std::memory_order_acquire)) {
        return;
    }

    // 获取联想命令
    std::vector<std::pair<String, String>> matches;
    int count = 0;

    if (rl_line_buffer && rl_end >= 1 && rl_line_buffer[0] == '/') {
        String prefix(rl_line_buffer, rl_end);

        if (g_commandRegistry) {
            auto allCommands = g_commandRegistry->getCommands();
            for (const auto* cmd : allCommands) {
                if (cmd) {
                    String cmdWithSlash = "/" + cmd->name();
                    if (cmdWithSlash.find(prefix) == 0 && cmdWithSlash.length() > prefix.length()) {
                        matches.push_back({cmdWithSlash, cmd->description()});
                    }
                }
            }
        }

        std::sort(matches.begin(), matches.end());
        count = std::min((int)matches.size(), 5);
    }

    // 计算输入行数（考虑换行）
    int termWidth = getTerminalWidth();
    int promptWidth = 2;  // "❯ " = 2 columns
    int inputWidth = displayWidth(rl_line_buffer, rl_end);
    int totalWidth = promptWidth + inputWidth;
    int inputLines = (totalWidth + termWidth - 1) / termWidth;  // Ceiling division
    if (inputLines < 1) inputLines = 1;

    // Use the larger of current lines or last displayed lines to ensure we clear everything
    int linesToClear = std::max(inputLines, g_lastDisplayLines);

    // 清除所有输入行（移动到第一行，然后向下清除）
    std::cout << "\r";  // Move to start of current line
    for (int i = 1; i < linesToClear; i++) {
        std::cout << "\033[A";  // Move up to first line
    }
    std::cout << "\r";  // Ensure at start of first line

    // Clear all lines from top to bottom
    for (int i = 0; i < linesToClear; i++) {
        std::cout << "\033[2K";  // Clear entire line
        if (i < linesToClear - 1) {
            std::cout << "\033[B";  // Move down to next line
        }
    }

    // Move back to top line
    for (int i = 1; i < linesToClear; i++) {
        std::cout << "\033[A";  // Move up
    }
    std::cout << "\r";  // Ensure at start

    // 显示提示符 (与 readline prompt 一致)
    std::cout << "\033[1;32m❯ \033[0m";

    // 显示输入内容
    std::cout.write(rl_line_buffer, rl_end);

    // 清除下方所有内容
    std::cout << "\033[J";

    // Update last display lines for next call
    g_lastDisplayLines = inputLines;

    // 显示联想提示
    if (count > 0) {
        std::cout << "\n";
        for (int i = 0; i < count; i++) {
            std::cout << "  \033[36m" << matches[i].first << "\033[0m";
            if (!matches[i].second.empty() && matches[i].second.length() < 40) {
                std::cout << " \033[90m- " << matches[i].second << "\033[0m";
            }
            if (i < count - 1) std::cout << "\n";
        }

        // 将光标移回输入行
        std::cout << "\033[" << count << "A";  // 上移 count 行
    }

    // 计算光标位置（考虑多行）
    int cursorWidth = promptWidth + displayWidth(rl_line_buffer, rl_point);
    int cursorLine = cursorWidth / termWidth;  // Which line (0-indexed)
    int cursorCol = cursorWidth % termWidth;   // Column on that line
    if (cursorCol == 0 && cursorWidth > 0) {
        // At start of a new line - adjust
        cursorCol = termWidth;
        cursorLine--;
    }

    // 移到正确位置
    std::cout << "\r";
    if (inputLines > 1 && cursorLine < inputLines - 1) {
        // Need to move up from bottom line
        std::cout << "\033[" << (inputLines - 1 - cursorLine) << "A";
    }
    if (cursorCol > 0) {
        std::cout << "\033[" << cursorCol << "C";
    }

    std::cout << std::flush;
}
#endif

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
#ifdef HAS_FTXUI
        app.add_flag("--ftxui", useFtxui_, "Use FTXUI component-based terminal UI");
#endif

        try {
            app.parse(argc, argv);
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

        // 设置日志级别 — 日志输出到 stderr，避免与 stdout 上的流式内容混合
        auto stderrLogger = spdlog::stderr_color_mt("stderr");
        spdlog::set_default_logger(stderrLogger);
        spdlog::set_level(verbose_ ? spdlog::level::debug : spdlog::level::info);

        // 初始化 HTTP 代理 (从环境变量)
        auto proxyConfig = Http::loadProxyFromEnv();
        if (proxyConfig.enabled()) {
            Http::setProxy(proxyConfig);
            spdlog::info("HTTP proxy configured: {}:{}", proxyConfig.host, proxyConfig.port);

            auto caCert = Http::getCaCertPath();
            if (caCert) {
                spdlog::info("CA certificate: {}", *caCert);
            }
        }

        // 初始化权限
        permissionSettings_ = std::make_unique<PermissionSettings>();
        permissionEngine_ = std::make_unique<RuleEngine>(*permissionSettings_);

        // 应用权限模式
        if (dangerouslySkipPermissions_) {
            permissionSettings_->setMode(PermissionMode::Bypass);
            spdlog::warn("Permission checks bypassed - this is dangerous!");
        } else if (autoMode_ || permissionModeStr_ == "auto") {
            permissionSettings_->setMode(PermissionMode::Auto);
            permissionEngine_->yoloClassifier().setEnabled(true);
            spdlog::info("Auto mode enabled (AI classifier will decide permissions)");
        } else if (!permissionModeStr_.empty()) {
            auto mode = parsePermissionMode(permissionModeStr_);
            if (mode) {
                permissionSettings_->setMode(*mode);
                if (*mode == PermissionMode::Auto) {
                    permissionEngine_->yoloClassifier().setEnabled(true);
                    spdlog::info("Auto mode enabled (AI classifier will decide permissions)");
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

        // 初始化 API 客户端
        initApiClient();

        // 初始化 AgentLoop
        initAgentLoop();

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

        // 从环境变量获取 API Key (优先级: 参数 > 环境变量 > 配置)
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

        // 验证 API Key (本地模型可以为空)
        if (apiKey.empty()) {
            spdlog::debug("No API key configured. Using empty key (for local models).");
        }

        // Auto-detect provider type based on base URL
        // Most self-hosted servers (vLLM, Ollama, LM Studio) use OpenAI-compatible format
        bool isOpenAICompatible = false;
        if (!baseUrl.empty()) {
            // Check if it's the official Anthropic API
            bool isAnthropicAPI = (baseUrl.find("api.anthropic.com") != String::npos);
            // Custom base URLs typically use OpenAI format
            isOpenAICompatible = !isAnthropicAPI;

            if (isOpenAICompatible && provider == "anthropic") {
                spdlog::debug("Custom base URL detected ({}), using OpenAI-compatible format", baseUrl);
                provider = "openai";
            }
        }

        // 创建客户端并设置所有配置
        if (provider == "anthropic") {
            auto client = std::make_unique<AnthropicClient>(apiKey);
            client->setModel(model);
            client->setMaxTokens(config_->getMaxTokens());
            if (!baseUrl.empty()) {
                client->setBaseUrl(baseUrl);
            }
            apiClient_ = client.get();
            apiClientHolder_ = std::move(client);
        } else {
            auto client = std::make_unique<OpenAIClient>(apiKey);
            client->setModel(model);
            client->setMaxTokens(config_->getMaxTokens());
            if (!baseUrl.empty()) {
                client->setBaseUrl(baseUrl);
            }
            apiClient_ = client.get();
            apiClientHolder_ = std::move(client);
        }

        spdlog::debug("Using {} provider with model {}", provider, model);
    }

    void initAgentLoop() {
        String systemPrompt = config_->getSystemPrompt();
        tokenTracker_ = std::make_unique<TokenTracker>();

        agentLoop_ = std::make_unique<AgentLoop>(
            *apiClient_,
            *toolRegistry_,
            systemPrompt,
            *tokenTracker_
        );

        // Wire up signal handler global for Ctrl+C cancel
        g_agentLoop = agentLoop_.get();

        agentLoop_->setPermissionEngine(permissionEngine_.get());

        // Initialize auto-compact with context window size
        // Default 200k tokens for Claude 3.5/4 models
        int contextWindow = 200000;
        if (apiClient_) {
            String model = apiClient_->getModelName();
            if (model.find("haiku") != String::npos) {
                contextWindow = 200000;
            } else if (model.find("opus") != String::npos) {
                contextWindow = 200000;
            }
        }
        agentLoop_->initAutoCompact(contextWindow);

        // 设置回调
        setupCallbacks();
    }

    void setupCallbacks() {
        // 工具事件回调 - matches TS tool use display
        agentLoop_->setOnToolEvent([this](const ToolEvent& event) {
            if (event.phase == ToolEventPhase::Start) {
                if (spinner_) spinner_->stop();
                if (!useFtxui_) {
                    // Show tool badge — ⎿ prefix with per-tool colored badge
                    std::cout << "\n\033[s"
                              << AnsiStyle::DIM << "  ⎿ "
                              << AnsiStyle::RESET << MessageResponse::formatToolBadge(event.toolName)
                              << " " << AnsiStyle::DIM << truncateToolInput(event.arguments, 60)
                              << "\033[u" << std::flush;
                } else if (ftxuiRepl_) {
                    ftxuiRepl_->addToolMessage(event.toolName, event.arguments, "");
                }
            }
        });

        // 流式开始回调
        agentLoop_->setOnStreamStart([this]() {
            if (spinner_) spinner_->stop();
            if (!useFtxui_) {
                // Don't output anything — the first onToken will start the text
            }
        });

        // Thinking callback — update thinking summary for FTXUI
        agentLoop_->setOnThinking([this](const String& thinking) {
            if (ftxuiRepl_ && useFtxui_) {
                ftxuiRepl_->updateThinkingSummary(thinking);
            }
        });

        // ========== 新增回调：流畅输出的关键 ==========

        // content_block_stop 回调 — 每个内容块完成时触发
        // 这是原版 TS 在 content_block_stop 时 yield AssistantMessage 的 C++ 等价物
        agentLoop_->setOnContentBlockStop([this](const String& blockType, int index, const String& content) {
            if (useFtxui_ && ftxuiRepl_) {
                if (blockType == "tool_use") {
                    spdlog::debug("Content block stop: tool_use at index {}", index);
                } else if (blockType == "thinking") {
                    // Thinking block complete — store for expand/collapse
                    if (!content.empty()) {
                        ftxuiRepl_->addThinkingMessage(content);
                    }
                }
            }
        });

        // 工具结果流式回调 — 每个工具完成时立即显示结果
        // 匹配原版 TS 的 StreamingToolExecutor.getCompletedResults() 行为
        agentLoop_->setOnToolResult([this](const String& toolName, const String& result, bool isError) {
            if (!useFtxui_) {
                // readline模式：工具结果在流式文本之间插入
                // 需要在正确的位置输出，避免与 readline 光标操作冲突
                // 保存光标 → 新行 → 输出结果 → 恢复光标
                if (!result.empty()) {
                    // Save cursor position, move to new line, output, restore cursor
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
            } else if (ftxuiRepl_) {
                ftxuiRepl_->addToolMessage(toolName, "", result);
            }
        });

        // TAOR循环继续回调 — 模型开始新一轮 Think 时触发
        // In FTXUI mode: don't inject into streaming text (that clutters the output).
        // The status bar already shows "Running" with elapsed time and turn counter.
        agentLoop_->setOnLoopContinue([this](int iteration, int maxIterations) {
            if (!useFtxui_) {
                std::cout << "\n\033[s"
                          << AnsiStyle::DIM << "  ⟳ Continuing... (turn "
                          << iteration << ")" << AnsiStyle::RESET
                          << "\033[u" << std::flush;
            }
        });

        // 上下文压缩预警回调
        agentLoop_->setOnCompactWarning([this](int level, long currentTokens, long maxTokens) {
            double pct = static_cast<double>(currentTokens) / maxTokens * 100.0;
            String msg = level >= 2
                ? "Context window nearly full (" + std::to_string(static_cast<int>(pct)) + "%). Auto-compacting..."
                : "Context window usage at " + std::to_string(static_cast<int>(pct)) + "%";
            if (useFtxui_ && ftxuiRepl_) {
                ftxuiRepl_->addSystemMessage(msg);
            } else {
                std::cout << "\n" << AnsiStyle::YELLOW << "⚠ " << msg
                          << AnsiStyle::RESET << "\n";
            }
        });

        // 权限确认回调
        agentLoop_->setOnPermissionRequest([this](const PermissionRequest& req) {
            return promptPermission(req);
        });
    }

    void runRepl() {
        // 打印横幅
        BannerPrinter banner(std::cout);
        banner.printWelcome();

        // Enable status line at bottom of terminal
        if (tokenTracker_) {
            statusLine_ = std::make_unique<StatusLine>(std::cout);
            String model = config_->getModel();
            if (model.empty()) model = "glm-5";
            statusLine_->enable(model, *tokenTracker_);
        }

        std::cout << "Type your message and press Enter. Type /help for commands.\n\n";

        // 设置 locale 支持 UTF-8
        setlocale(LC_CTYPE, "en_US.UTF-8");
        setlocale(LC_ALL, "en_US.UTF-8");

#ifdef USE_READLINE
        // GNU readline 配置
        rl_variable_bind("input-meta", "on");
        rl_variable_bind("output-meta", "on");
        rl_variable_bind("convert-meta", "off");
        rl_reset_terminal(nullptr);

        // 设置命令补全
        g_commandRegistry = commandRegistry_.get();

        // Setup Completer with commands, working dir, and history
        CompleterConfig completerConfig;
        if (commandRegistry_) {
            for (const auto* cmd : commandRegistry_->getCommands()) {
                if (cmd) completerConfig.commands.push_back(cmd->name());
            }
        }
        completerConfig.workingDir = std::filesystem::current_path();
        completer_ = std::make_unique<Completer>(completerConfig);
        g_completer = completer_.get();

        rl_attempted_completion_function = commandCompletion;

        // 设置实时联想显示
        rl_redisplay_function = hintRedisplay;

        // 加载历史
        loadHistory();
#endif

        // REPL 循环
        while (true) {
            String input;

#ifdef USE_READLINE
            // 使用 readline (支持 UTF-8 和行编辑)
            char* line = readline("\001\033[1;32m\002❯ \001\033[0m\002");
            if (!line) {
                // EOF (Ctrl+D)
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

        // Show model info in header
        if (agentLoop_) {
            String info = config_->getModel();
            if (info.empty()) info = "glm-5";
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
            std::cout << "Goodbye!\n";
            CleanupRegistry::runCleanupFunctions();
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
    bool useFtxui_ = false;
    String model_;
    String provider_;
    String permissionModeStr_;

    // 组件
    std::unique_ptr<AppConfig> config_;
    std::unique_ptr<PermissionSettings> permissionSettings_;
    std::unique_ptr<RuleEngine> permissionEngine_;
    std::unique_ptr<ToolRegistry> toolRegistry_;
    std::unique_ptr<CommandRegistry> commandRegistry_;
    std::unique_ptr<TokenTracker> tokenTracker_;
    std::unique_ptr<AgentLoop> agentLoop_;
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

/// Signal handler — context-aware interrupt handling
/// - During streaming: sets g_interruptRequested flag (async-signal-safe)
/// - At idle prompt: double-press-to-exit (800ms window)
/// - SIGTERM always exits immediately
///
/// IMPORTANT: Only async-signal-safe operations are used here.
/// No spdlog, no mutex, no heap allocation, no non-atomic writes.
static void signalHandler(int signal) {
    if (signal == SIGTERM) {
        _Exit(143);  // Skip cleanup — not safe in signal handler
    }

    if (signal == SIGINT) {
        // If agent is streaming, set the interrupt flag.
        // The agent loop checks this flag and cancels itself.
        if (g_agentStreaming.load(std::memory_order_acquire)) {
            g_interruptRequested.store(true, std::memory_order_release);
            // Also abort the API client's stream directly (atomic store, signal-safe)
            if (g_agentLoop) {
                g_agentLoop->cancel();
            }
            return;
        }

        // At idle prompt: double-press-to-exit
        // write() is async-signal-safe
        const char msg[] = "\nPress Ctrl+C again to exit\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);

        auto now = std::chrono::steady_clock::now();
        int count = g_ctrlCCount.fetch_add(1, std::memory_order_relaxed) + 1;

        if (count >= 2) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_lastCtrlCTime).count();
            if (elapsed < 800) {
                _Exit(0);
            }
            // Too slow — reset and treat as first press
            g_ctrlCCount.store(1, std::memory_order_relaxed);
        }

        g_lastCtrlCTime = now;
    }
}

int main(int argc, char* argv[]) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

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
