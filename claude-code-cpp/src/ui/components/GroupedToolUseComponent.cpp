#ifdef HAS_FTXUI

#include <claude/ui/components/GroupedToolUseComponent.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element GroupedToolUseComponent::OnRender() {
    using namespace ftxui;

    auto& tools = msg_.groupedTools;
    if (tools.empty()) return text("") | dim;

    // Delegate to IToolRenderer::renderGroupedToolUse()
    auto* renderer = ToolRendererRegistry::instance().getRenderer(tools[0].toolName);

    // Build ToolUseBlock vector from ToolUseRenderData
    std::vector<ToolUseBlock> blocks;
    blocks.reserve(tools.size());
    for (auto& td : tools) {
        ToolUseBlock b;
        b.toolId = td.toolUseId;
        b.toolName = td.toolName;
        b.input = td.arguments;
        blocks.push_back(std::move(b));
    }

    return renderer->renderGroupedToolUse(blocks, ctx_);
}

} // namespace claude::ui

#endif // HAS_FTXUI
