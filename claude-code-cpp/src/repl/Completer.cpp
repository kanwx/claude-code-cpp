#include <claude/repl/Completer.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace claude {

void Completer::addCandidate(const String& candidate) {
    // 避免重复
    if (std::find(candidates_.begin(), candidates_.end(), candidate) == candidates_.end()) {
        candidates_.push_back(candidate);
    }
}

void Completer::addCommand(const String& cmd) {
    // 命令以 / 开头
    String fullCmd = cmd[0] == '/' ? cmd : "/" + cmd;
    addCandidate(fullCmd);
}

void Completer::addTool(const String& tool) {
    if (std::find(tools_.begin(), tools_.end(), tool) == tools_.end()) {
        tools_.push_back(tool);
    }
}

void Completer::addHistory(const String& cmd) {
    // 避免重复的连续历史
    if (!history_.empty() && history_.front() == cmd) {
        return;
    }

    // 如果已存在，移到前面
    auto it = std::find(history_.begin(), history_.end(), cmd);
    if (it != history_.end()) {
        history_.erase(it);
    }

    history_.push_front(cmd);

    // 限制大小
    while (history_.size() > maxHistorySize_) {
        history_.pop_back();
    }
}

std::vector<String> Completer::complete(const String& prefix) const {
    std::vector<String> matches;

    // 空前缀返回所有候选
    if (prefix.empty()) {
        return candidates_;
    }

    // 前缀匹配
    for (const auto& candidate : candidates_) {
        if (candidate.size() >= prefix.size() &&
            candidate.substr(0, prefix.size()) == prefix) {
            matches.push_back(candidate);
        }
    }

    // 按字母排序
    std::sort(matches.begin(), matches.end());

    return matches;
}

bool Completer::isCommandContext(const String& input) const {
    // 输入以 / 开头
    return !input.empty() && input[0] == '/';
}

bool Completer::isFilePathContext(const String& input) const {
    // 检测文件路径模式
    // 例如: "read " 或 "edit " 后面跟着路径
    std::istringstream iss(input);
    String firstWord;
    iss >> firstWord;

    // 工具调用后的参数通常是文件路径
    static const std::vector<String> pathTools = {
        "read", "write", "edit", "glob", "grep",
        "Read", "Write", "Edit", "Glob", "Grep"
    };

    if (std::find(pathTools.begin(), pathTools.end(), firstWord) != pathTools.end()) {
        return true;
    }

    // 或包含路径特征
    if (input.find('/') != String::npos || input.find('\\') != String::npos) {
        return true;
    }

    // 或以 ./ 或 ../ 或 ~ 开头的参数
    String rest;
    std::getline(iss, rest);
    if (!rest.empty()) {
        size_t start = rest.find_first_not_of(" ");
        if (start != String::npos) {
            String arg = rest.substr(start);
            if (arg.substr(0, 2) == "./" || arg.substr(0, 3) == "../" || arg[0] == '~') {
                return true;
            }
        }
    }

    return false;
}

CompletionResult Completer::smartComplete(const String& input, const std::filesystem::path& workDir) const {
    CompletionResult result;
    result.type = CompletionType::Command;

    if (input.empty()) {
        result.matches = candidates_;
        return result;
    }

    // 命令补全
    if (isCommandContext(input)) {
        result.matches = complete(input);
        result.type = CompletionType::Command;

        // 计算共同前缀
        if (result.matches.size() == 1) {
            result.commonPrefix = result.matches[0];
        } else if (result.matches.size() > 1) {
            result.commonPrefix = result.matches[0];
            for (size_t i = 1; i < result.matches.size(); ++i) {
                size_t j = 0;
                while (j < result.commonPrefix.size() &&
                       j < result.matches[i].size() &&
                       result.commonPrefix[j] == result.matches[i][j]) {
                    ++j;
                }
                result.commonPrefix = result.commonPrefix.substr(0, j);
            }
        }
        return result;
    }

    // 文件路径补全
    if (isFilePathContext(input)) {
        // 提取路径部分
        std::istringstream iss(input);
        String firstWord;
        iss >> firstWord;

        String pathPart;
        std::getline(iss, pathPart);
        size_t start = pathPart.find_first_not_of(" ");
        if (start != String::npos) {
            pathPart = pathPart.substr(start);
        } else {
            pathPart = "";
        }

        result.matches = completeFilePath(pathPart, workDir);
        result.type = CompletionType::FilePath;

        // 保留前面的工具名
        if (!result.matches.empty() && !firstWord.empty()) {
            for (auto& match : result.matches) {
                match = firstWord + " " + match;
            }
        }
        return result;
    }

    // 历史搜索
    result.matches = searchHistory(input);
    if (!result.matches.empty()) {
        result.type = CompletionType::History;
        return result;
    }

    // 默认返回命令匹配
    result.matches = complete(input);
    return result;
}

