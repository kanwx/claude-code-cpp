#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

/// Renders sub-agent/parallel task progress messages (AgentProgress).
/// Displays a tree-style view with agent type badge, description, and
/// running/done status.
class AgentProgressComponent : public MessageComponent {
public:
    AgentProgressComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::AgentProgress;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
