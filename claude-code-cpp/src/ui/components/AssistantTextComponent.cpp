#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantTextComponent.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element AssistantTextComponent::OnRender() {
    using namespace ftxui;
    if (!msg_.text.empty()) {
        auto mdBlocks = FtxuiMarkdown::render(msg_.text);
        return vbox(std::move(mdBlocks));
    }
    return text("");
}

} // namespace claude::ui

#endif // HAS_FTXUI
