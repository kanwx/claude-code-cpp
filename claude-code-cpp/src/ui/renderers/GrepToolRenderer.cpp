#ifdef HAS_FTXUI

#include "GrepToolRenderer.hpp"
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

ftxui::Element makeGrepBadge() {
    auto fg = ftxui_colors::toolFgColor("Grep");
    auto bg = ftxui_colors::toolBgColor("Grep");
    return ftxui::text(" Grep ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

int countMatches(const std::string& result) {
    if (result.empty()) return 0;
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

Element GrepToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeGrepBadge();
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : pattern;
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string GrepToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : pattern;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "⎿ " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Grep " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element GrepToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    auto matches = countMatches(result.result);
    std::string matchStr = std::to_string(matches) + " matches";

    if (!ctx.verbose) {
        // Compact: pattern + match count
        return hbox({
            text("  ✓ ") | ftxui::color(ctx.theme.toolSuccess) | bold,
            text(label + " ") | ftxui::color(ctx.theme.muted),
            text(matchStr) | ftxui::color(ctx.theme.success),
        });
    }
    // Verbose: pattern, match count, and output
    return vbox({
        hbox({
            text("  ✓ ") | ftxui::color(ctx.theme.toolSuccess) | bold,
            text(label + " ") | ftxui::color(ctx.theme.muted) | dim,
            text(matchStr) | ftxui::color(ctx.theme.success),
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string GrepToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    auto matches = countMatches(result.result);
    std::string matchStr = std::to_string(matches) + " matches";

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_SUCCESS) + "  ✓ " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + " " + AnsiStyle::RESET +
               AnsiStyle::Semantic::DIFF_ADD + matchStr + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_SUCCESS) + "  ✓ " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + " " + AnsiStyle::RESET +
           AnsiStyle::Semantic::DIFF_ADD + matchStr + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::STATUS_DIM + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    return vbox({
        hbox({
            text("  ✗ ") | ftxui::color(ctx.theme.toolError) | bold,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string GrepToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    return std::string(AnsiStyle::Semantic::TOOL_ERROR) + "  ✗ " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⊘ ") | ftxui::color(ctx.theme.toolRejected) | dim,
        text("Grep (rejected)") | ftxui::color(ctx.theme.toolRejected) | dim,
    });
}

std::string GrepToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_REJECTED) + "  ⊘ Grep (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⊘ ") | dim | ftxui::color(ctx.theme.toolCanceled),
        text("Grep (canceled)") | dim | ftxui::color(ctx.theme.muted),
    });
}

std::string GrepToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_CANCELLED) + "  ⊘ Grep (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeGrepBadge();
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Searching " + pattern;
    auto dot = text("●") | ftxui::color(ctx.theme.accent) | blink;
    auto progText = progress.empty() ? text("") : text(" " + progress) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        dot,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
        text("…") | ftxui::color(ctx.theme.muted),
        progText,
    });
}

// ── Queued ──────────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Grep") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element GrepToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeGrepBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string GrepToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    return pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : pattern;
}

std::string GrepToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Grep";
}

bool GrepToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
