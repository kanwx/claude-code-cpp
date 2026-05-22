#pragma once

#include "MessageRenderer.hpp"

namespace claude {

// Per-tool colors (shared between ToolUseRenderer and CollapsedToolRenderer)
ftxui::Color toolBgColor(const String& toolName);
ftxui::Color toolFgColor(const String& toolName);

/// Renders AssistantToolUse messages with colored badge, input summary, and diff output.
class ToolUseRenderer : public MessageRenderer {
public:
    std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                        const RendererContext& ctx) override;
    DisplayMessage::Type targetType() const override {
        return DisplayMessage::Type::AssistantToolUse;
    }

    /// Render a tool result with diff detection and colored +/- lines
    static std::vector<ftxui::Element> renderToolResult(const String& result,
                                                         const RendererContext& ctx);
};

/// Renders UserToolResult messages (paired to tool_use).
class ToolResultRenderer : public MessageRenderer {
public:
    std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                        const RendererContext& ctx) override;
    DisplayMessage::Type targetType() const override {
        return DisplayMessage::Type::UserToolResult;
    }
};

/// Renders CollapsedReadSearch messages (grouped read/search summary).
class CollapsedToolRenderer : public MessageRenderer {
public:
    std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                        const RendererContext& ctx) override;
    DisplayMessage::Type targetType() const override {
        return DisplayMessage::Type::CollapsedReadSearch;
    }
};

} // namespace claude
