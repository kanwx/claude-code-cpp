#ifdef HAS_FTXUI

#include <claude/ui/components/ContentArea.hpp>
#include <claude/ui/ContentBlockRenderer.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

using namespace ftxui_colors;

ftxui::Component ContentAreaComponent(ContentState& state, const RenderContext& ctx) {
    auto* st = &state;

    return ftxui::Renderer([st] {
        using namespace ftxui;

        Elements contentEls;

        // "N messages above" indicator (for compacted/hidden messages)
        if (st->messagesAbove > 0) {
            contentEls.push_back(
                hbox({
                    text(" \xe2\x86\x91 " + std::to_string(st->messagesAbove) + " messages above ")
                    | dim | color(MacShadow)
                    | center
                }) | flex
            );
        }

        // Content blocks — render via renderFtxuiElement, with separator before
        // the final answer after tool output.
        if (st->contentBlocks && !st->contentBlocks->empty()) {
            auto sepIndices = findAnswerSeparatorIndices(*st->contentBlocks);
            for (size_t i = 0; i < st->contentBlocks->size(); ++i) {
                if (std::find(sepIndices.begin(), sepIndices.end(), i) != sepIndices.end()) {
                    contentEls.push_back(renderAnswerSeparator());
                }
                contentEls.push_back(renderFtxuiElement((*st->contentBlocks)[i], {}));
            }
        }

        // Streaming text — rendered as a virtual AnswerText block inline
        // with committed content blocks. This avoids the visual "jump" that
        // occurs when text moves between the old separate streaming area and
        // the committed blocks area. Using the same rendering path ensures
        // that committing text (moving it from streamingText_ to contentBlocks_)
        // is visually seamless.
        if (!st->streaming.text.empty()) {
            // Build a virtual ContentBlock for the streaming text
            ContentBlock streamingCb;
            streamingCb.type = ContentBlock::AnswerText;
            streamingCb.text = st->streaming.text;
            // isFirst: true only when there are no committed AnswerText blocks
            // and the streaming text is the very first assistant content
            streamingCb.isFirst = st->contentBlocks &&
                std::none_of(st->contentBlocks->begin(), st->contentBlocks->end(),
                    [](const ContentBlock& b) { return b.type == ContentBlock::AnswerText; });
            contentEls.push_back(renderFtxuiElement(streamingCb, {}));

            // Blinking cursor after streaming text
            bool cursorVisible = (st->streaming.tickCounter % 4) < 2;
            Color cursorColor = cursorVisible ? MacPeach : MacCream;
            contentEls.push_back(hbox({
                text("  \xe2\x97\x89") | color(cursorColor) | blink,
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
                thinkingElems.push_back(text(" \xe2\x97\x8f") | color(thinkColor) | dim);
            }
            if (!st->thinking.summary.empty() && st->thinking.summary.size() <= 60) {
                thinkingElems.push_back(text("  ") | dim);
                thinkingElems.push_back(text(st->thinking.summary) | dim | color(MacCream));
            }
            thinkingElems.push_back(text(" [Ctrl+O]") | dim | color(MacShadow));

            contentEls.push_back(hbox(std::move(thinkingElems)));
        }

        // Wrap in a scrollable flex area.
        // During active streaming (autoScroll), pin to bottom so new text is always
        // visible. When the user scrolls manually (autoScroll becomes false), remove
        // programmatic positioning and let yframe handle scrolling natively.
        // Using focusPositionRelative outside of auto-scroll causes a tug-of-war
        // with ftxui's internal scroll state, making content appear to shift.
        auto content = vbox(std::move(contentEls)) | flex;

        if (st->autoScroll) {
            content = content | focusPositionRelative(0.f, 1.0f);
        }

        content = content
            | yframe
            | vscroll_indicator
            | flex;

        return content;
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