std::vector<String> Completer::searchHistory(const String& prefix) const {
    std::vector<String> matches;

    for (const auto& cmd : history_) {
        // 子串匹配
        if (cmd.find(prefix) != String::npos) {
            matches.push_back(cmd);
        }
    }

    return matches;
}

// ========== 辅助函数 ==========

/// 创建默认补全器
Completer createDefaultCompleter(const std::vector<String>& commands) {
    Completer completer;

    // 添加命令
    for (const auto& cmd : commands) {
        completer.addCommand(cmd);
    }

    // 添加常用命令
    completer.addCommand("help");
    completer.addCommand("exit");
    completer.addCommand("quit");
    completer.addCommand("clear");
    completer.addCommand("compact");
    completer.addCommand("config");
    completer.addCommand("memory");
    completer.addCommand("review");
    completer.addCommand("commit");
    completer.addCommand("model");
    completer.addCommand("init");
    completer.addCommand("doctor");
    completer.addCommand("status");
    completer.addCommand("login");
    completer.addCommand("logout");
    completer.addCommand("branch");
    completer.addCommand("diff");
    completer.addCommand("issue");
    completer.addCommand("pr-comments");
    completer.addCommand("permissions");
    completer.addCommand("hooks");
    completer.addCommand("theme");
    completer.addCommand("vim");
    completer.addCommand("plan");
    completer.addCommand("thinkback");
    completer.addCommand("usage");
    completer.addCommand("summary");
    completer.addCommand("export");
    completer.addCommand("resume");
    completer.addCommand("history");
    completer.addCommand("mcp");
    completer.addCommand("skills");
    completer.addCommand("upgrade");
    completer.addCommand("lang");

    // 添加工具名称
    completer.addTool("Read");
    completer.addTool("Write");
    completer.addTool("Edit");
    completer.addTool("Bash");
    completer.addTool("Glob");
    completer.addTool("Grep");
    completer.addTool("WebFetch");
    completer.addTool("WebSearch");
    completer.addTool("LSP");
    completer.addTool("TaskCreate");
    completer.addTool("TaskUpdate");
    completer.addTool("TaskList");

    return completer;
}

/// 文件路径补全
std::vector<String> completeFilePath(const String& prefix, const std::filesystem::path& baseDir) {
    std::vector<String> matches;

    if (prefix.empty()) {
        return matches;
    }

    try {
        std::filesystem::path searchDir = baseDir;
        String searchPrefix = prefix;

        // 处理 ~ 展开
        String expandedPrefix = prefix;
        if (prefix[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home) {
                expandedPrefix = String(home) + prefix.substr(1);
            }
        }

        // 解析目录部分
        auto lastSlash = expandedPrefix.find_last_of("/\\");
        if (lastSlash != String::npos) {
            searchDir = expandedPrefix.substr(0, lastSlash);
            searchPrefix = expandedPrefix.substr(lastSlash + 1);
        } else {
            searchDir = baseDir;
            searchPrefix = expandedPrefix;
        }

        if (!std::filesystem::exists(searchDir)) {
            return matches;
        }

        for (const auto& entry : std::filesystem::directory_iterator(searchDir)) {
            String name = entry.path().filename().string();

            // 隐藏文件检查
            if (!searchPrefix.empty() && searchPrefix[0] != '.' && name[0] == '.') {
                continue;
            }

            if (name.size() >= searchPrefix.size() &&
                name.substr(0, searchPrefix.size()) == searchPrefix) {
                String match;
                if (lastSlash != String::npos) {
                    match = prefix.substr(0, lastSlash + 1) + name;
                } else {
                    match = name;
                }
                if (entry.is_directory()) {
                    match += "/";
                }
                matches.push_back(match);
            }
        }

        // 排序：目录优先，然后按字母
        std::sort(matches.begin(), matches.end(), [](const String& a, const String& b) {
            bool aDir = !a.empty() && a.back() == '/';
            bool bDir = !b.empty() && b.back() == '/';
            if (aDir != bDir) return aDir;
            return a < b;
        });

    } catch (...) {
        // 忽略权限错误等
    }

    return matches;
}

