#pragma once

#include "../Tool.hpp"

namespace claude {

/// Grep content search tool
/// Prioritizes ripgrep (rg) subprocess, falls back to std::regex
class GrepTool : public Tool {
public:
    String name() const override { return "Grep"; }

    String description() const override {
        return "Search for a pattern in file contents using ripgrep-style search. "
               "Fast content search with regex support.";
    }

    String inputSchema() const override;

    String execute(const Json& input, ToolContext& context) override;

    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe(const Json&) const override { return true; }

    size_t maxResultSizeChars() const override { return 30000; }

    String activityDescription(const Json& input) const override {
        return "Grep " + input.value("pattern", "");
    }

    bool alwaysLoad() const override { return true; }

    String userFacingName() const override { return "Search"; }

    bool isCollapsible() const override { return true; }
    bool isSearchTool() const override { return true; }
    ToolResultSummary renderToolResult(const String& result, bool isError,
                            bool isCancelled, bool isRejected) const override;

    /// Check if ripgrep is installed on the system
    static bool hasRipgrep();

    // ---- Test injection points (for unit-testing fallback paths) ----
    static bool forceUseFallbackForTest;
    static String ripgrepPathOverride;

    // Exposed for tests; not part of public tool contract.
    /// Check if pattern is safe for std::regex (no catastrophic backtracking risk)
    static bool isRegexPatternSafe(const String& pattern);

    // Exposed for tests; not part of public tool contract.
    /// Check if pattern contains no regex metacharacters (plain literal)
    static bool isLiteralPattern(const String& pattern);

private:
    /// Execute search using ripgrep subprocess
    String executeWithRipgrep(const Json& input, ToolContext& context);

    /// Execute search using std::regex (fallback)
    String executeWithRegex(const Json& input, ToolContext& context);

    /// Cached path to rg binary
    static String cachedRgPath_;
};

} // namespace claude
