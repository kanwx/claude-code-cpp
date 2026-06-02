#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <string>

namespace claude::ui {

struct ThinkingState {
    std::string summary;
    bool active = false;
    bool stalled = false;
    int tickCounter = 0;
};

ftxui::Component ThinkingIndicatorComponent(ThinkingState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
