cmake_minimum_required(VERSION 3.20)
project(claude_code_cpp VERSION 0.1.0 LANGUAGES CXX)

# C++23 标准 (for std::expected)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt Creator / CMake 兼容性: 显式添加 C++23 编译标志
# 必须在 project() 之后、target 之前设置
if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23")
endif()

# 编译选项
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ========== OpenSSL 优先设置 ==========
# 必须在任何其他 include 路径之前设置，确保找到 OpenSSL 3.x 而非旧版 1.1
if(APPLE)
    file(GLOB OPENSSL_VERSIONS "/usr/local/Cellar/openssl@3/*")
    if(OPENSSL_VERSIONS)
        list(SORT OPENSSL_VERSIONS)
        list(REVERSE OPENSSL_VERSIONS)
        list(GET OPENSSL_VERSIONS 0 LATEST_OPENSSL)
        # 使用 CMAKE_REQUIRED_INCLUDES 确保 find_package 使用正确的 OpenSSL
        set(OPENSSL_ROOT_DIR "${LATEST_OPENSSL}" CACHE PATH "OpenSSL root" FORCE)
        set(OPENSSL_INCLUDE_DIR "${LATEST_OPENSSL}/include" CACHE PATH "OpenSSL include" FORCE)
        # 强制在所有编译的最前面添加 OpenSSL 3.x include 路径
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${LATEST_OPENSSL}/include")
        message(STATUS "Using OpenSSL: ${LATEST_OPENSSL}")
    endif()
endif()

# ========== 第三方库 ==========
# 使用 homebrew 安装的版本

# 线程支持 - 手动创建 Threads::Threads 目标以绕过 FindThreads 模块问题
# 必须在 spdlog 之前创建，因为 spdlog 会调用 find_package(Threads)
if(NOT TARGET Threads::Threads)
    add_library(Threads::Threads INTERFACE IMPORTED GLOBAL)
    target_compile_options(Threads::Threads INTERFACE "-pthread")
    target_link_options(Threads::Threads INTERFACE "-pthread")
    set(Threads_FOUND TRUE CACHE INTERNAL "Threads found")
endif()

find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(CLI11 REQUIRED)

# FTXUI - C++ Functional Terminal User Interface
find_path(FTXUI_INCLUDE_DIR ftxui/component/component.hpp
    PATHS /usr/local/include
    NO_DEFAULT_PATH
)
if(FTXUI_INCLUDE_DIR)
    message(STATUS "Found FTXUI: ${FTXUI_INCLUDE_DIR}")
else()
    message(WARNING "FTXUI not found, terminal UI will fallback to basic output")
endif()

if(APPLE)
    set(APPLE_FRAMEWORKS "-framework CoreFoundation" "-framework Security")
endif()

# 自动检测 Homebrew OpenSSL 3.x 路径
if(APPLE)
    # 查找最新的 openssl@3 版本
    file(GLOB OPENSSL_VERSIONS "/usr/local/Cellar/openssl@3/*")
    if(OPENSSL_VERSIONS)
        list(SORT OPENSSL_VERSIONS)
        list(REVERSE OPENSSL_VERSIONS)
        list(GET OPENSSL_VERSIONS 0 LATEST_OPENSSL)
        set(OPENSSL_ROOT_DIR "${LATEST_OPENSSL}" CACHE PATH "OpenSSL root")
        set(OPENSSL_INCLUDE_DIR "${LATEST_OPENSSL}/include" CACHE PATH "OpenSSL include dir")
        # 强制 OpenSSL 3 头文件路径在所有其他 include 之前
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${LATEST_OPENSSL}/include")
        message(STATUS "Using OpenSSL: ${LATEST_OPENSSL}")
    endif()
endif()
find_package(OpenSSL REQUIRED)

# 确保 OpenSSL 3 头文件路径优先于 /usr/local/include (那里有旧版 OpenSSL 1.1)

# httplib header-only 版本，启用 SSL
add_library(httplib INTERFACE)
# 使用 -I (非 -isystem) 确保 OpenSSL 3 头文件路径优先
target_include_directories(httplib BEFORE INTERFACE
    ${OPENSSL_INCLUDE_DIR}
)
target_include_directories(httplib SYSTEM INTERFACE
    /usr/local/include
)
target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto)

