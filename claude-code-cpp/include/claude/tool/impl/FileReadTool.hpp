#pragma once

#include "../Tool.hpp"

namespace claude {

/// 文件读取工具
class FileReadTool : public Tool {
public:
    String name() const override { return "Read"; }

    String description() const override {
        return "Read a file from the local filesystem. "
               "Supports text files, images, and PDFs.";
    }

    String inputSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "file_path": {
                    "type": "string",
                    "description": "The absolute path to the file to read"
                },
                "offset": {
                    "type": "integer",
                    "description": "Line number to start reading from"
                },
                "limit": {
                    "type": "integer",
                    "description": "Number of lines to read"
                }
            },
            "required": ["file_path"]
        })";
    }

    String execute(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe(const Json&) const override { return true; }

    size_t maxResultSizeChars() const override { return 50000; }

    String activityDescription(const Json& input) const override {
        return "📄 Read " + input.value("file_path", "");
    }

    bool alwaysLoad() const override { return true; }

    bool isCollapsible() const override { return true; }
    bool isReadTool() const override { return true; }
    String renderToolResult(const String& result, bool isError,
                            bool isCancelled, bool isRejected) const override;
};

} // namespace claude
