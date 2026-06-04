#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantThinkingComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
#include <sstream>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element AssistantThinkingComponent::OnRender() {
    using namespace ftxui;

    if (msg_.expanded || ctx_.verbose) {
        // Expanded: simple content display
        std::vector<Element> lines;
        lines.push_back(hbox({
            text("∴ ") | color(MacLavender) | dim,
            text("Thinking") | bold | color(MacLavender),
        }));

        std::istringstream stream(msg_.thinking.text);
        String line;
        while (std::getline(stream, line)) {
            lines.push_back(hbox({
                text("  ") | color(MacLavender) | dim,
                text(line) | dim | color(MacCream),
            }));
        }

        return vbox(std::move(lines));
    } else {
        // Collapsed: simple one-line indicator matching TS style
        return hbox({
            text("∴ ") | color(MacLavender) | dim,
            text("Thinking") | color(MacLavender),
            text(" (ctrl+o)") | dim | color(MacShadow),
        });
    }
}

} // namespace claude::ui

#endif // HAS_FTXUI
