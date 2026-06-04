#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace claude::ui {

struct StreamingState {
    std::string text;
    int tickCounter = 0;
    // Pre-rendered elements from StreamingRenderer (avoids full reparse each frame)
    std::vector<ftxui::Element> cachedElements;
};

ftxui::Component StreamingTextComponent(StreamingState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
