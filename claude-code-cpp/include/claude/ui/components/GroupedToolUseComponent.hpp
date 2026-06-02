#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

/// Renders grouped same-type tool use messages (GroupedToolUse) by delegating
/// to IToolRenderer::renderGroupedToolUse() via ToolRendererRegistry.
/// Builds a std::vector<ToolUseBlock> from msg_.groupedTools before delegating.
class GroupedToolUseComponent : public MessageComponent {
public:
    GroupedToolUseComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::GroupedToolUse;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
