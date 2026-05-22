#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <functional>

namespace claude::ui {

/// Generic modal overlay component — renders a centered dialog over the main UI.
class ModalOverlayComponent {
public:
    using OnClose = std::function<void()>;

    /// Build a modal component wrapping the given content
    static ftxui::Component build(
        ftxui::Component child,
        bool& shown,
        OnClose onClose = nullptr
    );
};

} // namespace claude::ui
