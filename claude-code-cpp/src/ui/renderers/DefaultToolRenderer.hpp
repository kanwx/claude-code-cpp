#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/IToolRenderer.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/ActivityDescription.hpp>

namespace claude::ui {

/// Fallback renderer that produces current-style output for any tool.
/// Used when no tool-specific renderer is registered.
class DefaultToolRenderer : public IToolRenderer {
public:
    // Tool invocation UI
    ftxui::Element renderToolUse(const ToolUseBlock& tool,
                                 const RenderContext& ctx) override;
    std::string renderToolUseAnsi(const ToolUseBlock& tool) override;

    // Success result UI
    ftxui::Element renderToolResult(const ToolResultBlock& result,
                                    const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    std::string renderToolResultAnsi(const ToolResultBlock& result,
                                     const ToolUseBlock& tool) override;

    // Error result UI
    ftxui::Element renderToolError(const ToolResultBlock& result,
                                   const ToolUseBlock& tool,
                                   const RenderContext& ctx) override;
    std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                    const ToolUseBlock& tool) override;

    // Rejected UI
    ftxui::Element renderToolRejected(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolRejectedAnsi(const ToolUseBlock& tool) override;

    // Canceled UI
    ftxui::Element renderToolCanceled(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolCanceledAnsi(const ToolUseBlock& tool) override;

    // Progress while tool is running
    ftxui::Element renderToolProgress(const ToolUseBlock& tool,
                                      const std::string& progress,
                                      const RenderContext& ctx) override;

    // Queued waiting UI
    ftxui::Element renderToolQueued(const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;

    // Grouped parallel same-type calls
    ftxui::Element renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) override;

    // Compact summary text
    std::string getToolUseSummary(const ToolUseBlock& tool) override;

    // User-facing display name
    std::string userFacingName(const ToolUseBlock& tool) override;

    // Classification
    bool isCollapsible() const override;
    bool isResultTruncatable(const ToolResultBlock& result) const override;

private:
    /// Build a tool badge element with per-tool background color
    static ftxui::Element makeBadge(const std::string& toolName);
};

} // namespace claude::ui

#endif // HAS_FTXUI