/// 参数补全
std::vector<String> completeArgument(const String& command, const String& argPrefix) {
    std::vector<String> matches;

    // 各命令的特定参数
    static const std::unordered_map<String, std::vector<String>> commandArgs = {
        {"/model", {"gpt-4o", "gpt-4", "gpt-3.5-turbo", "claude-3-opus", "claude-3-sonnet", "claude-3-haiku"}},
        {"/theme", {"dark", "light", "solarized", "monokai", "nord"}},
        {"/lang", {"en", "zh", "ja", "ko", "auto"}},
        {"/permissions", {"default", "acceptEdits", "bypassPermissions", "plan"}},
        {"/vim", {"on", "off", "status"}},
        {"/output-style", {"default", "streaming", "compact"}},
        {"/effort", {"low", "medium", "high"}},
        {"/branch", {"create", "switch", "delete", "list"}},
        {"/git", {"status", "diff", "log", "add", "commit", "push", "pull"}}
    };

    auto it = commandArgs.find(command);
    if (it != commandArgs.end()) {
        for (const auto& arg : it->second) {
            if (arg.size() >= argPrefix.size() &&
                arg.substr(0, argPrefix.size()) == argPrefix) {
                matches.push_back(arg);
            }
        }
    }

    return matches;
}

// ========== Fuzzy matching ==========

std::vector<std::pair<String, int>> Completer::fuzzyFilter(
    const std::vector<String>& candidates, const String& query) {
    std::vector<std::pair<String, int>> results;

    if (query.empty()) {
        for (const auto& c : candidates) {
            results.emplace_back(c, 0);
        }
        return results;
    }

    for (const auto& candidate : candidates) {
        // Walk through candidate checking if all query chars appear in order
        size_t ci = 0;  // candidate index
        size_t qi = 0;  // query index
        int score = 0;
        int lastMatchPos = -1;

        while (ci < candidate.size() && qi < query.size()) {
            if (std::tolower(static_cast<unsigned char>(candidate[ci])) ==
                std::tolower(static_cast<unsigned char>(query[qi]))) {
                // Position gap scoring: consecutive matches score higher
                int gap = (lastMatchPos < 0) ? 0 : static_cast<int>(ci) - lastMatchPos - 1;
                score += 1000 / (gap + 1);  // consecutive = 1000, gap of 1 = 500, etc.
                lastMatchPos = static_cast<int>(ci);
                qi++;
            }
            ci++;
        }

        // Only include if all query characters matched
        if (qi == query.size()) {
            results.emplace_back(candidate, score);
        }
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    return results;
}

// ========== Tab completion support ==========

String Completer::commonPrefix(const String& input) const {
    if (lastCompletions_.empty()) return input;

    if (lastCompletions_.size() == 1) {
        return lastCompletions_[0];
    }

    // Find common prefix among all completions
    String prefix = lastCompletions_[0];
    for (size_t i = 1; i < lastCompletions_.size(); ++i) {
        size_t j = 0;
        while (j < prefix.size() && j < lastCompletions_[i].size() &&
               prefix[j] == lastCompletions_[i][j]) {
            ++j;
        }
        prefix = prefix.substr(0, j);
    }
    return prefix;
}

void Completer::updateCompletions(const String& input, size_t cursorPos) {
    lastCompletions_.clear();

    if (input.empty() || cursorPos < input.size()) {
        // Don't complete in the middle of text or on empty input
        return;
    }

    // Use smartComplete to get context-aware results
    auto result = smartComplete(input, workDir_);

    if (result.matches.empty()) {
        // Fall back to fuzzy matching across all candidates + tools
        std::vector<String> allCandidates = candidates_;
        for (const auto& t : tools_) {
            allCandidates.push_back(t);
        }
        auto fuzzy = fuzzyFilter(allCandidates, input);
        for (const auto& [match, score] : fuzzy) {
            if (score > 0) {
                lastCompletions_.push_back(match);
            }
        }
    } else {
        // Use prefix matches from smartComplete, then augment with fuzzy
        lastCompletions_ = result.matches;

        // Add fuzzy matches that aren't already in the list
        std::vector<String> allCandidates = candidates_;
        for (const auto& t : tools_) {
            allCandidates.push_back(t);
        }
        auto fuzzy = fuzzyFilter(allCandidates, input);
        for (const auto& [match, score] : fuzzy) {
            if (score > 0 &&
                std::find(lastCompletions_.begin(), lastCompletions_.end(), match) == lastCompletions_.end()) {
                lastCompletions_.push_back(match);
            }
        }
    }

    // Limit to a reasonable number for display
    if (lastCompletions_.size() > 20) {
        lastCompletions_.resize(20);
    }
}

} // namespace claude
