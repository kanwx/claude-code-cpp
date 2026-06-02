#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantThinkingComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element AssistantThinkingComponent::OnRender() {
    using namespace ftxui;

    if (msg_.expanded || ctx_.verbose) {
        // Expanded: bordered box with full content
        std::vector<Element> lines;
        lines.push_back(hbox({
            text("╭─ 💭 Thinking ─") | color(MacLavender) | dim,
            filler() | color(MacLavender) | dim,
            text("╮") | color(MacLavender) | dim,
        }));

        std::istringstream stream(msg_.thinking.text);
        String line;
        while (std::getline(stream, line)) {
            lines.push_back(hbox({
                text("│ ") | color(MacLavender) | dim,
                text(line) | dim | color(MacCream),
                filler(),
                text(" │") | color(MacLavender) | dim,
            }));
        }

        lines.push_back(hbox({
            text("╰─ ✓ Done ─") | color(MacMint),
            filler() | color(MacLavender) | dim,
            text("╯") | color(MacLavender) | dim,
        }));

        return vbox(std::move(lines));
    } else {
        // Collapsed: one-line summary in a bordered box
        String summary = msg_.thinking.text;
        if (summary.size() > 80) summary = summary.substr(0, 77) + "...";
        return hbox({
            text("╭─ 💭 ") | color(MacLavender) | dim,
            text("Thinking") | color(MacLavender),
            text(" ─ ") | color(MacLavender) | dim,
            text(summary) | dim | color(MacCream),
            text(" (ctrl+o)") | dim | color(MacShadow),
            filler() | color(MacLavender) | dim,
            text("╮") | color(MacLavender) | dim,
        });
    }
}

} // namespace claude::ui

#endif // HAS_FTXUI
