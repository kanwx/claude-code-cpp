#include <claude/repl/ReplSession.hpp>
#include <claude/command/PromptCommand.hpp>
#include <claude/console/BannerPrinter.hpp>
#include <claude/console/Spinner.hpp>
#include <claude/console/MarkdownRenderer.hpp>
#include <claude/permission/PermissionTypes.hpp>
#include <claude/utils/I18n.hpp>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <cctype>
#include <clocale>

// 行编辑支持: 优先使用 GNU readline (更好的 UTF-8 支持), 回退到 editline
#if __has_include(<readline/readline.h>)
    #include <readline/readline.h>
    #include <readline/history.h>
    #define USE_READLINE 1
    #define USE_GNU_READLINE 1
#elif defined(__APPLE__)
    #include <editline/readline.h>
    #include <histedit.h>
    #define USE_READLINE 1
    #define USE_EDITLINE 1
#endif

namespace claude {

namespace console {
    void printBanner();
}

ReplSession::ReplSession(
    AgentLoop& agentLoop,
    ToolRegistry& tools,
    CommandRegistry& commands
) : agentLoop_(agentLoop), tools_(tools), commands_(commands) {}

void ReplSession::run() {
    console::printBanner();

    std::cout << "\nType /help for available commands.\n";
    std::cout << "Press Ctrl+C to exit.\n\n";

    agentLoop_.setOnPermissionRequest([this](const PermissionRequest& req) {
        return promptPermission(req);
    });

    agentLoop_.setOnToolEvent([](const ToolEvent& event) {
        if (event.phase == ToolEventPhase::Start) {
            std::cout << "\n\033[90m▶ " << event.toolName << "...\033[0m" << std::flush;
        } else {
            if (event.error) {
                std::cout << " \033[31m✗\033[0m\n";
            } else {
                std::cout << " \033[32m✓\033[0m\n";
            }
        }
    });

    agentLoop_.setOnStreamStart([]() {
        std::cout << "\n";
    });

    // 设置 locale 支持 UTF-8 (与测试程序完全一致)
    setlocale(LC_CTYPE, "en_US.UTF-8");
    setlocale(LC_ALL, "en_US.UTF-8");

#ifdef USE_GNU_READLINE
    // GNU readline 配置 (与工作的测试程序完全一致)
    rl_variable_bind("input-meta", "on");
    rl_variable_bind("output-meta", "on");
    rl_variable_bind("convert-meta", "off");
    rl_reset_terminal(nullptr);
#endif

    while (running_) {
        String input;

#if USE_READLINE
        // 构建提示符 (像测试程序那样用 snprintf)
        char prompt[128];
        if (provider_.empty()) {
            snprintf(prompt, sizeof(prompt), "\001\033[1;36m\002claude\001\033[0m\002> ");
        } else {
            snprintf(prompt, sizeof(prompt), "\001\033[1;36m\002claude\001\033[0m\002 \001\033[90m\002[%s/%s]\001\033[0m\002> ",
                     provider_.c_str(), model_.c_str());
        }

        char* line = readline(prompt);
        if (!line) {
            // EOF (Ctrl+D)
            break;
        }
        input = line;
        free(line);

        // 添加到历史（非空行）
        if (!input.empty() && input.find_first_not_of(" \t\n\r") != String::npos) {
            add_history(input.c_str());
        }
#else
        // 回退到基础 getline
        std::cout << prompt << std::flush;
        if (!std::getline(std::cin, input)) {
            break;
        }
#endif

        if (input.empty() || input.find_first_not_of(" \t\n\r") == String::npos) {
            continue;
        }

        handleInput(input);
    }

    std::cout << "\nGoodbye!\n";
}

void ReplSession::setProviderInfo(const String& provider, const String& model) {
    provider_ = provider;
    model_ = model;
}

void ReplSession::handleInput(const String& input) {
    if (!input.empty() && input[0] == '/') {
        if (handleSlashCommand(input)) {
            return;
        }
    }

    try {
        Spinner spinner;
        spinner.start(tr("status.thinking"));

        auto result = agentLoop_.runStreaming(input, [](const String& token) {
            std::cout << token << std::flush;
        });

        spinner.stop();

        if (!result) {
            std::cout << "\n\033[31mError: " << result.error() << "\033[0m\n";
        }

        std::cout << "\n";

    } catch (const std::exception& e) {
        std::cout << "\n\033[31mException: " << e.what() << "\033[0m\n";
    }
}

bool ReplSession::handleSlashCommand(const String& input) {
    String cmd = input;
    String args;

    auto spacePos = input.find(' ');
    if (spacePos != String::npos) {
        cmd = input.substr(0, spacePos);
        args = input.substr(spacePos + 1);
    }

    cmd = cmd.substr(1);

    if (cmd == "exit" || cmd == "quit") {
        running_ = false;
        return true;
    }

    if (cmd == "clear") {
        agentLoop_.reset();
        std::cout << "\033[2J\033[H";
        console::printBanner();
        return true;
    }

    auto* command = commands_.findByName(cmd);
    if (!command) {
        std::cout << "\033[31mUnknown command: /" << cmd << "\033[0m\n";
        std::cout << "Type /help for available commands.\n";
        return true;
    }

    try {
        auto context = CommandContext::create(agentLoop_, tools_, std::filesystem::current_path());

        // Check if this is a PromptCommand (delegates to AI model)
        if (command->commandType() == CommandType::Prompt) {
            auto* promptCmd = static_cast<PromptCommand*>(command);
            String prompt = promptCmd->buildPrompt(args, context);

            if (prompt.empty()) {
                return true;
            }

            // Inject the prompt as a user message and let the AI process it
            try {
                Spinner spinner;
                spinner.start("Processing command...");

                auto result = agentLoop_.runStreaming(prompt, [](const String& token) {
                    std::cout << token << std::flush;
                });

                spinner.stop();

                if (!result) {
                    std::cout << "\n\033[31mError: " << result.error() << "\033[0m\n";
                }

                std::cout << "\n";
            } catch (const std::exception& e) {
                std::cout << "\n\033[31mException: " << e.what() << "\033[0m\n";
            }
            return true;
        }

        // Local command: execute directly
        String output = command->execute(args, context);

        if (!output.empty()) {
            std::cout << output << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "\033[31mError: " << e.what() << "\033[0m\n";
    }

    return true;
}

PermissionChoice ReplSession::promptPermission(const PermissionRequest& req) {
    std::cout << "\n\033[33m⚠ Permission Required:\033[0m\n";
    std::cout << "  Tool: " << req.toolName << "\n";
    std::cout << "  " << req.activityDescription << "\n\n";

    while (true) {
        std::cout << "Allow? [y/n/always/never]: " << std::flush;

        String response;
        if (!std::getline(std::cin, response)) {
            return PermissionChoice::DenyOnce;
        }

        for (auto& c : response) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (response == "y" || response == "yes") {
            return PermissionChoice::AllowOnce;
        }
        if (response == "n" || response == "no") {
            return PermissionChoice::DenyOnce;
        }
        if (response == "always") {
            return PermissionChoice::AlwaysAllow;
        }
        if (response == "never") {
            return PermissionChoice::AlwaysDeny;
        }

        std::cout << "Invalid choice. Please enter y, n, always, or never.\n";
    }
}

} // namespace claude
