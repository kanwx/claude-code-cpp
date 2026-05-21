#pragma once

#include "../Tool.hpp"

namespace claude {

/// Web 获取工具
class WebFetchTool : public Tool {
public:
    String name() const override { return "WebFetch"; }

    String description() const override {
        return "Fetch content from a URL. "
               "Returns the content for analysis or processing.";
    }

    String inputSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "url": {
                    "type": "string",
                    "description": "The URL to fetch"
                },
                "prompt": {
                    "type": "string",
                    "description": "What to extract from the content"
                }
            },
            "required": ["url", "prompt"]
        })";
    }

    String execute(const Json& input, ToolContext& context) override;

    PermissionResult checkPermission(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return true; }

    size_t maxResultSizeChars() const override { return 50000; }

    String activityDescription(const Json& input) const override {
        return "🌐 Fetch " + input.value("url", "");
    }

    bool shouldDefer() const override { return true; }
    String searchHint() const override { return "fetch and analyze web pages"; }
};

} // namespace claude
