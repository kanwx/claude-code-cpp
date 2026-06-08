#ifdef HAS_FTXUI

#include "LspToolRenderer.hpp"
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

ftxui::Element makeLspBadge() {
    auto fg = ftxui_colors::toolFgColor("LSP");
    auto bg = ftxui_colors::toolBgColor("LSP");
    return ftxui::text(" LSP ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

std::string truncateResult(const std::string& text, size_t maxLen = 80) {
    if (text.size() <= maxLen) return text;
    return text.substr(0, maxLen - 1) + "…";
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element LspToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                        const RenderContext& ctx) {
    auto badge = makeLspBadge();
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string desc;
    if (!operation.empty() && !symbol.empty()) {
        desc = operation + " " + symbol;
    } else if (!symbol.empty()) {
        desc = symbol;
    } else if (!operation.empty()) {
        desc = operation;
    } else {
        desc = claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true);
    }
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string LspToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string desc;
    if (!operation.empty() && !symbol.empty()) {
        desc = operation + " " + symbol;
    } else if (!symbol.empty()) {
        desc = symbol;
    } else if (!operation.empty()) {
        desc = operation;
    } else {
        desc = claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true);
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " LSP " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element LspToolRenderer::renderToolResult(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string label = operation.empty() ? tool.toolName : operation;
    if (!symbol.empty()) label += " " + symbol;

    if (!ctx.verbose) {
        // Compact: operation + symbol + brief result
        auto brief = truncateResult(result.result);
        return hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted),
            text(brief) | ftxui::color(ctx.theme.success),
        });
    }
    // Verbose: label + full result
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label) | ftxui::color(ctx.theme.muted) | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string LspToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string label = operation.empty() ? tool.toolName : operation;
    if (!symbol.empty()) label += " " + symbol;

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + ": " + result.result + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + "\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element LspToolRenderer::renderToolError(const ToolResultBlock& result,
                                          const ToolUseBlock& tool,
                                          const RenderContext& ctx) {
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string label = operation.empty() ? tool.toolName : operation;
    if (!symbol.empty()) label += " " + symbol;
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string LspToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                   const ToolUseBlock& tool) {
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string label = operation.empty() ? tool.toolName : operation;
    if (!symbol.empty()) label += " " + symbol;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element LspToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                             const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("LSP (rejected)") | ftxui::color(ctx.theme.toolRejected) | dim,
    });
}

std::string LspToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  LSP (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element LspToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                             const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("LSP (canceled)") | dim | ftxui::color(ctx.theme.muted),
    });
}

std::string LspToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  LSP (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element LspToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                             const std::string& progress,
                                             const RenderContext& ctx) {
    auto badge = makeLspBadge();
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    std::string desc;
    if (!operation.empty() && !symbol.empty()) {
        desc = operation + " " + symbol;
    } else if (!symbol.empty()) {
        desc = symbol;
    } else {
        desc = claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true);
    }
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

Element LspToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("LSP") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element LspToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeLspBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string LspToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto operation = extractField(tool.input, "operation");
    auto symbol = extractField(tool.input, "symbol");
    if (!operation.empty() && !symbol.empty()) return operation + " " + symbol;
    if (!symbol.empty()) return symbol;
    if (!operation.empty()) return operation;
    return claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false);
}

std::string LspToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "LSP";
}

bool LspToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