# ========== 主库 ==========

# 核心源文件
set(CORE_SOURCES
    src/core/AgentLoop.cpp
    src/core/AgentLoopCompact.cpp
    src/core/AgentLoopStreaming.cpp
    src/core/AgentLoopTools.cpp
    src/core/BackgroundAgentHandle.cpp
    src/core/UnifiedTaskStore.cpp
    src/core/TokenTracker.cpp
    src/core/ConversationPersistence.cpp
    src/core/HookManager.cpp
    src/core/StreamingToolExecutor.cpp
    src/core/compact/CompactService.cpp
    src/core/compact/CompactPrompt.cpp
    src/core/compact/AutoCompact.cpp
    src/core/compact/MicroCompact.cpp
    src/core/compact/PostCompactCleanup.cpp
    src/core/compact/SessionMemoryCompact.cpp
    src/core/compact/CompactWarningHook.cpp
    src/core/compact/MessageGrouping.cpp
    src/core/compact/ApiMicroCompact.cpp
    src/core/HistoryManager.cpp
    src/core/MigrationManager.cpp
    src/core/HookSettings.cpp
    src/core/FeatureFlags.cpp
    src/core/QueryEngine.cpp
    src/core/ReactiveState.cpp
    src/core/ContentBlockParam.cpp
)

# 工具源文件
set(TOOL_SOURCES
    src/tool/MultimodalProcessor.cpp
    src/tool/ToolRegistry.cpp
    src/tool/FileIndex.cpp
    src/tool/impl/BashTool.cpp
    src/tool/impl/BashSecurity.cpp
    src/tool/impl/FileReadTool.cpp
    src/tool/impl/FileWriteTool.cpp
    src/tool/impl/FileEditTool.cpp
    src/tool/impl/GlobTool.cpp
    src/tool/impl/GrepTool.cpp
    src/tool/impl/WebFetchTool.cpp
    src/tool/impl/WebSearchTool.cpp
    src/tool/impl/AskUserQuestionTool.cpp
    src/tool/impl/TaskTools.cpp
    src/tool/impl/LSPTool.cpp
    src/tool/impl/EnterPlanModeTool.cpp
    src/tool/impl/ExitPlanModeTool.cpp
    src/tool/impl/WorktreeTools.cpp
    src/tool/impl/ExitWorktreeTool.cpp
    src/tool/impl/TodoWriteTool.cpp
    src/tool/impl/AgentTool.cpp
    src/tool/AgentTypes.cpp
    src/tool/impl/NotebookEditTool.cpp
    src/tool/impl/BriefTool.cpp
    src/tool/impl/SkillTool.cpp
    src/tool/impl/ToolSearchTool.cpp
    src/tool/impl/ScheduleCronTool.cpp
    src/tool/impl/ConfigTool.cpp
    src/tool/impl/SendMessageTool.cpp
    src/tool/impl/SleepTool.cpp
    src/tool/impl/MCPTools.cpp
    src/tool/impl/MCPTool.cpp
    src/tool/impl/TaskOutputTool.cpp
    src/tool/impl/MissingTools.cpp
    src/tool/impl/REPLSessionTool.cpp
    src/tool/impl/RagTool.cpp
    src/tool/impl/OntologyTools.cpp
    src/tool/impl/CognitiveTools.cpp
)

