#pragma once

#include "../Tool.hpp"

namespace claude {

/// 文件写入工具
class FileWriteTool : public Tool {
public:
    String name() const override { return "Write"; }

    String description() const override {
        return "Write a file to the local filesystem. "
               "Creates the file if it doesn't exist, overwrites if it does.";
    }

    String inputSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "file_path": {
                    "type": "string",
                    "description": "The absolute path to the file to write"
                },
                "content": {
                    "type": "string",
                    "description": "The content to write to the file"
                }
            },
            "required": ["file_path", "content"]
        })";
    }

    String execute(const Json& input, ToolContext& context) override;

    PermissionResult checkPermission(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return false; }

    String activityDescription(const Json& input) const override {
        return "✏️ Write " + input.value("file_path", "");
    }

    bool alwaysLoad() const override { return true; }

    String renderToolResult(const String& result, bool isError,
                            bool isCancelled, bool isRejected) const override;
};

} // namespace claude
