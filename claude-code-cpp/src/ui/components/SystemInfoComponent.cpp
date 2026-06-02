#ifdef HAS_FTXUI

#include <claude/ui/components/SystemInfoComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element SystemInfoComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("※ ") | color(MacSky),
        text(msg_.text) | dim | color(MacCream),
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
