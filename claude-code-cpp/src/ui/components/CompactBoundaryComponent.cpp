#ifdef HAS_FTXUI

#include <claude/ui/components/CompactBoundaryComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element CompactBoundaryComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("  ── context compacted ──") | dim | color(MacShadow),
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
