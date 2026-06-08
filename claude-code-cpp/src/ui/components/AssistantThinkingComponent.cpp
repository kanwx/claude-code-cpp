#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantThinkingComponent.hpp>
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
        // Expanded: "∴ Thinking…" header + dim content at paddingLeft=2
        std::vector<Element> lines;
        lines.push_back(hbox({
            text("∴ ") | color(thinkingColor) | dim,
            text("Thinking…") | dim | color(thinkingColor),
        }));

        std::istringstream stream(msg_.thinking.text);
        String line;
        while (std::getline(stream, line)) {
            lines.push_back(hbox({
                text("  ") | dim,
                text(line) | dim | color(thinkingColor),
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
