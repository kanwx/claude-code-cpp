#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/IPermissionRenderer.hpp>

namespace claude::ui {

class FileReadPermissionRenderer : public IPermissionRenderer {
public:
    ftxui::Element renderPrompt(const PermissionRequest& req,
                                const RenderContext& ctx) override;
    std::string getActivityDescription(const PermissionRequest& req) override;
    std::vector<ftxui::Element> renderDetailLines(
        const PermissionRequest& req, const RenderContext& ctx) override;
};

} // namespace claude::ui
#endif // HAS_FTXUI
