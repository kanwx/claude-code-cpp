#pragma once
#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>
#include <string>
#include <vector>

namespace claude::ui {

class IToolRenderer {
public:
    virtual ~IToolRenderer() = default;

    // Tool invocation UI
    virtual ftxui::Element renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) = 0;
    virtual std::string renderToolUseAnsi(const ToolUseBlock& tool) = 0;

    // Success result UI
    virtual ftxui::Element renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) = 0;
    virtual std::string renderToolResultAnsi(const ToolResultBlock& result,
                                             const ToolUseBlock& tool) = 0;

    // Error result UI
    virtual ftxui::Element renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) = 0;
    virtual std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                            const ToolUseBlock& tool) = 0;

    // Rejected UI
    virtual ftxui::Element renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) = 0;
    virtual std::string renderToolRejectedAnsi(const ToolUseBlock& tool) = 0;

    // Canceled UI
    virtual ftxui::Element renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) = 0;
    virtual std::string renderToolCanceledAnsi(const ToolUseBlock& tool) = 0;

    // Progress while tool is running
    virtual ftxui::Element renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) = 0;

    // Queued waiting UI
    virtual ftxui::Element renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) = 0;

    // Grouped parallel same-type calls
    virtual ftxui::Element renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) = 0;

    // Compact summary text
    virtual std::string getToolUseSummary(const ToolUseBlock& tool) = 0;

    // User-facing display name
    virtual std::string userFacingName(const ToolUseBlock& tool) = 0;

    // Classification
    virtual bool isCollapsible() const = 0;
    virtual bool isResultTruncatable(const ToolResultBlock& result) const = 0;
};

} // namespace claude::ui

#endif // HAS_FTXUI
