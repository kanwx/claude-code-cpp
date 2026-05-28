// Readline support + session persistence functions.
// saveSession() is always compiled (used by both readline and non-readline exit paths).
// All readline-specific functions are guarded by #ifdef USE_READLINE.

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

#include <claude/core/AgentLoop.hpp>
#include <claude/core/Types.hpp>
#include <spdlog/spdlog.h>

// ========== Session persistence (always compiled) ==========

namespace claude {
namespace session {

void saveSession(AgentLoop* loop) {
    if (!loop) return;

    const char* home = std::getenv("HOME");
    if (!home) return;

    auto sessionDir = std::filesystem::path(home) / ".claude" / "sessions";
    std::filesystem::create_directories(sessionDir);

    // Generate session filename from timestamp
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream nameStream;
    nameStream << "session_" << std::put_time(std::localtime(&tt), "%Y%m%d_%H%M%S");
    auto sessionFile = sessionDir / (nameStream.str() + ".json");

    const auto& messages = loop->getMessageHistory();
    if (messages.empty()) return; // Don't save empty sessions

    // Serialize messages to JSON
    Json sessionJson = Json::object();
    sessionJson["version"] = 1;
    sessionJson["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    Json msgArray = Json::array();
    for (const auto& msg : messages) {
        Json jm;
        switch (msg.role) {
            case MessageRole::System:     jm["role"] = "system"; break;
            case MessageRole::Assistant:  jm["role"] = "assistant"; break;
            case MessageRole::ToolResult: jm["role"] = "tool"; break;
            default:                      jm["role"] = "user"; break;
        }

        if (!msg.content.empty()) {
            jm["content"] = msg.content;
        }

        if (!msg.toolCalls.empty()) {
            Json tcs = Json::array();
            for (const auto& tc : msg.toolCalls) {
                Json tcj;
                tcj["id"] = tc.id;
                tcj["function"] = {{"name", tc.name}, {"arguments", tc.arguments}};
                tcs.push_back(tcj);
            }
            jm["tool_calls"] = tcs;
        }

        if (!msg.toolResults.empty()) {
            Json trs = Json::array();
            for (const auto& tr : msg.toolResults) {
                trs.push_back({
                    {"tool_call_id", tr.callId},
                    {"name", tr.toolName},
                    {"content", tr.content},
                    {"is_error", tr.isError}
                });
            }
            jm["tool_results"] = trs;
        }

        if (msg.thinking) {
            jm["thinking"] = *msg.thinking;
        }

        msgArray.push_back(jm);
    }
    sessionJson["messages"] = msgArray;

    // Keep only the 50 most recent sessions
    std::vector<std::filesystem::path> sessions;
    for (const auto& entry : std::filesystem::directory_iterator(sessionDir)) {
        if (entry.path().extension() == ".json") {
            sessions.push_back(entry.path());
        }
    }
    if (sessions.size() >= 50) {
        std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) {
            return std::filesystem::last_write_time(a) < std::filesystem::last_write_time(b);
        });
        for (size_t i = 0; i < sessions.size() - 49; i++) {
            std::filesystem::remove(sessions[i]);
        }
    }

    std::ofstream file(sessionFile);
    if (file) {
        file << sessionJson.dump(2);
        spdlog::info("Session saved to {}", sessionFile.string());
    }
}

} // namespace session
} // namespace claude

// ========== Readline-specific functions (guarded) ==========

#if __has_include(<readline/readline.h>)
#define USE_READLINE 1
#endif

#ifdef USE_READLINE

#include <sys/ioctl.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <claude/command/CommandRegistry.hpp>
#include <claude/console/Completer.hpp>
#include <claude/bootstrap/SignalHandler.hpp>

using namespace claude;

// Globals for readline — accessed by completion functions and main.cpp
CommandRegistry* g_commandRegistry = nullptr;
Completer* g_completer = nullptr;
int g_lastHintLines = 0;

// 记录上次显示的行数，用于正确清除
static int g_lastDisplayLines = 1;

// 历史文件路径
static std::filesystem::path getHistoryFilePath() {
    const char* home = std::getenv("HOME");
    if (!home) return {};
    auto path = std::filesystem::path(home) / ".claude";
    std::filesystem::create_directories(path);
    return path / "history.txt";
}

// 加载历史
void loadHistory() {
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
void saveHistory(const String& entry) {
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
            width += 1;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            width += 2;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            width += 2;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
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
    std::cout << "\r";
    for (int i = 1; i < linesToClear; i++) {
        std::cout << "\033[A";
    }
    std::cout << "\r";

    // Clear all lines from top to bottom
    for (int i = 0; i < linesToClear; i++) {
        std::cout << "\033[2K";
        if (i < linesToClear - 1) {
            std::cout << "\033[B";
        }
    }

    // Move back to top line
    for (int i = 1; i < linesToClear; i++) {
        std::cout << "\033[A";
    }
    std::cout << "\r";

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
        std::cout << "\033[" << count << "A";
    }

    // 计算光标位置（考虑多行）
    int cursorWidth = promptWidth + displayWidth(rl_line_buffer, rl_point);
    int cursorLine = cursorWidth / termWidth;
    int cursorCol = cursorWidth % termWidth;
    if (cursorCol == 0 && cursorWidth > 0) {
        cursorCol = termWidth;
        cursorLine--;
    }

    // 移到正确位置
    std::cout << "\r";
    if (inputLines > 1 && cursorLine < inputLines - 1) {
        std::cout << "\033[" << (inputLines - 1 - cursorLine) << "A";
    }
    if (cursorCol > 0) {
        std::cout << "\033[" << cursorCol << "C";
    }

    std::cout << std::flush;
}

// Public interface — called from main.cpp's runRepl()

namespace claude {
namespace readline_support {

void initReadline(CommandRegistry* registry, Completer* completer) {
    // GNU readline 配置
    rl_variable_bind("input-meta", "on");
    rl_variable_bind("output-meta", "on");
    rl_variable_bind("convert-meta", "off");
    rl_reset_terminal(nullptr);

    g_commandRegistry = registry;
    g_completer = completer;

    rl_attempted_completion_function = commandCompletion;
    rl_redisplay_function = hintRedisplay;

    loadHistory();
}

} // namespace readline_support
} // namespace claude

#endif // USE_READLINE
