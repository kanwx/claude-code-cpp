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
    return ftxui::text(" Search ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

int countLines(const std::string& result) {
    if (result.empty()) return 0;
    int count = 1;
    for (char c : result) {
        if (c == '\n') count++;
    }
    return count;
}

int countFileNames(const std::string& result) {
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
    // Trim trailing newline
    if (!result.empty() && result.back() == '\n') count--;
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
        text("  ⎿  ") | dim,
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string GrepToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto desc = pattern.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : pattern;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Search " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element GrepToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto outputMode = extractField(tool.input, "output_mode");
    // Default to "content" if not specified
    if (outputMode.empty()) outputMode = "content";

    auto lines = countLines(result.result);

    // No results: "No matches found" (dim)
    if (lines == 0 || result.result == "No matches found.\n" || result.result == "No matches found.") {
        return hbox({
            text("  ⎿  ") | dim,
            text("No matches found") | dim,
        });
    }

    if (!ctx.verbose) {
        // Compact formats based on output_mode
        if (outputMode == "files_with_matches") {
            auto files = countFileNames(result.result);
            return hbox({
                text("  ⎿  ") | dim,
                text("Found ") | dim,
                text(std::to_string(files)) | bold,
                text(" files") | dim,
                text(" [Ctrl+O to expand]") | dim,
            });
        } else if (outputMode == "count") {
            auto files = countFileNames(result.result);
            auto matches = lines; // in count mode, each line is a file count
            return hbox({
                text("  ⎿  ") | dim,
                text("Found ") | dim,
                text(std::to_string(matches)) | bold,
                text(" matches across ") | dim,
                text(std::to_string(files)) | bold,
                text(" files") | dim,
                text(" [Ctrl+O to expand]") | dim,
            });
        } else {
            // content mode
            return hbox({
                text("  ⎿  ") | dim,
                text("Found ") | dim,
                text(std::to_string(lines)) | bold,
                text(" lines") | dim,
                text(" [Ctrl+O to expand]") | dim,
            });
        }
    }
    // Verbose: summary + full output
    if (outputMode == "files_with_matches") {
        auto files = countFileNames(result.result);
        return vbox({
            hbox({
                text("  ⎿  ") | dim,
                text("Found ") | dim,
                text(std::to_string(files)) | bold,
                text(" files") | dim,
            }),
            text(result.result) | ftxui::color(ctx.theme.muted) | dim,
        });
    } else if (outputMode == "count") {
        auto files = countFileNames(result.result);
        return vbox({
            hbox({
                text("  ⎿  ") | dim,
                text("Found ") | dim,
                text(std::to_string(lines)) | bold,
                text(" matches across ") | dim,
                text(std::to_string(files)) | bold,
                text(" files") | dim,
            }),
            text(result.result) | ftxui::color(ctx.theme.muted) | dim,
        });
    } else {
        return vbox({
            hbox({
                text("  ⎿  ") | dim,
                text("Found ") | dim,
                text(std::to_string(lines)) | bold,
                text(" lines") | dim,
            }),
            text(result.result) | ftxui::color(ctx.theme.muted) | dim,
        });
    }
}

std::string GrepToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto outputMode = extractField(tool.input, "output_mode");
    if (outputMode.empty()) outputMode = "content";
    auto lines = countLines(result.result);

    // No results
    if (lines == 0 || result.result == "No matches found.\n" || result.result == "No matches found.") {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               "No matches found" + AnsiStyle::RESET;
    }

    std::string summary;
    if (outputMode == "files_with_matches") {
        summary = "Found " + std::to_string(countFileNames(result.result)) + " files [Ctrl+O to expand]";
    } else if (outputMode == "count") {
        summary = "Found " + std::to_string(lines) + " matches across " +
                  std::to_string(countFileNames(result.result)) + " files [Ctrl+O to expand]";
    } else {
        summary = "Found " + std::to_string(lines) + " lines [Ctrl+O to expand]";
    }

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               summary + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           summary + "\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Error: " + result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string GrepToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto pattern = extractField(tool.input, "pattern");
    auto label = pattern.empty() ? tool.toolName : pattern;
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Tool use rejected") | dim,
    });
}

std::string GrepToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Search (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element GrepToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⎿  ") | dim,
        text("Interrupted \xE2\x88\x99 What should Claude do instead?") | dim,
    });
}

std::string GrepToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿  Search (canceled)" +
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
        text("  ⎿  ") | dim,
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
        text("Search") | dim | ftxui::color(ctx.theme.muted),
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
        text("  ⎿  ") | dim,
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
    return "Search";
}

bool GrepToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
