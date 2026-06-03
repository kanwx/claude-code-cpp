#ifdef HAS_FTXUI

#include <claude/ui/permissions/BashPermissionRenderer.hpp>
#include "ui/FtxuiColors.hpp"

namespace claude::ui {

namespace {

static std::string extractField(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) { result += json[pos+1]; pos += 2; }
        else { result += json[pos]; pos++; }
    }
    return result;
}

} // anonymous namespace

using namespace ftxui;

Element BashPermissionRenderer::renderPrompt(const PermissionRequest& req,
                                              const RenderContext& ctx) {
    auto details = renderDetailLines(req, ctx);
    Elements children;
    children.push_back(hbox({
        text("  Allow ") | ftxui::color(ctx.theme.warning) | bold,
        text("Bash") | ftxui::color(ctx.theme.accent) | bold,
        text("?") | ftxui::color(ctx.theme.warning) | bold,
    }));
    for (auto& line : details) {
        children.push_back(std::move(line));
    }
    return vbox(std::move(children));
}

std::string BashPermissionRenderer::getActivityDescription(
    const PermissionRequest& req) {
    auto cmd = extractField(req.input, "command");
    return cmd.empty() ? req.activity : cmd;
}

std::vector<ftxui::Element> BashPermissionRenderer::renderDetailLines(
    const PermissionRequest& req, const RenderContext& /*ctx*/) {
    auto cmd = extractField(req.input, "command");
    Elements lines;
    lines.push_back(hbox({text("  Command: "), text(cmd) | ftxui::color(ftxui_colors::MacCream)}));
    // Safety warnings
    if (cmd.find("rm ") != std::string::npos ||
        cmd.find("rmdir") != std::string::npos) {
        lines.push_back(text("  \xe2\x9a\xa0 Destructive command") | ftxui::color(ftxui_colors::MacRose) | bold);
    }
    if (cmd.find("sudo") != std::string::npos) {
        lines.push_back(text("  \xe2\x9a\xa0 Elevated privileges") | ftxui::color(ftxui_colors::MacGold) | bold);
    }
    return lines;
}

} // namespace claude::ui

#endif // HAS_FTXUI
