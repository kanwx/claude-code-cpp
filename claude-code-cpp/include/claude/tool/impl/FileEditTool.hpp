#pragma once

#include "../Tool.hpp"

namespace claude {

/// 文件编辑工具 —— 精确字符串替换
class FileEditTool : public Tool {
public:
    String name() const override { return "Edit"; }

    String description() const override {
        return "Perform exact string replacements in files. "
               "Use this to modify existing files by replacing specific strings.";
    }

    String inputSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "file_path": {
                    "type": "string",
                    "description": "The absolute path to the file to edit"
                },
                "old_string": {
                    "type": "string",
                    "description": "The text to replace"
                },
                "new_string": {
                    "type": "string",
                    "description": "The text to replace it with"
                },
                "replace_all": {
                    "type": "boolean",
                    "description": "Replace all occurrences"
                }
            },
            "required": ["file_path", "old_string", "new_string"]
        })";
    }

    String execute(const Json& input, ToolContext& context) override;

    PermissionResult checkPermission(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return false; }

    String activityDescription(const Json& input) const override {
        return "✂️ Edit " + input.value("file_path", "");
    }

    bool alwaysLoad() const override { return true; }
};

} // namespace claude
