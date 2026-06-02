#ifdef HAS_FTXUI

#include <claude/ui/components/ToolUseComponent.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element ToolUseComponent::OnRender() {
    auto* renderer = ToolRendererRegistry::instance().getRenderer(msg_.toolUse.toolName);
    return renderer->renderToolUse(msg_.toolUse, ctx_);
}

} // namespace claude::ui

#endif // HAS_FTXUI
