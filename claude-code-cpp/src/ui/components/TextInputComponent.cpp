#include <claude/ui/components/TextInputComponent.hpp>
#include <claude/core/Types.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

TextInputComponent::TextInputComponent(ReplCompleter& completer)
    : completer_(completer)
{}

ftxui::Component TextInputComponent::build() {
    inputComponent_ = ftxui::Input(&input_, placeholder_);

    auto renderer = ftxui::Renderer(inputComponent_, [this]() { return render(); });
    return renderer | ftxui::CatchEvent([this](ftxui::Event event) { return handleEvent(event); });
}

void TextInputComponent::clear() {
    input_.clear();
    completionVisible_ = false;
    currentCompletions_.clear();
}

void TextInputComponent::setPlaceholder(const String& placeholder) {
    placeholder_ = placeholder;
}

ftxui::Element TextInputComponent::render() {
    auto inputElem = inputComponent_->Render();

    if (!completionVisible_ || currentCompletions_.empty()) {
        return ftxui::hbox(ftxui::text(std::string("> ")), inputElem);
    }

    std::vector<ftxui::Element> completionItems;
    for (size_t i = 0; i < currentCompletions_.size() && i < 10; ++i) {
        std::string label = std::string("  ") + currentCompletions_[i];
        if (static_cast<int>(i) == completionIndex_) {
            completionItems.push_back(ftxui::text(label) | ftxui::inverted);
        } else {
            completionItems.push_back(ftxui::text(label) | ftxui::dim);
        }
    }

    return ftxui::vbox({
        ftxui::hbox(ftxui::text(std::string("> ")), inputElem),
        ftxui::vbox(std::move(completionItems)) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 50),
    });
}

bool TextInputComponent::handleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Tab) {
        updateCompletions();
        if (!currentCompletions_.empty()) {
            completionVisible_ = true;
            if (completionIndex_ < 0) completionIndex_ = 0;
            String prefix = completer_.commonPrefix(input_);
            if (!prefix.empty() && prefix != input_) {
                input_ = prefix;
            }
        }
        return true;
    }

    if (completionVisible_ && !currentCompletions_.empty()) {
        if (event == ftxui::Event::ArrowDown) {
            completionIndex_ = (completionIndex_ + 1) % static_cast<int>(currentCompletions_.size());
            return true;
        }
        if (event == ftxui::Event::ArrowUp) {
            completionIndex_ = (completionIndex_ - 1 + static_cast<int>(currentCompletions_.size()))
                             % static_cast<int>(currentCompletions_.size());
            return true;
        }
        if (event == ftxui::Event::Return && completionIndex_ >= 0) {
            input_ = currentCompletions_[completionIndex_];
            completionVisible_ = false;
            return true;
        }
    }

    if (event == ftxui::Event::Escape) {
        completionVisible_ = false;
        return true;
    }

    completionVisible_ = false;
    return false;
}

void TextInputComponent::updateCompletions() {
    completer_.updateCompletions(input_, input_.size());
    currentCompletions_ = completer_.currentCompletions();
}

} // namespace claude::ui
