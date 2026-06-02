#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component_base.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>

namespace claude::ui {

class MessageComponent : public ftxui::ComponentBase {
public:
    MessageComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : msg_(msg), ctx_(ctx) {}

    virtual DisplayMessage::Type messageType() const = 0;

protected:
    const DisplayMessage& msg_;
    RenderContext ctx_;
};

} // namespace claude::ui

#endif // HAS_FTXUI
