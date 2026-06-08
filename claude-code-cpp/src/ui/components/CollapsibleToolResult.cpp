#ifdef HAS_FTXUI

#include <claude/ui/components/CollapsibleToolResult.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

CollapsibleToolResult::CollapsibleToolResult(const std::string& summary,
                                             ftxui::Element fullContent,
                                             bool expanded,
                                             bool focused)
    : summary_(summary)
    , fullContent_(std::move(fullContent))
    , expanded_(expanded)
    , focused_(focused) {}

ftxui::Element CollapsibleToolResult::OnRender() {
    using namespace ftxui;

    if (expanded_) {
        // Expanded: full content + [Ctrl+O to collapse] hint
        return vbox({
            std::move(fullContent_),
            hbox({
                text("  ⎿  ") | dim,
                text("[Ctrl+O to collapse]") | dim | color(MacShadow),
            }),
        });
    } else {
        // Collapsed: summary + [Ctrl+O to expand] hint
        auto hintColor = focused_ ? MacCream : MacShadow;
        return hbox({
            text("  ⎿  ") | dim,
            text(summary_) | dim,
            text(" [Ctrl+O to expand]") | dim | color(hintColor),
        });
    }
}

bool CollapsibleToolResult::isCollapsibleToolResult(const std::string& toolName) {
    auto* renderer = ToolRendererRegistry::instance().getRenderer(toolName);
    if (!renderer) return false;
    return renderer->isCollapsible();
}

} // namespace claude::ui

#endif // HAS_FTXUI
