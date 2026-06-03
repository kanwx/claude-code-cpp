#pragma once
#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>
#include <claude/ui/RenderContext.hpp>
#include <string>
#include <vector>

namespace claude::ui {

struct PermissionRequest {
    std::string toolName;
    std::string input;  // JSON string
    std::string activity;
};

class IPermissionRenderer {
public:
    virtual ~IPermissionRenderer() = default;

    virtual ftxui::Element renderPrompt(const PermissionRequest& req,
                                        const RenderContext& ctx) = 0;
    virtual std::string getActivityDescription(const PermissionRequest& req) = 0;
    virtual std::vector<ftxui::Element> renderDetailLines(
        const PermissionRequest& req, const RenderContext& ctx) {
        return {};
    }
};

} // namespace claude::ui

#endif // HAS_FTXUI
