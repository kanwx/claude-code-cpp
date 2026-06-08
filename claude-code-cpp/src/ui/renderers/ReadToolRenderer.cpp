#ifdef HAS_FTXUI

#include "ReadToolRenderer.hpp"
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

ftxui::Element makeReadBadge() {
    auto fg = ftxui_colors::toolFgColor("Read");
    auto bg = ftxui_colors::toolBgColor("Read");
    return ftxui::text(" Read ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

int countLines(const std::string& result) {
    if (result.empty()) return 0;
    int count = 1;
    for (char c : result) {
        if (c == '\n') count++;
    }
    return count;
}

bool isFileNotFound(const std::string& result) {
    return result.find("not found") != std::string::npos ||
           result.find("No such file") != std::string::npos ||
           result.find("does not exist") != std::string::npos;
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element ReadToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeReadBadge();
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : filePath;
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string ReadToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : filePath;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Read " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element ReadToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    if (!ctx.verbose && !ctx.toolResultExpanded) {
        // Compact: "Read {N} lines [Ctrl+O to expand]" (N bold)
        auto lines = countLines(result.result);
        return hbox({
            text("  ⎿  ") | dim,
            text("Read ") | dim,
            text(std::to_string(lines)) | bold,
            text(" lines") | dim,
            text(" [Ctrl+O to expand]") | dim,
        });
    }
    // Verbose/expanded: "Read {N} lines" + full content + collapse hint
    auto lines = countLines(result.result);
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text("Read ") | dim,
            text(std::to_string(lines)) | bold,
            text(" lines") | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
        hbox({
            text("  ⎿  ") | dim,
            text("[Ctrl+O to collapse]") | dim,
        }),
    });
}

std::string ReadToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;

    // For compact output just show file path
    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + "\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element ReadToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    // TS format: "File not found" or "Error reading file"
    auto label = isFileNotFound(result.result) ? "File not found" : "Error reading file";
    return hbox({
        text("  ⎿  ") | dim,
        text(label) | ftxui::color(ctx.theme.error),
    });
}

std::string ReadToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element ReadToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Tool use rejected") | dim,
    });
}

std::string ReadToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Read (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element ReadToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Interrupted \xE2\x88\x99 What should Claude do instead?") | dim,
    });
}

std::string ReadToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Read (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element ReadToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeReadBadge();
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Reading " + filePath;
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

Element ReadToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Read") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element ReadToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeReadBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string ReadToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    return filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : filePath;
}

std::string ReadToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Read";
}

bool ReadToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 500;
}

} // namespace claude::ui

#endif // HAS_FTXUI
