#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/IPermissionRenderer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace claude::ui {

class PermissionRendererRegistry {
public:
    static PermissionRendererRegistry& instance();

    void registerRenderer(const std::string& toolName,
                          std::unique_ptr<IPermissionRenderer> renderer);

    IPermissionRenderer* getRenderer(const std::string& toolName) const;
    IPermissionRenderer* getFallbackRenderer() const;

    PermissionRendererRegistry(const PermissionRendererRegistry&) = delete;
    PermissionRendererRegistry& operator=(const PermissionRendererRegistry&) = delete;

private:
    PermissionRendererRegistry();
    std::unordered_map<std::string, std::unique_ptr<IPermissionRenderer>> renderers_;
    std::unique_ptr<IPermissionRenderer> fallback_;
};

} // namespace claude::ui

#endif // HAS_FTXUI
