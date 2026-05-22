#include <claude/ui/components/ModalOverlay.hpp>
#include <ftxui/component/event.hpp>

namespace claude::ui {

ftxui::Component ModalOverlayComponent::build(
    ftxui::Component child,
    bool& shown,
    OnClose onClose
) {
    auto renderer = ftxui::Renderer(child, [child, &shown]() {
        if (!shown) return ftxui::emptyElement();

        return ftxui::vbox({
            ftxui::filler(),
            ftxui::hbox({
                ftxui::filler(),
                child->Render() | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70),
                ftxui::filler(),
            }),
            ftxui::filler(),
        });
    });

    return renderer | ftxui::CatchEvent([&shown, onClose](ftxui::Event event) {
        if (!shown) return false;
        if (event == ftxui::Event::Escape) {
            shown = false;
            if (onClose) onClose();
            return true;
        }
        return false;
    });
}

} // namespace claude::ui
