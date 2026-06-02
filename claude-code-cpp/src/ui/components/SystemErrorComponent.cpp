#ifdef HAS_FTXUI

#include <claude/ui/components/SystemErrorComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element SystemErrorComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("✗ ") | color(MacRose) | bold,
        text(msg_.text) | color(MacRose),
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
