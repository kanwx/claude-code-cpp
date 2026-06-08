#ifdef HAS_FTXUI

#include "GlobToolRenderer.hpp"
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

ftxui::Element makeGlobBadge() {
    auto fg = ftxui_colors::toolFgColor("Glob");
    auto bg = ftxui_colors::toolBgColor("Glob");
    return ftxui::text(" Search ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

int countFiles(const std::string& result) {
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

Element GlobToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeGlobBadge();
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : pattern;
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string GlobToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : pattern;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Search " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element GlobToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto files = countFiles(result.result);

    // No results: "No files found" (dim)
    if (files == 0) {
        return hbox({
            text("  ⎿  ") | dim,
            text("No files found") | dim,
        });
    }

    if (!ctx.verbose) {
        // Compact: "Found {N} files [Ctrl+O to expand]" (N bold)
        return hbox({
            text("  ⎿  ") | dim,
            text("Found ") | dim,
            text(std::to_string(files)) | bold,
            text(" files") | dim,
            text(" [Ctrl+O to expand]") | dim,
        });
    }
    // Verbose: "Found {N} files" + file list
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text("Found ") | dim,
            text(std::to_string(files)) | bold,
            text(" files") | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string GlobToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    auto files = countFiles(result.result);
    std::string fileStr = std::to_string(files) + " files";

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + " " + AnsiStyle::RESET +
               AnsiStyle::Semantic::DIFF_ADD + fileStr + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + " " + AnsiStyle::RESET +
           AnsiStyle::Semantic::DIFF_ADD + fileStr + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::STATUS_DIM + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element GlobToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Error: " + result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string GlobToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element GlobToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Tool use rejected") | dim,
    });
}

std::string GlobToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Search (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element GlobToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Interrupted \xE2\x88\x99 What should Claude do instead?") | dim,
    });
}

std::string GlobToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Search (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element GlobToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeGlobBadge();
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Matching " + pattern;
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

Element GlobToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Search") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element GlobToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeGlobBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string GlobToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    return pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : pattern;
}

std::string GlobToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Search";
}

bool GlobToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
