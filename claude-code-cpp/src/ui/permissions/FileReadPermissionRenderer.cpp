#ifdef HAS_FTXUI

#include <claude/ui/permissions/FileReadPermissionRenderer.hpp>
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

Element FileReadPermissionRenderer::renderPrompt(const PermissionRequest& req,
                                                  const RenderContext& ctx) {
    auto details = renderDetailLines(req, ctx);
    Elements children;
    children.push_back(hbox({
        text("  Allow ") | ftxui::color(ctx.theme.warning) | bold,
        text("Read") | ftxui::color(ctx.theme.accent) | bold,
        text("?") | ftxui::color(ctx.theme.warning) | bold,
    }));
    for (auto& line : details) {
        children.push_back(std::move(line));
    }
    return vbox(std::move(children));
}

std::string FileReadPermissionRenderer::getActivityDescription(
    const PermissionRequest& req) {
    auto path = extractField(req.input, "file_path");
    return path.empty() ? req.activity : path;
}

std::vector<ftxui::Element> FileReadPermissionRenderer::renderDetailLines(
    const PermissionRequest& req, const RenderContext& /*ctx*/) {
    auto path = extractField(req.input, "file_path");
    Elements lines;
    lines.push_back(hbox({text("  File: "), text(path) | ftxui::color(ftxui_colors::MacCream)}));
    return lines;
}

} // namespace claude::ui

#endif // HAS_FTXUI
