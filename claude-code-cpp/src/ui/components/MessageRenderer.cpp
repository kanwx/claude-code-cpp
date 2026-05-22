#include <claude/ui/components/MessageRenderer.hpp>

namespace claude {

void RendererRegistry::registerRenderer(std::unique_ptr<MessageRenderer> renderer) {
    auto type = renderer->targetType();
    renderers_[static_cast<int>(type)] = std::move(renderer);
}

std::vector<ftxui::Element> RendererRegistry::render(const DisplayMessage& msg,
                                                      const RendererContext& ctx) const {
    auto it = renderers_.find(static_cast<int>(msg.type));
    if (it != renderers_.end()) {
        return it->second->render(msg, ctx);
    }
    return {};
}

bool RendererRegistry::hasRenderer(DisplayMessage::Type type) const {
    return renderers_.find(static_cast<int>(type)) != renderers_.end();
}

} // namespace claude
