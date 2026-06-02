#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <string>

namespace claude::ui {

struct StreamingState {
    std::string text;
    int tickCounter = 0;
};

ftxui::Component StreamingTextComponent(StreamingState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
