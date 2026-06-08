#ifdef HAS_FTXUI

#include "WebFetchToolRenderer.hpp"
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

ftxui::Element makeWebFetchBadge() {
    auto fg = ftxui_colors::toolFgColor("WebFetch");
    auto bg = ftxui_colors::toolBgColor("WebFetch");
    return ftxui::text(" WebFetch ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

std::string truncateUrl(const std::string& url, size_t maxLen = 60) {
    if (url.size() <= maxLen) return url;
    return url.substr(0, maxLen - 1) + "…";
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element WebFetchToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                             const RenderContext& ctx) {
    auto badge = makeWebFetchBadge();
    auto url = extractField(tool.input, "url");
    auto desc = url.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : truncateUrl(url);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string WebFetchToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto url = extractField(tool.input, "url");
    auto desc = url.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : truncateUrl(url);
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " WebFetch " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element WebFetchToolRenderer::renderToolResult(const ToolResultBlock& result,
                                                const ToolUseBlock& tool,
                                                const RenderContext& ctx) {
    auto url = extractField(tool.input, "url");
    auto label = url.empty() ? tool.toolName : truncateUrl(url, 40);
    auto contentLen = result.result.size();
    std::string lenStr = std::to_string(contentLen) + " chars";

    if (!ctx.verbose) {
        // Compact: URL + content length
        return hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted),
            text(lenStr) | ftxui::color(ctx.theme.success),
        });
    }
    // Verbose: URL, content length, and fetched content
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted) | dim,
            text(lenStr) | ftxui::color(ctx.theme.success),
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string WebFetchToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                        const ToolUseBlock& tool) {
    auto url = extractField(tool.input, "url");
    auto label = url.empty() ? tool.toolName : truncateUrl(url, 40);
    auto contentLen = result.result.size();
    std::string lenStr = std::to_string(contentLen) + " chars";

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + " " + AnsiStyle::RESET +
               AnsiStyle::Semantic::DIFF_ADD + lenStr + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + " " + AnsiStyle::RESET +
           AnsiStyle::Semantic::DIFF_ADD + lenStr + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::STATUS_DIM + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element WebFetchToolRenderer::renderToolError(const ToolResultBlock& result,
                                               const ToolUseBlock& tool,
                                               const RenderContext& ctx) {
    auto url = extractField(tool.input, "url");
    auto label = url.empty() ? tool.toolName : truncateUrl(url, 40);
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string WebFetchToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                        const ToolUseBlock& tool) {
    auto url = extractField(tool.input, "url");
    auto label = url.empty() ? tool.toolName : truncateUrl(url, 40);
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element WebFetchToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                                  const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("WebFetch (rejected)") | ftxui::color(ctx.theme.toolRejected) | dim,
    });
}

std::string WebFetchToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  WebFetch (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element WebFetchToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                                  const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("WebFetch (canceled)") | dim | ftxui::color(ctx.theme.muted),
    });
}

std::string WebFetchToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  WebFetch (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element WebFetchToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                                  const std::string& progress,
                                                  const RenderContext& ctx) {
    auto badge = makeWebFetchBadge();
    auto url = extractField(tool.input, "url");
    auto desc = url.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Fetching " + truncateUrl(url);
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

Element WebFetchToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                                const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("WebFetch") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element WebFetchToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeWebFetchBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string WebFetchToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto url = extractField(tool.input, "url");
    return url.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : truncateUrl(url);
}

std::string WebFetchToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "WebFetch";
}

bool WebFetchToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
