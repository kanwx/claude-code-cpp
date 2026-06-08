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

    // Per-message expand state for collapsible tool results.
    // Set by tool result components before delegating to ToolRendererRegistry.
    // When true, renderers should show full content instead of compact summary.
    bool toolResultExpanded = false;

    // Index of the focused collapsible tool result (for visual indicator).
    // -1 means no collapsible result is focused.
    int collapsibleFocusIndex = -1;

    RenderContext(const ThemeColors& t) : theme(t) {}
};

} // namespace claude::ui

#endif // HAS_FTXUI
