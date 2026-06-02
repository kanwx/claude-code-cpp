#ifdef HAS_FTXUI

#include <claude/ui/components/UserPromptComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element UserPromptComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("❯ ") | color(MacSage) | bold,
        paragraph(msg_.text) | bold | flex,
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
