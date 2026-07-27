#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/stream/ContentBlock.hpp>
#include <claude/ui/RenderContext.hpp>
#include <claude/ui/components/StreamingText.hpp>
#include <claude/ui/components/ThinkingIndicator.hpp>
#include <vector>

namespace claude::ui {

struct ContentState {
    const std::vector<DisplayMessage>* messages = nullptr;
    const std::vector<ContentBlock>* contentBlocks = nullptr;
    StreamingState streaming;
    ThinkingState thinking;
    bool autoScroll = true;
    float scrollRatio = 1.0f;
    int messagesAbove = 0;
};

ftxui::Component ContentAreaComponent(ContentState& state, const RenderContext& ctx);

} // namespace claude::ui

#endif // HAS_FTXUI
