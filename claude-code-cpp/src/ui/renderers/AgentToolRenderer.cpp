#ifdef HAS_FTXUI

#include "AgentToolRenderer.hpp"
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

ftxui::Element makeAgentBadge() {
    auto fg = ftxui_colors::toolFgColor("Agent");
    auto bg = ftxui_colors::toolBgColor("Agent");
    return ftxui::text(" Agent ") | ftxui::color(fg) | ftxui::bgcolor(bg) | ftxui::bold;
}

std::string truncateDesc(const std::string& desc, size_t maxLen = 60) {
    if (desc.size() <= maxLen) return desc;
    return desc.substr(0, maxLen - 1) + "…";
}

} // anonymous namespace

using namespace ftxui;

// ── Tool invocation ─────────────────────────────────────────────────

Element AgentToolRenderer::renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) {
    auto badge = makeAgentBadge();
    auto agentType = extractField(tool.input, "type");
    auto description = extractField(tool.input, "description");
    std::string desc;
    if (!agentType.empty() && !description.empty()) {
        desc = agentType + ": " + truncateDesc(description);
    } else if (!description.empty()) {
        desc = truncateDesc(description);
    } else if (!agentType.empty()) {
        desc = agentType;
    } else {
        desc = claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true);
    }
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        badge,
        text(" " + desc) | ftxui::color(ctx.theme.muted),
    });
}

std::string AgentToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    auto agentType = extractField(tool.input, "type");
    auto description = extractField(tool.input, "description");
    std::string desc;
    if (!agentType.empty() && !description.empty()) {
        desc = agentType + ": " + truncateDesc(description);
    } else if (!description.empty()) {
        desc = truncateDesc(description);
    } else if (!agentType.empty()) {
        desc = agentType;
    } else {
        desc = claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true);
    }
    return std::string(AnsiStyle::Semantic::TOOL_PREFIX) + "⎿ " + AnsiStyle::RESET +
           AnsiStyle::toolBgColor(tool.toolName) + AnsiStyle::toolFgColor(tool.toolName) +
           AnsiStyle::BOLD + " Agent " + AnsiStyle::RESET + " " + desc;
}

// ── Success result ──────────────────────────────────────────────────

Element AgentToolRenderer::renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    auto description = extractField(tool.input, "description");
    auto label = description.empty() ? tool.toolName : truncateDesc(description);

    if (!ctx.verbose) {
        // Compact: sub-agent result summary (first line)
        auto firstNl = result.result.find('\n');
        std::string summary = (firstNl == std::string::npos)
            ? result.result
            : result.result.substr(0, firstNl);
        if (summary.size() > 80) summary = summary.substr(0, 79) + "…";
        return hbox({
            text("  ✓ ") | ftxui::color(ctx.theme.toolSuccess) | bold,
            text(label + ": ") | ftxui::color(ctx.theme.muted),
            text(summary) | ftxui::color(ctx.theme.success),
        });
    }
    // Verbose: label + full result
    return vbox({
        hbox({
            text("  ✓ ") | ftxui::color(ctx.theme.toolSuccess) | bold,
            text(label) | ftxui::color(ctx.theme.muted) | dim,
        }),
        text(result.result) | ftxui::color(ctx.theme.muted) | dim,
    });
}

std::string AgentToolRenderer::renderToolResultAnsi(const ToolResultBlock& result,
                                                     const ToolUseBlock& tool) {
    auto description = extractField(tool.input, "description");
    auto label = description.empty() ? tool.toolName : truncateDesc(description);

    if (result.result.size() < 500) {
        return std::string(AnsiStyle::Semantic::TOOL_SUCCESS) + "  ✓ " +
               AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
               label + ": " + result.result + AnsiStyle::RESET;
    }
    return std::string(AnsiStyle::Semantic::TOOL_SUCCESS) + "  ✓ " +
           AnsiStyle::RESET + AnsiStyle::Semantic::STATUS_DIM +
           label + "\n" + result.result + AnsiStyle::RESET;
}

// ── Error result ────────────────────────────────────────────────────

Element AgentToolRenderer::renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) {
    auto description = extractField(tool.input, "description");
    auto label = description.empty() ? tool.toolName : truncateDesc(description);
    return vbox({
        hbox({
            text("  ✗ ") | ftxui::color(ctx.theme.toolError) | bold,
            text(label) | ftxui::color(ctx.theme.error),
        }),
        text(result.result) | ftxui::color(ctx.theme.error),
    });
}

std::string AgentToolRenderer::renderToolErrorAnsi(const ToolResultBlock& result,
                                                    const ToolUseBlock& tool) {
    auto description = extractField(tool.input, "description");
    auto label = description.empty() ? tool.toolName : truncateDesc(description);
    return std::string(AnsiStyle::Semantic::TOOL_ERROR) + "  ✗ " +
           AnsiStyle::BOLD + label + AnsiStyle::RESET + "\n" +
           AnsiStyle::Semantic::TOOL_ERROR + result.result + AnsiStyle::RESET;
}

// ── Rejected ────────────────────────────────────────────────────────

Element AgentToolRenderer::renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⊘ ") | ftxui::color(ctx.theme.toolRejected) | dim,
        text("Agent (rejected)") | ftxui::color(ctx.theme.toolRejected) | dim,
    });
}

std::string AgentToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_REJECTED) + "  ⊘ Agent (rejected)" +
           AnsiStyle::RESET;
}

// ── Canceled ────────────────────────────────────────────────────────

Element AgentToolRenderer::renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) {
    return hbox({
        text("  ⊘ ") | dim | ftxui::color(ctx.theme.toolCanceled),
        text("Agent (canceled)") | dim | ftxui::color(ctx.theme.muted),
    });
}

std::string AgentToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& /*tool*/) {
    return std::string(AnsiStyle::Semantic::TOOL_CANCELLED) + "  ⊘ Agent (canceled)" +
           AnsiStyle::RESET;
}

// ── Progress ────────────────────────────────────────────────────────

Element AgentToolRenderer::renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) {
    auto badge = makeAgentBadge();
    auto description = extractField(tool.input, "description");
    auto desc = description.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/true)
        : "Running " + truncateDesc(description);
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

Element AgentToolRenderer::renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) {
    return hbox({
        text("  ○ ") | dim | ftxui::color(ctx.theme.dimBorder),
        text("Agent") | dim | ftxui::color(ctx.theme.muted),
        text(" (queued)") | dim | ftxui::color(ctx.theme.dimBorder),
    });
}

// ── Grouped ─────────────────────────────────────────────────────────

Element AgentToolRenderer::renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) {
    if (tools.empty()) return text("");
    auto badge = makeAgentBadge();
    auto count = text(" ×" + std::to_string(tools.size())) | ftxui::color(ctx.theme.muted);
    return hbox({
        text("⎿ ") | dim | ftxui::color(ctx.theme.dimBorder),
        badge,
        count,
    });
}

// ── Summary / naming / classification ──────────────────────────────

std::string AgentToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    auto description = extractField(tool.input, "description");
    return description.empty()
        ? claude::getActivityDescription(tool.toolName, tool.input, /*active=*/false)
        : truncateDesc(description);
}

std::string AgentToolRenderer::userFacingName(const ToolUseBlock& /*tool*/) {
    return "Agent";
}

bool AgentToolRenderer::isResultTruncatable(const ToolResultBlock& result) const {
    return result.result.size() > 2000;
}

} // namespace claude::ui

#endif // HAS_FTXUI
