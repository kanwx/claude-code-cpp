#pragma once

#include <claude/ui/IToolRenderer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace claude::ui {

class ToolRendererRegistry {
public:
    static ToolRendererRegistry& instance();

    void registerRenderer(const std::string& toolName,
                          std::unique_ptr<IToolRenderer> renderer);

    IToolRenderer* getRenderer(const std::string& toolName) const;
    IToolRenderer* getFallbackRenderer() const;

    ToolRendererRegistry(const ToolRendererRegistry&) = delete;
    ToolRendererRegistry& operator=(const ToolRendererRegistry&) = delete;

private:
    ToolRendererRegistry();
    std::unordered_map<std::string, std::unique_ptr<IToolRenderer>> renderers_;
    std::unique_ptr<IToolRenderer> fallback_;
};

} // namespace claude::ui
