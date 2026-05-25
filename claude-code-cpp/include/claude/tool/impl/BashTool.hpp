#pragma once

#include "../Tool.hpp"
#include "BashSecurity.hpp"

namespace claude {

/// Bash 工具 —— 执行 shell 命令 (带完整安全子系统)
class BashTool : public Tool {
public:
    String name() const override { return "Bash"; }

    String description() const override {
        return "Execute a bash command in the working directory. "
               "Commands are classified for safety: read-only commands auto-approve, "
               "destructive commands require confirmation. "
               "Sandbox-aware with injection detection and git safety checks.";
    }

    String inputSchema() const override {
        return "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\","
               "\"description\":\"The shell command to execute\"},"
               "\"timeout\":{\"type\":\"integer\",\"description\":\"Timeout in seconds (default: 120, max: 600)\"},"
               "\"description\":{\"type\":\"string\",\"description\":\"Brief description of what the command does\"}"
               "},\"required\":[\"command\"]}";
    }

    String execute(const Json& input, ToolContext& context) override;

    String executeStreaming(const Json& input, ToolContext& context,
                           ChunkCallback onChunk) override;
    bool supportsStreaming() const override { return true; }

    PermissionResult checkPermission(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override;

    bool isConcurrencySafe(const Json& input) const override;

    bool isDestructive(const Json& input) const override;

    size_t maxResultSizeChars() const override { return 50000; }

    String activityDescription(const Json& input) const override;

    bool alwaysLoad() const override { return true; }

    String renderToolResult(const String& result, bool isError,
                            bool isCancelled, bool isRejected) const override;

    /// Get last preflight result (for permission UI formatting)
    const std::optional<bash_security::PreflightResult>& lastPreflight() const {
        return lastPreflight_;
    }

private:
    mutable std::optional<bash_security::PreflightResult> lastPreflight_;
};

} // namespace claude
