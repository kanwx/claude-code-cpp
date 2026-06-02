#ifdef HAS_FTXUI

#include <claude/ui/components/TurnDurationComponent.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element TurnDurationComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("  ") | dim,
        text(msg_.text) | dim,
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
