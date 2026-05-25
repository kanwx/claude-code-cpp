#pragma once

#include "../Tool.hpp"

namespace claude {

/// LSP 工具 - 提供代码智能功能
class LSPTool : public Tool {
public:
    String name() const override { return "LSP"; }

    String description() const override {
        return R"(Query Language Server Protocol for code intelligence. Provides:
- goto_definition: Jump to where a symbol is defined
- find_references: Find all usages of a symbol
- hover: Get type info and documentation for a symbol
- diagnostics: Get errors/warnings for a file
- document_symbols: List all symbols in a file

Use this when you need to understand code structure, find symbol definitions,
or check for compile errors. Requires an LSP server configured for the file type.)";
    }

    String inputSchema() const override;
    String execute(const Json& input, ToolContext& context) override;
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe(const Json&) const override { return true; }

    String activityDescription(const Json& input) const override {
        String action = input.value("action", "query");
        return "LSP " + action;
    }

    bool shouldDefer() const override { return true; }
    String searchHint() const override { return "language server protocol code intelligence"; }
};

} // namespace claude
