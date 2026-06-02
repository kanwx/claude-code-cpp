#ifdef HAS_FTXUI

#include <claude/ui/components/StreamingText.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Component StreamingTextComponent(StreamingState& state) {
    return ftxui::Renderer([&state] {
        using namespace ftxui;

        if (state.text.empty()) {
            return emptyElement();
        }

        // Render streaming text using incremental markdown renderer
        // For component-level, we use the static FtxuiMarkdown::render()
        // which gives us the current snapshot of the streaming text.
        auto mdBlocks = FtxuiMarkdown::render(state.text);

        Elements els;
        bool firstElem = true;
        for (auto& elem : mdBlocks) {
            if (firstElem) {
                // Prefix first element with assistant marker
                els.push_back(hbox({
                    text("● ") | color(MacSky),
                    std::move(elem) | flex,
                }));
                firstElem = false;
            } else {
                els.push_back(std::move(elem));
            }
        }

        // Blinking cursor at end
        bool cursorVisible = (state.tickCounter % 4) < 2;
        Color cursorColor = cursorVisible ? MacPeach : MacCream;
        els.push_back(hbox({
            text("  ◉") | color(cursorColor) | blink,
        }));

        return vbox(std::move(els));
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
