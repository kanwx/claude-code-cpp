#include <claude/ui/components/AssistantTextRenderer.hpp>
#include <claude/core/Types.hpp>
#include <sstream>

namespace claude {

using namespace ftxui;

static const Color BrandOrange = Color::RGB(215, 119, 87);

// ========== AssistantTextRenderer ==========

std::vector<Element> AssistantTextRenderer::render(const DisplayMessage& msg,
                                                     const RendererContext& ctx) {
    std::vector<Element> elems;
    if (!msg.text.empty()) {
        auto mdBlocks = FtxuiMarkdown::render(msg.text);
        for (auto& mdElem : mdBlocks) {
            elems.push_back(std::move(mdElem));
        }
    }
    return elems;
}

// ========== AssistantThinkingRenderer ==========

std::vector<Element> AssistantThinkingRenderer::render(const DisplayMessage& msg,
                                                        const RendererContext& ctx) {
    std::vector<Element> elems;

    if (ctx.verboseTools) {
        // Expanded: show full thinking text
        elems.push_back(hbox({
            text("∴ ") | color(Color::Magenta),
            text("Thinking") | bold | color(Color::Magenta),
        }));
        std::istringstream thinkStream(msg.thinking.text);
        String thinkLine;
        while (std::getline(thinkStream, thinkLine)) {
            elems.push_back(hbox({
                text("  ▎ ") | color(Color::Magenta) | dim,
                text(thinkLine) | dim | color(Color::GrayLight),
            }));
        }
    } else {
        // Collapsed: summary only
        String summary = msg.thinking.text;
        if (summary.size() > 80) summary = summary.substr(0, 77) + "...";
        elems.push_back(hbox({
            text("∴ ") | color(Color::Magenta) | dim,
            text("Thinking") | dim | color(Color::Magenta),
            text("  " + summary) | dim | color(Color::GrayLight),
            text(" (ctrl+o)") | dim | color(Color::GrayDark),
        }));
    }

    return elems;
}

} // namespace claude
