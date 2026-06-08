#ifdef HAS_FTXUI

#include "DefaultToolRenderer.hpp"
#include "ui/FtxuiColors.hpp"
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/ActivityDescription.hpp>

namespace claude::ui {

using namespace ftxui;

// ── Private helper ──────────────────────────────────────────────────

Element DefaultToolRenderer::makeBadge(const std::string& toolName) {
    auto fg = ftxui_colors::toolFgColor(toolName);
    auto bg = ftxui_colors::toolBgColor(toolName);
    return text(" " + toolName + " ") | ftxui::color(fg) | bgcolor(bg) | bold;
}

// ── Tool invocation ─────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto badge = makeBadge(tool.toolName);
    auto activity = text(" " + claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true));
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        activity | ftxui::color(ctx.theme.muted),
    });
}

std::string DefaultToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " " + tool.toolName + " " + AnsiStyle::RESET + " " +
           claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true);
}

// ── Success result ──────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolResult(const ToolResultBlock& result,
                                               const ToolUseBlock& tool,
                                               const RenderContext& ctx) {
    // Compact one-line summary in non-verbose mode
    if (!ctx.verbose && !ctx.toolResultExpanded && result.result.size() < 200) {
        return hbox({
            text("  ⎿  ") | dim,
            text(result.result) | ftxui::color(ctx.theme.muted),
        });
    }
    // Verbose: full output
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(tool.toolName) | ftxui::color(ctx.theme.muted) | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string DefaultToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                      const ToolUseBlock& tool) {
    if (result.result.size() < 200) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               result.result + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           tool.toolName + "\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolError(const ToolResultBlock& result,
                                              const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(tool.toolName) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string DefaultToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + tool.toolName + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                                 const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Tool use rejected") | dim,
    });
}

std::string DefaultToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& tool) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           tool.toolName + " (rejected)" + AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                                 const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Interrupted \xE2\x88\x99 What should Claude do instead?") | dim,
    });
}

std::string DefaultToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& tool) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           tool.toolName + " (canceled)" + AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                                 const std::string& progress,
                                                 const RenderContext& ctx) {
    auto badge = makeBadge(tool.toolName);
    auto activity = text(" " + claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true));
    auto dot = text("●") | ftxui::color(ctx.theme.accent) | blink;
    auto progText = progress.empty() ? text("") : text(" " + progress) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        dot,
        badge,
        activity | ftxui::color(ctx.theme.muted),
        text("…") | ftxui::color(ctx.theme.muted),
        progText,
    });
}

// ── Queued ──────────────────────────────────────────────────────────

Element DefaultToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                               const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text(tool.toolName) | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element DefaultToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeBadge(tools[0].toolName);
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string DefaultToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    return claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false);
}

std::string DefaultToolRenderer::userFacingName(const ToolUseBlock& tool) {
    return tool.toolName;
}

bool DefaultToolRenderer::isCollapsible() const {
    return false;
}

bool DefaultToolRenderer::isResultTruncatable(const ToolResultBlock& /*result*/) const {
    return false;
}

} // namespace claude::ui

#endif // HAS_FTXUI
