#include "claude/ui/ContentBlockRenderer.hpp"

#ifdef HAS_FTXUI
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include "claude/ui/FtxuiMarkdown.hpp"

namespace claude {

ftxui::Element renderFtxuiElement(const ContentBlock& block) {
    using namespace ftxui;

    switch (block.type) {
        case ContentBlock::UserMessage:
            return hbox({text("> ") | bold, text(block.text)});

        case ContentBlock::AnswerText: {
            auto elements = FtxuiMarkdown::render(block.text);
            return vbox(std::move(elements));
        }

        case ContentBlock::ThinkingBlock:
            if (block.expanded) {
                Elements els;
                els.push_back(text("Thinking...") | dim);
                auto detail = FtxuiMarkdown::render(block.detailText);
                for (auto& e : detail) els.push_back(std::move(e));
                els.push_back(text("Thinking") | dim);
                return vbox(std::move(els));
            }
            return text("Thinking  (Ctrl+O to expand)") | dim;

        case ContentBlock::ToolProgress:
            return hbox({text("  \xe2\x8e\xbf "), text("\xe2\x97\x8f "), text(block.activity + "...") | dim});

        case ContentBlock::ToolResult: {
            auto summary = block.summary.isError
                ? text(block.summary.errorText) | color(Color::Red)
                : text(block.summary.primaryText) | dim;
            if (!block.expanded && !block.summary.isError && !block.summary.primaryText.empty()) {
                return hbox({text("  \xe2\x8e\xbf "), summary, text("  [Ctrl+O]") | dim});
            }
            return hbox({text("  \xe2\x8e\xbf "), summary});
        }

        case ContentBlock::ToolGroup: {
            auto summaryEl = text(block.summary.primaryText) | dim;
            if (!block.expanded) {
                return hbox({text("  \xe2\x8e\xbf "), summaryEl, text("  [Ctrl+O]") | dim});
            }
            Elements childrenEls;
            childrenEls.push_back(hbox({text("  \xe2\x8e\xbf "), summaryEl}));
            for (auto& child : block.children) {
                childrenEls.push_back(hbox({text("    "), renderFtxuiElement(child)}));
            }
            return vbox(std::move(childrenEls));
        }

        case ContentBlock::ErrorMessage:
            return text("X " + block.text) | color(Color::Red);

        default:
            return text(block.text);
    }
}

} // namespace claude
#endif
