#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>

namespace claude::ui {

/// Wraps tool result content with collapsed/expanded rendering.
///
/// Collapsed: summary line + [Ctrl+O to expand] hint
/// Expanded:  full content   + [Ctrl+O to collapse] hint
///
/// This is a render-only component — event handling (Ctrl+O, [/]) is
/// done centrally in FtxuiRepl's CatchEvent wrapper which toggles
/// the DisplayMessage::expanded flag that this component reads.
class CollapsibleToolResult : public ftxui::ComponentBase {
public:
    /// @param summary       Compact one-line summary (e.g. "Read 42 lines")
    /// @param fullContent   Pre-rendered full content element
    /// @param expanded      Whether to show expanded view
    /// @param focused       Whether this result has keyboard focus
    CollapsibleToolResult(const std::string& summary,
                          ftxui::Element fullContent,
                          bool expanded = false,
                          bool focused = false);

    ftxui::Element OnRender() override;

    /// Check if a tool result message type is collapsible
    static bool isCollapsibleToolResult(const std::string& toolName);

private:
    std::string summary_;
    ftxui::Element fullContent_;
    bool expanded_;
    bool focused_;
};

} // namespace claude::ui

#endif // HAS_FTXUI
