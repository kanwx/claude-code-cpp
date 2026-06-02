// include/claude/ui/RenderContext.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>

namespace claude::ui {

struct ThemeColors;  // forward declaration — defined in FtxuiColors.hpp

struct RenderContext {
    bool verbose = false;
    bool isStreaming = false;
    int maxWidth = 80;
    int tickCounter = 0;
    const ThemeColors& theme;

    RenderContext(const ThemeColors& t) : theme(t) {}
};

} // namespace claude::ui

#endif // HAS_FTXUI
