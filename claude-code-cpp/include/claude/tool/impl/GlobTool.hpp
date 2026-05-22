#pragma once

#include "../Tool.hpp"
#include <filesystem>

namespace claude {

/// Glob 文件匹配工具
class GlobTool : public Tool {
public:
    String name() const override { return "Glob"; }

    String description() const override {
        return "Find files matching a glob pattern. "
               "Fast file pattern matching that works with any codebase size.";
    }

    String inputSchema() const override {
        return "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\",\"description\":\"The glob pattern to match files against\"},\"path\":{\"type\":\"string\",\"description\":\"The directory to search in\"}},\"required\":[\"pattern\"]}";
    }

    String execute(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return true; }

    size_t maxResultSizeChars() const override { return 30000; }

    String activityDescription(const Json& input) const override {
        return "Glob " + input.value("pattern", "");
    }

    bool alwaysLoad() const override { return true; }

    bool isCollapsible() const override { return true; }
    bool isSearchTool() const override { return true; }
    String renderToolResult(const String& result, bool isError,
                            bool isCancelled, bool isRejected) const override;

private:
    /// 匹配简单 glob 模式
    bool matchesGlob(const String& name, const String& pattern);

    /// 匹配路径模式 (包含 / 或 **)
    std::vector<std::filesystem::path> matchPathPattern(
        const std::filesystem::path& basePath, const String& pattern);

    /// 将 glob 模式转换为正则表达式
    String globToRegex(const String& pattern);
};

} // namespace claude
