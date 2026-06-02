#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

/// Renders tool use invocation messages (AssistantToolUse) by delegating
/// to IToolRenderer via ToolRendererRegistry.
class ToolUseComponent : public MessageComponent {
public:
    ToolUseComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::AssistantToolUse;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
