#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <string>

namespace claude::ui {

struct ThinkingState {
    std::string summary;
    std::string runningVerb;   // random verb per turn, e.g. "Wandering"
    int elapsedSeconds = 0;    // wall-clock elapsed since turn start
    int tokenEstimate = 0;     // streamingText_.size() / 4
    bool active = false;
    bool stalled = false;
    int tickCounter = 0;
};

ftxui::Component ThinkingIndicatorComponent(ThinkingState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
