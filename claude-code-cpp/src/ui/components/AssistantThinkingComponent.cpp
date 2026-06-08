#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantThinkingComponent.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
#include <sstream>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element AssistantThinkingComponent::OnRender() {
    using namespace ftxui;

    // Use dim/gray for all thinking text — matching TS style
    const auto thinkingColor = MacShadow;  // dim gray, not MacLavender

    if (msg_.expanded || ctx_.verbose) {
        // Expanded: "∴ Thinking…" header + dim markdown content at paddingLeft=2
        std::vector<Element> lines;
        lines.push_back(hbox({
            text("∴ ") | color(thinkingColor) | dim,
            text("Thinking…") | dim | color(thinkingColor),
        }));

        // Render thinking content as dim markdown (matching TS dimColor behavior)
        auto mdElements = FtxuiMarkdown::render(msg_.thinking.text, RenderOptions{.dimAll = true});
        for (auto& elem : mdElements) {
            lines.push_back(hbox({
                text("  ") | dim,
                std::move(elem),
            }));
        }

        return vbox(std::move(lines));
    } else {
        // Collapsed: "∴ Thinking  (Ctrl+O to expand)" — all dim, capital O
        return hbox({
            text("∴ ") | color(thinkingColor) | dim,
            text("Thinking") | dim | color(thinkingColor),
            text("  (Ctrl+O to expand)") | dim | color(thinkingColor),
        });
    }
}

} // namespace claude::ui

#endif // HAS_FTXUI
