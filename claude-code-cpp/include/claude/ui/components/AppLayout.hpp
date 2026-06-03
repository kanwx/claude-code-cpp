#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <claude/ui/components/HeaderBar.hpp>
#include <claude/ui/components/ContentArea.hpp>
#include <claude/ui/RenderContext.hpp>
#include <string>
#include <vector>

namespace claude::ui {

struct InputState {
    std::string text;
    size_t cursorPos = 0;
    bool streaming = false;
};

struct FooterState {
    std::string modeIndicator;
    std::string hintText;
    bool authenticated = false;
    bool modeHintDismissed = false;
    bool isStreaming = false;
};

struct AppLayoutState {
    HeaderState header;
    ContentState content;
    InputState input;
    FooterState footer;
    // Permission overlay
    bool permissionActive = false;
    std::string permissionToolName;
    std::string permissionActivity;
    std::string permissionDescription;
    int permissionFocusedIndex = 0;
    // Completions
    std::vector<std::string> completions;
    size_t completionIndex = 0;
    // Scroll state (shared with event handler)
    bool autoScroll = true;
    float scrollRatio = 1.0f;
    // Tick counter for animations
    int tickCounter = 0;
    // Verbose tools toggle
    bool verboseTools = false;
    // Last completion input (for cycling detection)
    std::string lastCompletionInput;
    // Text selection state (Shift+drag)
    bool selectionActive = false;
    int selectionStartY = 0;
    int selectionEndY = 0;
};

ftxui::Component AppLayoutComponent(AppLayoutState& state, const RenderContext& ctx);

} // namespace claude::ui

#endif // HAS_FTXUI
