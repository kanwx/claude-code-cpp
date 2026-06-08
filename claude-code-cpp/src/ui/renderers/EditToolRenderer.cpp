#ifdef HAS_FTXUI

#include "EditToolRenderer.hpp"
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

ftxui::Element makeEditBadge() {
    auto fg = ftxui_colors::toolFgColor("Edit");
    auto bg = ftxui_colors::toolBgColor("Edit");
    return ftxui::text(" Edit ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

/// Count lines starting with '+' (excluding '+++' headers) and '-' (excluding '---' headers).
void countDiffLines(const std::string& result, int& added, int& removed) {
    added = 0;
    removed = 0;
    size_t pos = 0;
    while (pos < result.size()) {
        auto nl = result.find('\n', pos);
        std::string line;
        if (nl == std::string::npos) {
            line = result.substr(pos);
            pos = result.size();
        } else {
            line = result.substr(pos, nl - pos);
            pos = nl + 1;
        }
        // Trim trailing CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 2 && line[0] == '+' && line[1] != '+') added++;
        else if (line.size() >= 2 && line[0] == '-' && line[1] != '-') removed++;
        else if (line.size() == 1 && line[0] == '+') added++;
        else if (line.size() == 1 && line[0] == '-') removed++;
    }
}

int countLines(const std::string& text) {
    if (text.empty()) return 0;
    int count = 1;
    for (char c : text) {
        if (c == '\n') count++;
    }
    return count;
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element EditToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeEditBadge();
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

std::string EditToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : filePath;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Edit " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element EditToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;

    int added = 0, removed = 0;
    countDiffLines(result.result, added, removed);
    std::string diffSummary = "+" + std::to_string(added) + "/-" + std::to_string(removed);

    if (!ctx.verbose) {
        // Compact: file path + diff summary
        return hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted),
            text(diffSummary) | ftxui::color(ctx.theme.success),
        });
    }
    // Verbose: file path, diff summary, and actual diff lines
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label + " ") | ftxui::color(ctx.theme.muted) | dim,
            text(diffSummary) | ftxui::color(ctx.theme.success),
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string EditToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;

    int added = 0, removed = 0;
    countDiffLines(result.result, added, removed);
    std::string diffSummary = "+" + std::to_string(added) + "/-" + std::to_string(removed);

    if (countLines(result.result) <= 5) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + " " + AnsiStyle::RESET +
               AnsiStyle::Semantic::DIFF_ADD + diffSummary + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + " " + AnsiStyle::RESET +
           AnsiStyle::Semantic::DIFF_ADD + diffSummary + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::STATUS_DIM + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element EditToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    return vbox({
        hbox({
            text("  ⎿  ") | dim,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string EditToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    auto label = filePath.empty() ? tool.toolName : filePath;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element EditToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Edit (rejected)") | ftxui::color(ctx.theme.toolRejected) | dim,
    });
}

std::string EditToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Edit (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element EditToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Edit (canceled)") | dim | ftxui::color(ctx.theme.muted),
    });
}

std::string EditToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Edit (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element EditToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeEditBadge();
    auto filePath = extractField(tool.input, "file_path");
    auto desc = filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Editing " + filePath;
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

Element EditToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Edit") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element EditToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeEditBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("  ⎿  ") | dim,
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string EditToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto filePath = extractField(tool.input, "file_path");
    return filePath.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : filePath;
}

std::string EditToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Edit";
}

bool EditToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return countLines(result.result) > 20;
}

} // namespace claude::ui

#endif // HAS_FTXUI