# 命令源文件
set(COMMAND_SOURCES
    src/command/CommandRegistry.cpp
    src/command/impl/HelpCommand.cpp
    src/command/impl/CommitCommand.cpp
    src/command/impl/CompactCommand.cpp
    src/command/impl/ConfigCommand.cpp
    src/command/impl/CostCommand.cpp
    src/command/impl/MemoryCommand.cpp
    src/command/impl/ReviewCommand.cpp
    src/command/impl/ClearCommand.cpp
    src/command/impl/InitCommand.cpp
    src/command/impl/DoctorCommand.cpp
    src/command/impl/ResumeCommand.cpp
    src/command/impl/McpCommand.cpp
    src/command/impl/StatusCommand.cpp
    src/command/impl/HistoryCommand.cpp
    src/command/impl/ModelCommand.cpp
    src/command/impl/PermissionsCommand.cpp
    src/command/impl/SkillsCommand.cpp
    src/command/impl/HooksCommand.cpp
    src/command/impl/VimCommand.cpp
    src/command/impl/EffortCommand.cpp
    src/command/impl/ThemeCommand.cpp
    src/command/impl/KeybindingsCommand.cpp
    src/command/impl/LangCommand.cpp
    src/command/impl/StatsCommand.cpp
    src/command/impl/UpgradeCommand.cpp
    src/command/impl/VersionCommand.cpp
    src/command/impl/BugCommand.cpp
    src/command/impl/FeedbackCommand.cpp
    src/command/impl/ExportCommand.cpp
    src/command/impl/SessionCommand.cpp
    src/command/impl/OutputStyleCommand.cpp
    src/command/impl/LoginCommand.cpp
    src/command/impl/PrivacyCommand.cpp
    src/command/impl/BranchCommand.cpp
    src/command/impl/RewindCommand.cpp
    src/command/impl/TagCommand.cpp
    src/command/impl/PlanCommand.cpp
    src/command/impl/LogoutCommand.cpp
    src/command/impl/EnvCommand.cpp
    src/command/impl/FastCommand.cpp
    src/command/impl/DiffCommand.cpp
    src/command/impl/UsageCommand.cpp
    src/command/impl/SummaryCommand.cpp
    src/command/impl/IssueCommand.cpp
    src/command/impl/PrCommentsCommand.cpp
    src/command/impl/SecurityReviewCommand.cpp
    src/command/impl/AutofixPrCommand.cpp
    src/command/impl/ThinkbackCommand.cpp
    src/command/impl/PluginCommand.cpp
    src/command/impl/IdeCommand.cpp
    src/command/impl/VoiceCommand.cpp
    src/command/impl/BridgeCommand.cpp
    src/command/impl/ProactiveCommand.cpp
    src/command/impl/ChromeCommand.cpp
    src/command/impl/MobileCommand.cpp
    src/command/impl/WorkflowsCommand.cpp
    src/command/impl/DebugCommand.cpp
    src/command/impl/DocCommand.cpp
    src/command/impl/CommitPushPrCommand.cpp
    src/command/impl/ContextCommand.cpp
    src/command/impl/FilesCommand.cpp
    src/command/impl/TasksCommand.cpp
    src/command/impl/AgentsCommand.cpp
    src/command/impl/RenameCommand.cpp
    src/command/impl/SwarmCommand.cpp
    src/command/impl/CopyCommand.cpp
    src/command/impl/AddDirCommand.cpp
    src/command/impl/TeleportCommand.cpp
    src/command/impl/AdvisorCommand.cpp
    src/command/impl/BughunterCommand.cpp
    src/command/impl/ReleaseNotesCommand.cpp
    src/command/impl/DesktopCommand.cpp
    src/command/impl/Upgrade2Command.cpp
    src/command/impl/AdditionalCommands.cpp
    src/command/impl/MoreCommands.cpp
    src/command/impl/FinalCommands.cpp
    src/command/impl/FinalTwoCommands.cpp
    src/command/impl/MissingCommands.cpp
    src/command/impl/AdditionalCommands2.cpp
    src/command/impl/RagCommand.cpp
    src/command/impl/OntologyCommand.cpp
)

# 权限源文件
set(PERMISSION_SOURCES
    src/permission/RuleEngine.cpp
    src/permission/PermissionSettings.cpp
    src/permission/PermissionRuleParser.cpp
    src/permission/DangerousPatterns.cpp
    src/permission/DenialTracker.cpp
    src/permission/BashClassifier.cpp
    src/permission/YoloClassifier.cpp
    src/permission/PolicyLimits.cpp
    src/permission/CommandPrefix.cpp
    src/permission/PermissionStore.cpp
)

# 控制台UI源文件
set(CONSOLE_SOURCES
    src/console/AnsiStyle.cpp
    src/console/BannerPrinter.cpp
    src/console/Spinner.cpp
    src/console/StatusLine.cpp
    src/console/MarkdownRenderer.cpp
    src/console/DiffRenderer.cpp
    src/console/ThinkingRenderer.cpp
    src/console/ThemeSystem.cpp
    src/console/Completer.cpp
    src/console/CreativeVerbs.cpp
    src/console/PermissionPromptRenderer.cpp
    src/console/PromptRenderer.cpp
    src/console/TurnDurationRenderer.cpp
    src/console/ActivityDescription.cpp
    src/console/TerminalCapabilities.cpp
)

