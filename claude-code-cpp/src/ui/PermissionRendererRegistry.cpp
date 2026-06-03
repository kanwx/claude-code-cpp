#ifdef HAS_FTXUI

#include <claude/ui/PermissionRendererRegistry.hpp>
#include <claude/ui/permissions/DefaultPermissionRenderer.hpp>

namespace claude::ui {

PermissionRendererRegistry& PermissionRendererRegistry::instance() {
    static PermissionRendererRegistry registry;
    return registry;
}

PermissionRendererRegistry::PermissionRendererRegistry()
    : fallback_(std::make_unique<DefaultPermissionRenderer>()) {}

void PermissionRendererRegistry::registerRenderer(
    const std::string& toolName,
    std::unique_ptr<IPermissionRenderer> renderer) {
    renderers_[toolName] = std::move(renderer);
}

IPermissionRenderer* PermissionRendererRegistry::getRenderer(
    const std::string& toolName) const {
    auto it = renderers_.find(toolName);
    if (it != renderers_.end()) return it->second.get();
    return fallback_.get();
}

IPermissionRenderer* PermissionRendererRegistry::getFallbackRenderer() const {
    return fallback_.get();
}

} // namespace claude::ui

#endif // HAS_FTXUI
