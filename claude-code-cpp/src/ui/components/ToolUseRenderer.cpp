#include <claude/ui/components/ToolUseRenderer.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <claude/core/Types.hpp>
#include <sstream>

namespace claude {

using namespace ftxui;

// ========== Tool color helpers ==========

Color toolBgColor(const String& toolName) {
    if (toolName == "Read" || toolName == "FileReadTool") return Color::RGB(40, 50, 40);
    if (toolName == "Write" || toolName == "FileWriteTool") return Color::RGB(50, 40, 30);
    if (toolName == "Edit" || toolName == "FileEditTool") return Color::RGB(45, 40, 35);
    if (toolName == "Bash" || toolName == "BashTool") return Color::RGB(35, 35, 50);
    if (toolName == "Grep" || toolName == "GrepTool") return Color::RGB(35, 45, 50);
    if (toolName == "Glob" || toolName == "GlobTool") return Color::RGB(35, 45, 50);
    if (toolName == "WebFetch" || toolName == "WebFetchTool") return Color::RGB(40, 35, 50);
    if (toolName == "WebSearch" || toolName == "WebSearchTool") return Color::RGB(40, 35, 50);
    if (toolName == "Agent") return Color::RGB(50, 40, 45);
    if (toolName == "LSP" || toolName == "LSPTool") return Color::RGB(45, 45, 35);
    return Color::RGB(40, 40, 40);
}

Color toolFgColor(const String& toolName) {
    if (toolName == "Read" || toolName == "FileReadTool") return Color::GreenLight;
    if (toolName == "Write" || toolName == "FileWriteTool") return Color::RGB(255, 180, 100);
    if (toolName == "Edit" || toolName == "FileEditTool") return Color::RGB(255, 200, 130);
    if (toolName == "Bash" || toolName == "BashTool") return Color::BlueLight;
    if (toolName == "Grep" || toolName == "GrepTool") return Color::Cyan;
    if (toolName == "Glob" || toolName == "GlobTool") return Color::Cyan;
    if (toolName == "WebFetch" || toolName == "WebFetchTool") return Color::Magenta;
    if (toolName == "WebSearch" || toolName == "WebSearchTool") return Color::Magenta;
    if (toolName == "Agent") return Color::RGB(255, 120, 130);
    if (toolName == "LSP" || toolName == "LSPTool") return Color::Yellow;
    return Color::Yellow;
}

// ========== ToolUseRenderer ==========

std::vector<Element> ToolUseRenderer::render(const DisplayMessage& msg,
                                               const RendererContext& ctx) {
    std::vector<Element> elems;

    String inputSummary = msg.toolUse.input;
    if (inputSummary.size() > 80) inputSummary = inputSummary.substr(0, 77) + "...";
    String toolLine = msg.toolUse.toolName;
    if (!inputSummary.empty()) toolLine += " " + inputSummary;

    elems.push_back(hbox({
        text("  ⎿ "),
        text(" " + toolLine + " ") | bold | color(toolFgColor(msg.toolUse.toolName)) | bgcolor(toolBgColor(msg.toolUse.toolName)),
    }));

    return elems;
}

std::vector<Element> ToolUseRenderer::renderToolResult(const String& result,
                                                        const RendererContext& ctx) {
    std::vector<Element> elems;

    bool hasDiff = result.find("\n-") != String::npos ||
                   result.find("\n+") != String::npos ||
                   result.find("@@") != String::npos;

    if (hasDiff) {
        std::istringstream diffStream(result);
        String diffLine;
        while (std::getline(diffStream, diffLine)) {
            if (diffLine.starts_with("---") || diffLine.starts_with("+++")) {
                elems.push_back(text("    " + diffLine) | bold | color(Color::Cyan));
            } else if (diffLine.starts_with("@@")) {
                elems.push_back(text("    " + diffLine) | color(Color::Cyan));
            } else if (diffLine.starts_with("-")) {
                elems.push_back(text("    " + diffLine) | color(Color::Red));
            } else if (diffLine.starts_with("+")) {
                elems.push_back(text("    " + diffLine) | color(Color::Green));
            } else {
                elems.push_back(text("    " + diffLine) | dim | color(Color::GrayLight));
            }
        }
    } else {
        String resultSummary = result;
        if (resultSummary.size() > 300) resultSummary = resultSummary.substr(0, 297) + "...";
        elems.push_back(hbox({
            text("    ⎿ ") | color(Color::GrayDark),
            FtxuiMarkdown::renderInline(resultSummary) | dim | color(Color::GrayLight),
        }));
    }

    return elems;
}

// ========== ToolResultRenderer ==========

std::vector<Element> ToolResultRenderer::render(const DisplayMessage& msg,
                                                   const RendererContext& ctx) {
    // Tool results are rendered inline after their paired ToolUse
    // This renderer handles orphan results (rare case)
    if (!msg.toolResult.result.empty()) {
        return ToolUseRenderer::renderToolResult(msg.toolResult.result, ctx);
    }
    return {};
}

// ========== CollapsedToolRenderer ==========

std::vector<Element> CollapsedToolRenderer::render(const DisplayMessage& msg,
                                                     const RendererContext& ctx) {
    std::vector<Element> elems;

    if (msg.expanded) {
        // Show individual tools — need access to the original messages
        // For now, show the counts
        elems.push_back(hbox({
            text("  ⎿ ") | color(Color::Cyan),
            text("Expanded view") | color(Color::Cyan),
        }));
    } else {
        std::vector<String> parts;
        const auto& g = msg.collapsedGroup;
        if (g.searchCount > 0)
            parts.push_back("Searched " + std::to_string(g.searchCount) + " pattern" + (g.searchCount > 1 ? "s" : ""));
        if (g.readCount > 0)
            parts.push_back("read " + std::to_string(g.readCount) + " file" + (g.readCount > 1 ? "s" : ""));
        if (g.listCount > 0)
            parts.push_back("listed " + std::to_string(g.listCount) + " director" + (g.listCount > 1 ? "ies" : "y"));

        String summary;
        for (size_t pi = 0; pi < parts.size(); ++pi) {
            if (pi > 0) summary += ", ";
            summary += parts[pi];
        }

        elems.push_back(hbox({
            text("  ⎿ ") | color(Color::Cyan),
            text(summary) | color(Color::Cyan),
            text(" (ctrl+o to expand)") | dim | color(Color::GrayDark),
        }));

        if (!g.latestHint.empty()) {
            String hint = g.latestHint;
            if (hint.size() > 80) hint = hint.substr(0, 77) + "...";
            elems.push_back(hbox({
                text("    ⎿ ") | color(Color::GrayDark),
                text(hint) | dim | color(Color::GrayLight),
            }));
        }
    }

    return elems;
}

} // namespace claude