# 上下文源文件
set(CONTEXT_SOURCES
    src/context/SystemPromptBuilder.cpp
    src/context/GitContext.cpp
    src/context/ClaudeMdLoader.cpp
    src/context/SkillLoader.cpp
    src/context/ContextInjector.cpp
)

# API源文件
set(API_SOURCES
    src/api/ApiClient.cpp
    src/api/AnthropicClient.cpp
    src/api/BedrockClient.cpp
    src/api/OpenAIClient.cpp
    src/api/VertexClient.cpp
    src/api/ApiClientFactory.cpp
    src/api/RetryPolicy.cpp
    src/api/RetryableClient.cpp
    src/api/RateLimitTracker.cpp
    src/api/MessageRepair.cpp
    src/api/ApiDebugTracker.cpp
)

# REPL源文件
set(REPL_SOURCES
    src/repl/ReplSession.cpp
    src/repl/Completer.cpp
)

# MCP源文件
set(MCP_SOURCES
    src/mcp/McpClient.cpp
    src/mcp/McpManager.cpp
    src/mcp/McpTransport.cpp
    src/mcp/McpServer.cpp
)

# 配置源文件
set(CONFIG_SOURCES
    src/config/AppConfig.cpp
    src/config/RagConfig.cpp
    src/config/SettingsManager.cpp
)

# LSP源文件
set(LSP_SOURCES
    src/lsp/LSPClient.cpp
    src/lsp/LspManager.cpp
    src/lsp/LSPServerManager.cpp
)

# Worktree源文件
set(WORKTREE_SOURCES
    src/worktree/WorktreeManager.cpp
)

# Update源文件
set(UPDATE_SOURCES
    src/update/AutoUpdater.cpp
)

# 工具源文件
set(UTILS_SOURCES
    src/utils/Process.cpp
    src/utils/Http.cpp
    src/utils/Json.cpp
    src/utils/SseParser.cpp
    src/utils/ChromeDevTools.cpp
    src/utils/CronScheduler.cpp
    src/utils/TaskWatcher.cpp
    src/utils/OutputStyles.cpp
    src/utils/UserMessage.cpp
    src/utils/ConnectionPool.cpp
    src/utils/I18n.cpp
)

# Swarm源文件
set(SWARM_SOURCES
    src/swarm/SwarmCoordinator.cpp
)

# Skills源文件
set(SKILLS_SOURCES
    src/skills/SkillDiscovery.cpp
)

# 插件源文件
set(PLUGIN_SOURCES
    src/plugin/PluginManager.cpp
    src/plugin/BuiltinPlugins.cpp
)

# Vim源文件
set(VIM_SOURCES
    src/vim/VimMotions.cpp
    src/vim/VimOperators.cpp
    src/vim/VimTextObjects.cpp
    src/vim/VimEngine.cpp
)

# Voice源文件
set(VOICE_SOURCES
    src/voice/VoiceInput.cpp
)

# Stream源文件
set(STREAM_SOURCES
    src/stream/IncrementalBlockParser.cpp
    src/stream/StreamBuffer.cpp
    src/stream/AnswerPostProcessor.cpp
    src/stream/MessagePipeline.cpp
)

# Bridge源文件
set(BRIDGE_SOURCES
    src/bridge/BridgeClient.cpp
    src/bridge/JwtUtils.cpp
    src/bridge/WeComBridge.cpp
    src/bridge/ConfigWizard.cpp
    src/bridge/DynamicConfig.cpp
    src/bridge/ReplBridge.cpp
)

# Computer Use源文件
set(COMPUTER_USE_SOURCES
    src/computer_use/ComputerUse.cpp
)

# Keybindings源文件
set(KEYBINDINGS_SOURCES
    src/keybindings/KeybindingsManager.cpp
)

