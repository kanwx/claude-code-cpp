#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

/// Renders collapsed read/search tool group messages (CollapsedReadSearch).
/// Shows a summary line from CollapsedToolGroup::summaryText() with
/// expand/collapse hint.
class CollapsedReadSearchComponent : public MessageComponent {
public:
    CollapsedReadSearchComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::CollapsedReadSearch;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
