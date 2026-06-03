#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/IToolRenderer.hpp>

namespace claude::ui {

class WebSearchToolRenderer : public IToolRenderer {
public:
    ftxui::Element renderToolUse(const ToolUseBlock& tool,
                                 const RenderContext& ctx) override;
    std::string renderToolUseAnsi(const ToolUseBlock& tool) override;
    ftxui::Element renderToolResult(const ToolResultBlock& result,
                                    const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    std::string renderToolResultAnsi(const ToolResultBlock& result,
                                     const ToolUseBlock& tool) override;
    ftxui::Element renderToolError(const ToolResultBlock& result,
                                   const ToolUseBlock& tool,
                                   const RenderContext& ctx) override;
    std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                    const ToolUseBlock& tool) override;
    ftxui::Element renderToolRejected(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolRejectedAnsi(const ToolUseBlock& tool) override;
    ftxui::Element renderToolCanceled(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolCanceledAnsi(const ToolUseBlock& tool) override;
    ftxui::Element renderToolProgress(const ToolUseBlock& tool,
                                      const std::string& progress,
                                      const RenderContext& ctx) override;
    ftxui::Element renderToolQueued(const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    ftxui::Element renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) override;
    std::string getToolUseSummary(const ToolUseBlock& tool) override;
    std::string userFacingName(const ToolUseBlock& tool) override;
    bool isCollapsible() const override { return true; }
    bool isResultTruncatable(const ToolResultBlock& result) const override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