# Services源文件
set(SERVICES_SOURCES
    src/services/OAuthService.cpp
    src/services/AnalyticsService.cpp
    src/services/TerminalNotifier.cpp
    src/services/CostTracker.cpp
    src/services/RemotePermissionBridge.cpp
    src/services/PermissionUI.cpp
    src/services/SessionMemoryService.cpp
    src/services/MarkdownMemoryService.cpp
    src/core/memory/AutoDream.cpp
    src/core/memory/MemoryExtraction.cpp
    src/core/SideQuery.cpp
    src/core/ConversationFork.cpp
    src/core/PromptSuggestion.cpp
    src/bootstrap/AppState.cpp
    src/bootstrap/MetricsState.cpp
    src/bootstrap/PermissionState.cpp
    src/bootstrap/UIState.cpp
    src/bootstrap/SessionState.cpp
    src/bootstrap/SignalHandler.cpp
    src/bootstrap/AgentRunner.cpp
    src/bootstrap/ReadlineSupport.cpp
    src/services/RemoteSettings.cpp
    src/services/VCRFixture.cpp
    src/services/DiagnosticTracker.cpp
    src/services/PreventSleep.cpp
    src/services/AwaySummary.cpp
    src/services/SessionTitleGenerator.cpp
    src/services/SettingsSync.cpp
    src/services/TeamMemorySync.cpp
    src/services/SyntheticOutput.cpp
    src/services/RemoteTrigger.cpp
    src/services/TipSystem.cpp
    src/services/TelemetryService.cpp
    src/services/NotificationService.cpp
)

# UI源文件
set(UI_SOURCES
    src/ui/UIComponents.cpp
    src/ui/FtxuiApp.cpp
    src/ui/FtxuiRepl.cpp
    src/ui/FtxuiStreaming.cpp
    src/ui/FtxuiPermission.cpp
    src/ui/FtxuiRender.cpp
    src/ui/FtxuiMarkdown.cpp
    src/ui/VirtualScroll.cpp
    src/ui/OffscreenFreeze.cpp
    src/ui/components/StatusBarRenderer.cpp
    src/ui/components/HeaderBar.cpp
    src/ui/components/PermissionPrompt.cpp
    src/ui/components/ModalOverlay.cpp
    src/ui/components/TextInputComponent.cpp
    src/ui/components/StreamingText.cpp
    src/ui/components/ThinkingIndicator.cpp
    src/ui/components/ContentArea.cpp
    src/ui/components/AppLayout.cpp
    src/ui/PromptInputFooter.cpp
    src/ui/renderers/ContentBlockAnsi.cpp
    src/ui/renderers/ContentBlockFtxui.cpp
    src/ui/ToolResultFormatter.cpp
    src/ui/SearchOverlay.cpp
    src/ui/PermissionRendererRegistry.cpp
    src/ui/permissions/DefaultPermissionRenderer.cpp
    src/ui/permissions/BashPermissionRenderer.cpp
    src/ui/permissions/FileEditPermissionRenderer.cpp
    src/ui/permissions/FileWritePermissionRenderer.cpp
    src/ui/permissions/FileReadPermissionRenderer.cpp
)

# RAG源文件
set(RAG_SOURCES
    src/rag/RagClient.cpp
)

# 本体论源文件
set(ONTOLOGY_SOURCES
    src/ontology/Ontology.cpp
    src/ontology/SemanticSearch.cpp
)

# 常量源文件
set(CONSTANTS_SOURCES
    src/constants/Prompts.cpp
)

# 创建主库
add_library(claude_core STATIC
    ${CORE_SOURCES}
    ${TOOL_SOURCES}
    ${COMMAND_SOURCES}
    ${PERMISSION_SOURCES}
    ${CONSOLE_SOURCES}
    ${CONTEXT_SOURCES}
    ${API_SOURCES}
    ${REPL_SOURCES}
    ${MCP_SOURCES}
    ${CONFIG_SOURCES}
    ${UTILS_SOURCES}
    ${LSP_SOURCES}
    ${WORKTREE_SOURCES}
    ${UPDATE_SOURCES}
    ${PLUGIN_SOURCES}
    ${VIM_SOURCES}
    ${VOICE_SOURCES}
    ${STREAM_SOURCES}
    ${BRIDGE_SOURCES}
    ${COMPUTER_USE_SOURCES}
    ${KEYBINDINGS_SOURCES}
    ${SERVICES_SOURCES}
    ${SWARM_SOURCES}
    ${SKILLS_SOURCES}
    ${UI_SOURCES}
    ${RAG_SOURCES}
    ${ONTOLOGY_SOURCES}
    ${CONSTANTS_SOURCES}
)

