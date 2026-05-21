#pragma once


#include "../core/Types.hpp"

#include <vector>
#include <deque>
#include <filesystem>

namespace claude {

/// 补全类型
enum class CompletionType {
    Command,      // 斜杠命令
    FilePath,     // 文件路径
    ToolName,     // 工具名称
    History,      // 历史命令
    Argument      // 参数
};

/// 补全结果
struct CompletionResult {
    std::vector<String> matches;
    CompletionType type;
    String commonPrefix;  // 共同前缀（用于自动补全）
};

/// Tab 补全器
class Completer {
public:
    /// 添加候选词
    void addCandidate(const String& candidate);

    /// 添加命令
    void addCommand(const String& cmd);

    /// 添加工具名称
    void addTool(const String& tool);

    /// 添加历史命令
    void addHistory(const String& cmd);

    /// 补全
    std::vector<String> complete(const String& prefix) const;

    /// 智能补全（根据上下文）
    CompletionResult smartComplete(const String& input, const std::filesystem::path& workDir) const;

    /// 获取历史命令（搜索）
    std::vector<String> searchHistory(const String& prefix) const;

    /// 设置最大历史大小
    void setMaxHistorySize(size_t size) { maxHistorySize_ = size; }

    /// 设置工作目录（用于文件路径补全）
    void setWorkDir(const std::filesystem::path& dir) { workDir_ = dir; }

    /// Fuzzy match: returns candidates where all query chars appear in order.
    /// Scored by character proximity (consecutive matches score higher).
    static std::vector<std::pair<String, int>> fuzzyFilter(
        const std::vector<String>& candidates, const String& query);

    /// Get the common prefix of current completions after the typed portion
    String commonPrefix(const String& input) const;

    /// Get current completions for display
    const std::vector<String>& currentCompletions() const { return lastCompletions_; }

    /// Update completions for current input
    void updateCompletions(const String& input, size_t cursorPos);

    /// Clear current completions (e.g. on Escape)
    void clearCompletions() { lastCompletions_.clear(); }

private:
    std::vector<String> candidates_;
    std::vector<String> tools_;
    std::deque<String> history_;
    size_t maxHistorySize_ = 100;
    std::vector<String> lastCompletions_;
    std::filesystem::path workDir_;

    // 判断是否是文件路径上下文
    bool isFilePathContext(const String& input) const;

    // 判断是否是命令上下文
    bool isCommandContext(const String& input) const;
};

/// 创建默认补全器
Completer createDefaultCompleter(const std::vector<String>& commands);

/// 文件路径补全
std::vector<String> completeFilePath(const String& prefix, const std::filesystem::path& baseDir);

/// 参数补全（针对特定命令）
std::vector<String> completeArgument(const String& command, const String& argPrefix);

} // namespace claude
