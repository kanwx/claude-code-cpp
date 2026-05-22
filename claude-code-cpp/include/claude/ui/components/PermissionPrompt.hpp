#pragma once

#include "../../permission/PermissionTypes.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace claude::ui {

/// Permission prompt component — modal overlay asking user to allow/deny tool execution.
class PermissionPromptComponent {
public:
    struct Options {
        String toolName;
        String activity;
        std::vector<String> choices = {"Yes", "Yes always", "No", "No always"};
    };

    explicit PermissionPromptComponent(const Options& opts);

    /// Build the FTXUI component
    ftxui::Component build();

    /// Get the user's selection (-1 = none yet)
    int selectedIndex() const { return selectedIndex_; }

    /// Map selection index to PermissionChoice
    PermissionChoice toPermissionChoice() const;

private:
    Options options_;
    int selectedIndex_ = -1;
    int focusedIndex_ = 0;

    ftxui::Element render();
    bool handleEvent(ftxui::Event event);
};

} // namespace claude::ui
