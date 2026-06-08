#ifdef HAS_FTXUI

#include "WebSearchToolRenderer.hpp"
#include "ui/FtxuiColors.hpp"
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/ActivityDescription.hpp>

namespace claude::ui {

namespace {

static std::string extractField(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // skip opening quote
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) { result += json[pos+1]; pos += 2; }
        else { result += json[pos]; pos++; }
    }
    return result;
}

ftxui::Element makeWebSearchBadge() {
    auto fg = ftxui_colors::toolFgColor("WebSearch");
    auto bg = ftxui_colors::toolBgColor("WebSearch");
    return ftxui::text(" WebSearch ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

int countSearchResults(const std::string& result) {
    if (result.empty()) return 0;
    // Count occurrences of common search result delimiters
    int count = 0;
    size_t pos = 0;
    while (pos < result.size()) {
        auto nl = result.find('\n', pos);
        if (nl == std::string::npos) {
            count++;
            break;
        }
        count++;
        pos = nl + 1;
    }
    return count;
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    auto badge = makeWebSearchBadge();
    auto query = extractField(tool.input, "query");
    auto desc = query.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : query;
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string WebSearchToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto query = extractField(tool.input, "query");
    auto desc = query.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : query;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " WebSearch " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolResult(const ToolResultBlock& result,
                                                 const ToolUseBlock& tool,
                                                 const RenderContext& ctx) {
    auto query = extractField(tool.input, "query");
    auto label = query.empty() ? tool.toolName : query;
    auto resultCount = countSearchResults(result.result);
    std::string resultStr = std::to_string(resultCount) + " results";

    if (!ctx.verbose && !ctx.toolResultExpanded) {
        // Compact: query + result count
        return hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted),
            text(resultStr) | ftxui::color(ctx.theme.success),
            text(" [Ctrl+O to expand]") | dim,
        });
    }
    // Verbose/expanded: query, result count, and results + collapse hint
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted) | dim,
            text(resultStr) | ftxui::color(ctx.theme.success),
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
        hbox({
            text("  ⎿  ") | dim,
            text("[Ctrl+O to collapse]") | dim,
        }),
    });
}

std::string WebSearchToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                         const ToolUseBlock& tool) {
    auto query = extractField(tool.input, "query");
    auto label = query.empty() ? tool.toolName : query;
    auto resultCount = countSearchResults(result.result);
    std::string resultStr = std::to_string(resultCount) + " results";

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + " " + AnsiStyle::RESET +
               AnsiStyle::Semantic::DIFF_ADD + resultStr + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + " " + AnsiStyle::RESET +
           AnsiStyle::Semantic::DIFF_ADD + resultStr + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::STATUS_DIM + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolError(const ToolResultBlock& result,
                                                const ToolUseBlock& tool,
                                                const RenderContext& ctx) {
    auto query = extractField(tool.input, "query");
    auto label = query.empty() ? tool.toolName : query;
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string WebSearchToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                         const ToolUseBlock& tool) {
    auto query = extractField(tool.input, "query");
    auto label = query.empty() ? tool.toolName : query;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                                   const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Tool use rejected") | dim,
    });
}

std::string WebSearchToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  WebSearch (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                                   const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Interrupted \xE2\x88\x99 What should Claude do instead?") | dim,
    });
}

std::string WebSearchToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  WebSearch (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                                   const std::string& progress,
                                                   const RenderContext& ctx) {
    auto badge = makeWebSearchBadge();
    auto query = extractField(tool.input, "query");
    auto desc = query.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Searching " + query;
    auto dot = text("●") | ftxui::color(ctx.theme.accent) | blink;
    auto progText = progress.empty() ? text("") : text(" " + progress) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        dot,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
        text("…") | ftxui::color(ctx.theme.muted),
        progText,
    });
}

// ── Queued ──────────────────────────────────────────────────────────

Element WebSearchToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                                 const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("WebSearch") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element WebSearchToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeWebSearchBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string WebSearchToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto query = extractField(tool.input, "query");
    return query.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : query;
}

std::string WebSearchToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "WebSearch";
}

bool WebSearchToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