target_include_directories(claude_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 添加必要的 include 路径
# 注意: 不使用 /usr/local/include 作为 SYSTEM 路径，避免找到旧的 OpenSSL 1.1
target_include_directories(claude_core PUBLIC
    /usr/local/include
)

target_link_libraries(claude_core
    PUBLIC
        spdlog::spdlog
        CLI11::CLI11
        ${APPLE_FRAMEWORKS}
        httplib
)

# FTXUI libraries
if(FTXUI_INCLUDE_DIR)
    find_library(FTXUI_COMPONENT_LIBRARY ftxui-component
        PATHS /usr/local/lib
        NO_DEFAULT_PATH
    )
    find_library(FTXUI_DOM_LIBRARY ftxui-dom
        PATHS /usr/local/lib
        NO_DEFAULT_PATH
    )
    find_library(FTXUI_SCREEN_LIBRARY ftxui-screen
        PATHS /usr/local/lib
        NO_DEFAULT_PATH
    )
    if(FTXUI_COMPONENT_LIBRARY AND FTXUI_DOM_LIBRARY AND FTXUI_SCREEN_LIBRARY)
        target_include_directories(claude_core PRIVATE ${FTXUI_INCLUDE_DIR})
        target_link_libraries(claude_core PRIVATE
            ${FTXUI_COMPONENT_LIBRARY}
            ${FTXUI_DOM_LIBRARY}
            ${FTXUI_SCREEN_LIBRARY}
        )
        message(STATUS "FTXUI libraries linked")
        add_compile_definitions(HAS_FTXUI)
    endif()
endif()

# 行编辑库: 优先使用 GNU readline (更好的 UTF-8 支持), 回退到 editline
if(APPLE)
    # 查找 Homebrew 的 readline
    find_path(READLINE_INCLUDE_DIR readline/readline.h
        PATHS /usr/local/opt/readline/include
        NO_DEFAULT_PATH
    )
    find_library(READLINE_LIBRARY readline
        PATHS /usr/local/opt/readline/lib
        NO_DEFAULT_PATH
    )
    if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        target_include_directories(claude_core PRIVATE ${READLINE_INCLUDE_DIR})
        target_link_libraries(claude_core PRIVATE ${READLINE_LIBRARY})
        message(STATUS "Using GNU readline: ${READLINE_LIBRARY}")
    else()
        # 回退到系统 editline
        find_library(EDITLINE_LIBRARY edit)
        if(EDITLINE_LIBRARY)
            target_link_libraries(claude_core PRIVATE ${EDITLINE_LIBRARY})
            message(STATUS "Using editline: ${EDITLINE_LIBRARY}")
        endif()
    endif()
else()
    find_package(Readline QUIET)
    if(Readline_FOUND)
        target_link_libraries(claude_core PRIVATE Readline::Readline)
        message(STATUS "Found readline: ${Readline_ROOT_DIR}")
    else()
        find_library(READLINE_LIBRARY readline)
        if(READLINE_LIBRARY)
            target_link_libraries(claude_core PRIVATE ${READLINE_LIBRARY})
            message(STATUS "Found readline: ${READLINE_LIBRARY}")
        endif()
    endif()
endif()

# 编译定义
target_compile_definitions(claude_core
    PRIVATE
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO
)

# ========== CLI 可执行文件 ==========

# 可自定义 CLI 名称 (如: cmake -DCLI_NAME=mm ..)
set(CLI_NAME "claude-cli" CACHE STRING "CLI executable name")

add_executable(${CLI_NAME} src/main.cpp)
target_link_libraries(${CLI_NAME} PRIVATE claude_core)

# ========== 安装 ==========

install(TARGETS claude_core ${CLI_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/claude DESTINATION include)

# 输出提示
message(STATUS "CLI executable: ${CLI_NAME}")
message(STATUS "Build with custom name: cmake -DCLI_NAME=mm -B build")

# ========== 测试 ==========

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
