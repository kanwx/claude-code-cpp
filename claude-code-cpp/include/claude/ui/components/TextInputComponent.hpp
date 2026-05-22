#pragma once

#include "../../repl/Completer.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <vector>

namespace claude::ui {

/// Text input component with Tab completion support.
/// Wraps FTXUI's Input component and adds completion dropdown.
class TextInputComponent {
public:
    using OnSubmit = std::function<void(const String&)>;

    explicit TextInputComponent(ReplCompleter& completer);

    /// Build the FTXUI component
    ftxui::Component build();

    /// Get current input text
    String text() const { return input_; }

    /// Clear input
    void clear();

    /// Set placeholder text
    void setPlaceholder(const String& placeholder);

private:
    ReplCompleter& completer_;
    String input_;
    String placeholder_;
    bool completionVisible_ = false;
    int completionIndex_ = 0;
    std::vector<String> currentCompletions_;
    ftxui::Component inputComponent_;

    ftxui::Element render();
    bool handleEvent(ftxui::Event event);
    void updateCompletions();
};

} // namespace claude::ui
