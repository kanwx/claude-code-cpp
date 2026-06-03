#ifdef HAS_FTXUI

#include "WriteToolRenderer.hpp"
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

ftxui::Element makeWriteBadge() {
    auto fg = ftxui_colors::toolFgColor("Write");
    auto bg = ftxui_colors::toolBgColor("Write");
    return ftxui::text(" Write ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

int countBytes(const std::string& text) {
    return static_cast<int>(text.size());
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element WriteToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeWriteBadge();
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : filePath;
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string WriteToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : filePath;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "⎿ " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Write " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element WriteToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    auto bytes = countBytes(result.result);
    std::string byteStr = std::to_string(bytes) + " bytes";

    if (!ctx.verbose) {
        // Compact: file path + byte count
        return hbox({
            text("  ✓ ") | ftxui::color(ctx.theme.toolSuccess) | bold,
            text("Wrote ") | ftxui::color(ctx.theme.muted),
            text(byteStr) | ftxui::color(ctx.theme.success),
            text(" to " + label) | ftxui::color(ctx.theme.muted),
        });
    }
    // Verbose: file path, byte count, and content
    return vbox({
        hbox({
            text("  ✓ ") | ftxui::color(ctx.theme.toolSuccess) | bold,
            text("Wrote " + byteStr + " to " + label) | ftxui::color(ctx.theme.muted) | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string WriteToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    auto bytes = countBytes(result.result);
    std::string byteStr = std::to_string(bytes) + " bytes";

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_SUCCESS) + "  ✓ " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               "Wrote " + byteStr + " to " + label + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_SUCCESS) + "  ✓ " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           "Wrote " + byteStr + " to " + label + "\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element WriteToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    return vbox({
        hbox({
            text("  ✗ ") | ftxui::color(ctx.theme.toolError) | bold,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string WriteToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    return std::string(AnsiStyle::Semantic::TOOL_ERROR) + "  ✗ " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element WriteToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⊘ ") | ftxui::color(ctx.theme.toolRejected) | dim,
        text("Write (rejected)") | ftxui::color(ctx.theme.toolRejected) | dim,
    });
}

std::string WriteToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_REJECTED) + "  ⊘ Write (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element WriteToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⊘ ") | dim | ftxui::color(ctx.theme.toolCanceled),
        text("Write (canceled)") | dim | ftxui::color(ctx.theme.muted),
    });
}

std::string WriteToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_CANCELLED) + "  ⊘ Write (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element WriteToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeWriteBadge();
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Writing " + filePath;
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

Element WriteToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Write") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element WriteToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeWriteBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string WriteToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    return filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : filePath;
}

std::string WriteToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Write";
}

bool WriteToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
