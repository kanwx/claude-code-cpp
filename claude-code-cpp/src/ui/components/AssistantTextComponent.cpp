#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantTextComponent.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <claude/console/AnsiStyle.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element AssistantTextComponent::OnRender() {
    using namespace ftxui;
    if (!msg_.text.empty()) {
        auto mdBlocks = FtxuiMarkdown::render(msg_.text);
        if (!mdBlocks.empty()) {
            // Prepend ⏺/● prefix to the first line of assistant text,
            // matching the TS Claude Code layout (width-2 column, right-aligned)
            mdBlocks[0] = hbox({
                text(String(" ") + AnsiStyle::ASSISTANT_PREFIX + " ") | color(MacCream),
                std::move(mdBlocks[0]) | flex,
            });
        }
        return vbox(std::move(mdBlocks));
    }
    return text("");
}

} // namespace claude::ui

#endif // HAS_FTXUI
