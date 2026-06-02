#ifdef HAS_FTXUI

#include <claude/ui/ToolRendererRegistry.hpp>
#include "ui/renderers/DefaultToolRenderer.hpp"

namespace claude::ui {

ToolRendererRegistry& ToolRendererRegistry::instance() {
    static ToolRendererRegistry registry;
    return registry;
}

ToolRendererRegistry::ToolRendererRegistry()
    : fallback_(std::make_unique<DefaultToolRenderer>()) {}

void ToolRendererRegistry::registerRenderer(
    const std::string& toolName,
    std::unique_ptr<IToolRenderer> renderer) {
    renderers_[toolName] = std::move(renderer);
}

IToolRenderer* ToolRendererRegistry::getRenderer(
    const std::string& toolName) const {
    auto it = renderers_.find(toolName);
    if (it != renderers_.end()) return it->second.get();
    return fallback_.get();
}

IToolRenderer* ToolRendererRegistry::getFallbackRenderer() const {
    return fallback_.get();
}

} // namespace claude::ui

#endif // HAS_FTXUI
