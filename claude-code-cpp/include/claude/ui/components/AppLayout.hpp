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

struct StatusState {
    std::string modelName;
    std::string turnDuration;  // "3m 53s"
    std::string contextStr;    // "8.2K/200K ctx"
    std::string tokenStr;      // "12.4K out"
    std::string costStr;       // "$0.04"
    std::string systemNotice;  // Config hints, recap text
    bool isStreaming = false;
    bool visible = false;      // Only shown when there's content
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
    StatusState status;
    InputState input;
    FooterState footer;
    // Permission overlay
    bool permissionActive = false;
    std::string permissionToolName;
    std::string permissionActivity;
    std::string permissionDescription;
    int permissionFocusedIndex = 0;
    // Tab-to-amend feedback
    bool permissionFeedbackActive = false;
    std::string permissionFeedbackText;
    size_t permissionFeedbackCursorPos = 0;
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
    // Collapsible tool result focus tracking
    // Index into the list of collapsible tool results in the message list.
    // -1 means no collapsible result is focused.
    int collapsibleFocusIndex = -1;
    // Total count of collapsible tool results in the current message list
    int collapsibleCount = 0;
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
