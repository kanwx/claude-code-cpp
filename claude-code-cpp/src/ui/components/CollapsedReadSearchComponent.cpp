#ifdef HAS_FTXUI

#include <claude/ui/components/CollapsedReadSearchComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element CollapsedReadSearchComponent::OnRender() {
    using namespace ftxui;

    auto& g = msg_.collapsedGroup;
    auto summary = g.summaryText();

    return hbox({
        text("  ⎿ ") | color(MacSky),
        text(summary) | color(MacSky),
        text(g.active ? "" : " ") | dim,
        text("[Ctrl+O to expand]") | dim | color(MacShadow),
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
