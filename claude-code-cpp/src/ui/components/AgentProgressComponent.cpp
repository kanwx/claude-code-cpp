#ifdef HAS_FTXUI

#include <claude/ui/components/AgentProgressComponent.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Element AgentProgressComponent::OnRender() {
    using namespace ftxui;

    auto agentType = msg_.toolUse.toolName.empty() ? String("Agent") : msg_.toolUse.toolName;
    auto desc = msg_.text.empty() ? String("Working...") : msg_.text;
    auto running = msg_.expanded;   // makeAgentProgress stores running state in expanded
    auto tokens = msg_.toolResult.result;  // "N tokens"

    auto badgeBg = toolBgColor("Agent");
    auto badgeFg = toolFgColor("Agent");

    return vbox({
        hbox({
            text("├── ") | color(MacShadow),
            text(" " + agentType + " ") | bold | bgcolor(badgeBg) | color(badgeFg),
            text(" " + desc) | color(MacCream),
            text(" · ") | color(MacShadow),
            text(tokens) | dim | color(MacCream),
        }),
        hbox({
            text("│  ") | color(MacShadow),
            text(running ? "⏻  Working..." : "Done") | color(running ? MacGold : MacMint),
        }),
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
