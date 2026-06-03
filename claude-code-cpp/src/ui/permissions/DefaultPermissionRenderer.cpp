#ifdef HAS_FTXUI

#include <claude/ui/permissions/DefaultPermissionRenderer.hpp>
#include "ui/FtxuiColors.hpp"

namespace claude::ui {

using namespace ftxui;

Element DefaultPermissionRenderer::renderPrompt(const PermissionRequest& req,
                                                 const RenderContext& ctx) {
    return vbox({
        hbox({
            text("  Allow ") | ftxui::color(ctx.theme.warning) | bold,
            text(req.toolName) | ftxui::color(ctx.theme.accent) | bold,
            text("?") | ftxui::color(ctx.theme.warning) | bold,
        }),
        text("  " + req.activity) | ftxui::color(ctx.theme.muted),
    });
}

std::string DefaultPermissionRenderer::getActivityDescription(
    const PermissionRequest& req) {
    return req.activity;
}

} // namespace claude::ui

#endif // HAS_FTXUI
