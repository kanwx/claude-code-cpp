#include <claude/ui/components/PermissionPrompt.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

PermissionPromptComponent::PermissionPromptComponent(const Options& opts)
    : options_(opts)
{}

ftxui::Component PermissionPromptComponent::build() {
    return ftxui::Renderer([this]() { return render(); })
        | ftxui::CatchEvent([this](ftxui::Event event) { return handleEvent(event); });
}

ftxui::Element PermissionPromptComponent::render() {
    using namespace ftxui;

    std::vector<Element> items;
    items.push_back(text("  Permission Request") | bold | color(Color::Yellow));
    items.push_back(text(""));
    items.push_back(text("  Tool: " + options_.toolName) | color(Color::Cyan));
    items.push_back(text("  " + options_.activity));
    items.push_back(text(""));

    for (size_t i = 0; i < options_.choices.size(); ++i) {
        if (static_cast<int>(i) == focusedIndex_) {
            items.push_back(text("  > " + options_.choices[i]) | bold | inverted);
        } else {
            items.push_back(text("    " + options_.choices[i]));
        }
    }

    return vbox(std::move(items)) | border | size(WIDTH, LESS_THAN, 60);
}

bool PermissionPromptComponent::handleEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowUp) {
        focusedIndex_ = (focusedIndex_ - 1 + static_cast<int>(options_.choices.size()))
                       % static_cast<int>(options_.choices.size());
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        focusedIndex_ = (focusedIndex_ + 1) % static_cast<int>(options_.choices.size());
        return true;
    }
    if (event == ftxui::Event::Return) {
        selectedIndex_ = focusedIndex_;
        return true;
    }
    return false;
}

PermissionChoice PermissionPromptComponent::toPermissionChoice() const {
    switch (selectedIndex_) {
        case 0:  return PermissionChoice::AllowOnce;
        case 1:  return PermissionChoice::AlwaysAllow;
        case 2:  return PermissionChoice::DenyOnce;
        case 3:  return PermissionChoice::AlwaysDeny;
        default: return PermissionChoice::DenyOnce;
    }
}

} // namespace claude::ui
