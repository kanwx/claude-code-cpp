#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>
#include <vector>

namespace claude::ui {

ftxui::Component MessageListComponent(
    const std::vector<DisplayMessage>& messages,
    const RenderContext& ctx);

/// Internal: render message list as a single Element.
/// Used by both MessageListComponent and ContentAreaComponent.
ftxui::Element RenderMessageList(
    const std::vector<DisplayMessage>* messages,
    const RenderContext* ctx);

} // namespace claude::ui

#endif // HAS_FTXUI
