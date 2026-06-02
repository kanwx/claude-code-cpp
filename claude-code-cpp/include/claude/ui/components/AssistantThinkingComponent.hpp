#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

/// Renders thinking block messages (AssistantThinking) with expand/collapse.
/// Collapsed: one-line summary with border box.
/// Expanded: full content with line-by-line rendering in a bordered window.
class AssistantThinkingComponent : public MessageComponent {
public:
    AssistantThinkingComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::AssistantThinking;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
