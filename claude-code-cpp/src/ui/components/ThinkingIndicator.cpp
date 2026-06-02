#ifdef HAS_FTXUI

#include <claude/ui/components/ThinkingIndicator.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Component ThinkingIndicatorComponent(ThinkingState& state) {
    return ftxui::Renderer([&state] {
        using namespace ftxui;

        if (!state.active) {
            return emptyElement();
        }

        // Determine color based on stall state
        Color thinkColor = state.stalled ? MacRose : MacLavender;

        // Glimmer effect: pulsing brightness on spinner
        bool glimmerPhase = (state.tickCounter % 20) < 10;

        std::vector<Element> thinkingElems;
        thinkingElems.push_back(spinner(1, state.tickCounter) | color(thinkColor));
        thinkingElems.push_back(
            text(state.stalled ? " Thinking (stalled)" : " Thinking")
            | bold | color(thinkColor)
        );
        if (glimmerPhase) {
            thinkingElems.push_back(text(" ●") | color(thinkColor) | dim);
        }
        if (!state.summary.empty()) {
            thinkingElems.push_back(text("  ") | dim);
            std::string summary = state.summary;
            if (summary.size() > 60) {
                summary = "..." + summary.substr(summary.size() - 57);
            }
            thinkingElems.push_back(text(summary) | dim | color(MacCream));
        }
        thinkingElems.push_back(text(" (ctrl+o)") | dim | color(MacShadow));

        return hbox(std::move(thinkingElems));
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
