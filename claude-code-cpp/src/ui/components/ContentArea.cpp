#ifdef HAS_FTXUI

#include <claude/ui/components/ContentArea.hpp>
#include <claude/ui/components/MessageList.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Component ContentAreaComponent(ContentState& state, const RenderContext& ctx) {
    // Capture pointers — caller ensures lifetime
    const auto* msgs = state.messages;
    const auto* ctxPtr = &ctx;
    auto* st = &state;

    return ftxui::Renderer([st, msgs, ctxPtr] {
        using namespace ftxui;

        Elements contentEls;

        // "N messages above" indicator (for compacted/hidden messages)
        if (st->messagesAbove > 0) {
            contentEls.push_back(
                hbox({
                    text(" ↑ " + std::to_string(st->messagesAbove) + " messages above ")
                    | dim | color(MacShadow)
                    | center
                }) | flex
            );
        }

        // Message list — dispatches to per-type components
        if (msgs && !msgs->empty()) {
            contentEls.push_back(RenderMessageList(msgs, ctxPtr));
        }

        // Streaming text — live assistant text with blinking cursor
        if (!st->streaming.text.empty()) {
            if (!contentEls.empty()) contentEls.push_back(text(""));

            auto mdBlocks = FtxuiMarkdown::render(st->streaming.text);
            bool firstElem = true;
            for (auto& elem : mdBlocks) {
                if (firstElem) {
                    contentEls.push_back(hbox({
                        text("● ") | color(MacSky),
                        std::move(elem) | flex,
                    }));
                    firstElem = false;
                } else {
                    contentEls.push_back(std::move(elem));
                }
            }

            // Blinking cursor
            bool cursorVisible = (st->streaming.tickCounter % 4) < 2;
            Color cursorColor = cursorVisible ? MacPeach : MacCream;
            contentEls.push_back(hbox({
                text("  ◉") | color(cursorColor) | blink,
            }));
        }

        // Thinking indicator — spinner + summary
        if (st->thinking.active) {
            if (!contentEls.empty()) contentEls.push_back(text(""));

            Color thinkColor = st->thinking.stalled ? MacRose : MacLavender;
            bool glimmerPhase = (st->thinking.tickCounter % 20) < 10;

            std::vector<Element> thinkingElems;
            thinkingElems.push_back(spinner(1, st->thinking.tickCounter) | color(thinkColor));
            thinkingElems.push_back(
                text(st->thinking.stalled ? " Thinking (stalled)" : " Thinking")
                | bold | color(thinkColor)
            );
            if (glimmerPhase) {
                thinkingElems.push_back(text(" ●") | color(thinkColor) | dim);
            }
            if (!st->thinking.summary.empty()) {
                thinkingElems.push_back(text("  ") | dim);
                std::string summary = st->thinking.summary;
                if (summary.size() > 60) {
                    summary = "..." + summary.substr(summary.size() - 57);
                }
                thinkingElems.push_back(text(summary) | dim | color(MacCream));
            }
            thinkingElems.push_back(text(" (ctrl+o)") | dim | color(MacShadow));

            contentEls.push_back(hbox(std::move(thinkingElems)));
        }

        // Wrap in a scrollable flex area
        auto content = vbox(std::move(contentEls)) | flex;

        // Auto-scroll: use yframe to make the area scrollable.
        // When autoScroll is true, the frame automatically scrolls to the bottom.
        if (st->autoScroll) {
            content = content | yframe | flex;
        } else {
            content = content | yframe | flex;
        }

        return content;
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
