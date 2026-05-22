#pragma once

#include "MessageRenderer.hpp"
#include "claude/ui/FtxuiMarkdown.hpp"

namespace claude {

/// Renders AssistantText messages via FtxuiMarkdown with the orange dot prefix.
class AssistantTextRenderer : public MessageRenderer {
public:
    std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                        const RendererContext& ctx) override;
    DisplayMessage::Type targetType() const override {
        return DisplayMessage::Type::AssistantText;
    }
};

/// Renders AssistantThinking messages with expand/collapse and ctrl+o hint.
class AssistantThinkingRenderer : public MessageRenderer {
public:
    std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                        const RendererContext& ctx) override;
    DisplayMessage::Type targetType() const override {
        return DisplayMessage::Type::AssistantThinking;
    }
};

} // namespace claude
