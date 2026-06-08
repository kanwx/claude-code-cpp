#ifdef HAS_FTXUI

#include "BashToolRenderer.hpp"
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

ftxui::Element makeBashBadge() {
    auto fg = ftxui_colors::toolFgColor("Bash");
    auto bg = ftxui_colors::toolBgColor("Bash");
    return ftxui::text(" Bash ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

std::string truncateCmd(const std::string& cmd, size_t maxLen = 60) {
    if (cmd.size() <= maxLen) return cmd;
    return cmd.substr(0, maxLen - 1) + "…";
}

std::string firstLine(const std::string& output, size_t maxLen = 80) {
    auto nl = output.find('\n');
    std::string line = (nl == std::string::npos) ? output : output.substr(0, nl);
    // Trim trailing CR
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() <= maxLen) return line;
    return line.substr(0, maxLen - 1) + "…";
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element BashToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeBashBadge();
    auto cmd = extractField(tool.input, "command");
    auto display = cmd.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : truncateCmd(cmd);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        text(" " + display) | ftxui::color(ctx.theme.muted),
    });
}

std::string BashToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto cmd = extractField(tool.input, "command");
    auto display = cmd.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : truncateCmd(cmd);
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Bash " + AnsiStyle::RESET + " " + display;
}

// ── Success result ──────────────────────────────────────────────────

Element BashToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    if (!ctx.verbose) {
        // Compact: "Done" (dim) if no output, first line otherwise
        auto line = firstLine(result.result);
        if (line.empty()) {
            return hbox({
                text("  ⎿  ") | dim,
                text("Done") | dim,
            });
        }
        return hbox({
            text("  ⎿  ") | dim,
            text(line),
        });
    }
    // Verbose: full output
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text("Bash") | ftxui::color(ctx.theme.muted) | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string BashToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto line = firstLine(result.result);
    if (result.result.size() < 200) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               line + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           "Bash\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element BashToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    // TS format: "Error: {message}" (error color)
    auto msg = firstLine(result.result);
    if (msg.empty()) msg = "Unknown error";
    return hbox({
        text("  ⎿  ") | dim,
        text("Error: " + msg) | ftxui::color(ctx.theme.error),
    });
}

std::string BashToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto cmd = extractField(tool.input, "command");
    auto label = cmd.empty() ? tool.toolName : truncateCmd(cmd, 40);
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element BashToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Tool use rejected") | dim,
    });
}

std::string BashToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Bash (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element BashToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Interrupted \xE2\x88\x99 What should Claude do instead?") | dim,
    });
}

std::string BashToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Bash (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element BashToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeBashBadge();
    auto cmd = extractField(tool.input, "command");
    auto desc = cmd.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : truncateCmd(cmd);
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

Element BashToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Bash") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element BashToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeBashBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string BashToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto cmd = extractField(tool.input, "command");
    return cmd.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : truncateCmd(cmd);
}

std::string BashToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Bash";
}

bool BashToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    // Bash output can be large but is always visible (not collapsible)
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
