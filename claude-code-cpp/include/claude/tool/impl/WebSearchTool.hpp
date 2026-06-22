#pragma once

#include "../Tool.hpp"

namespace claude {

/// Web 搜索工具
class WebSearchTool : public Tool {
public:
    String name() const override { return "WebSearch"; }

    String description() const override {
        return "Search the web for information. "
               "Returns search results with titles, URLs, and snippets.";
    }

    String inputSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "query": {
                    "type": "string",
                    "description": "The search query"
                },
                "engine": {
                    "type": "string",
                    "enum": ["duckduckgo", "bing", "google", "serper"],
                    "default": "duckduckgo",
                    "description": "Search engine to use"
                },
                "allowed_domains": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Only include results from these domains"
                }
            },
            "required": ["query"]
        })";
    }

    String execute(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe(const Json&) const override { return true; }

    String activityDescription(const Json& input) const override {
        return "🔍 Search: " + input.value("query", "");
    }

    bool shouldDefer() const override { return true; }
    String searchHint() const override { return "search the web for information"; }

    bool isCollapsible() const override { return true; }
    bool isSearchTool() const override { return true; }
    ToolResultSummary renderToolResult(const String& result, bool isError,
                            bool isCancelled, bool isRejected) const override;
};

} // namespace claude
