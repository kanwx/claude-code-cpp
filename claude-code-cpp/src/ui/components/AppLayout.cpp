#ifdef HAS_FTXUI

#include <claude/ui/components/AppLayout.hpp>
#include <claude/ui/components/ContentArea.hpp>
#include <claude/ui/components/MessageList.hpp>
#include <claude/ui/ContentBlockRenderer.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <claude/ui/PromptInputFooter.hpp>
#include <claude/console/AnsiStyle.hpp>
#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <algorithm>
#include <cstdio>

namespace claude::ui {

using namespace ftxui_colors;

namespace {

String fmtTokens(int n) {
    if (n >= 1'000'000)
        return std::to_string(n / 100'000) + "." + std::to_string((n % 100'000) / 10) + "M";
    if (n >= 1'000)
        return std::to_string(n / 100) + "." + std::to_string((n % 100) / 10) + "K";
    return std::to_string(n);
}

String fmtCost(double cost) {
    if (cost < 0.0001) return "$0.0000";
    char buf[16];
    snprintf(buf, sizeof(buf), "$%.4f", cost);
    return String(buf);
}

} // namespace

// ========== Sub-renderers ==========

ftxui::Element renderHeaderBar(const HeaderState& state) {
    using namespace ftxui;

    int ctxPct = static_cast<int>(state.contextPercent);
    int barWidth = 10;
    int filled = std::clamp((ctxPct * barWidth + 50) / 100, 0, barWidth);

    auto barColor = (ctxPct >= 85) ? MacContextCrit
                  : (ctxPct >= 70) ? MacContextWarn
                  :                  MacContextOk;

    String barStr;
    for (int i = 0; i < filled; ++i) barStr += "█";
    for (int i = filled; i < barWidth; ++i) barStr += "░";
    String pctStr = std::to_string(ctxPct) + "% ctx";

    return hbox({
        text(" ╭─") | color(MacPeach),
        text(" Claude Code C++ ") | bold | color(MacPeach),
        text("│ ") | color(MacShadow),
        text(state.modelName) | dim | color(MacCream),
        text(" │ ") | color(MacShadow),
        text(barStr) | color(barColor),
        text(" " + pctStr) | color(barColor) | dim,
        text(" │ ") | color(MacShadow),
        text(fmtTokens(state.inputTokens) + " in/" + fmtTokens(state.outputTokens) + " out") | color(MacCream) | dim,
        text(" · ") | color(MacShadow),
        text(fmtCost(state.cost)) | color(MacCream) | dim,
        text(" │ ") | color(MacShadow),
        text(state.cwd.empty() ? "~" : state.cwd) | dim | color(MacCream),
        state.gitBranch.empty() ? emptyElement()
            : hbox({ text(" (") | color(MacShadow),
                     text(state.gitBranch) | dim | color(MacSky),
                     text(")") | color(MacShadow) }),
        filler(),
        text(state.isStreaming ? "● Running" : "○ Idle")
            | color(state.isStreaming ? MacMint : MacShadow),
        text(" ─╮") | color(MacPeach),
    });
}

ftxui::Element renderPermissionOverlay(const AppLayoutState& state) {
    using namespace ftxui;

    auto badgeBg = toolBgColor(state.permissionToolName);
    auto badgeFg = toolFgColor(state.permissionToolName);

    std::vector<Element> permElems;

    // ┌─ top border ─┐
    permElems.push_back(hbox({
        text("┌") | color(MacGold),
        text("─") | flex | color(MacGold),
        text("┐") | color(MacGold),
    }));

    // Question text — context-specific per TS format
    String question;
    if (!state.permissionToolName.empty()) {
        question = "Claude needs your permission to use " + state.permissionToolName;
    } else if (!state.permissionActivity.empty()) {
        question = state.permissionActivity;
    } else {
        question = "Claude Code needs your attention";
    }
    // Wrap question if too long
    if (question.size() > 60) question = question.substr(0, 57) + "...";

    permElems.push_back(hbox({
        text("│ ") | color(MacGold),
        text(question) | bold | color(MacCream),
        filler(),
        text(" │") | color(MacGold),
    }));

    // Blank line
    permElems.push_back(hbox({
        text("│") | color(MacGold),
        filler(),
        text("│") | color(MacGold),
    }));

    // Description (tool-specific, shown as dim detail under question)
    if (!state.permissionDescription.empty()) {
        String descSummary = state.permissionDescription;
        if (descSummary.size() > 70) descSummary = descSummary.substr(0, 67) + "...";
        permElems.push_back(hbox({
            text("│ ") | color(MacGold),
            text(descSummary) | dim | color(MacCream),
            filler(),
            text(" │") | color(MacGold),
        }));
        permElems.push_back(hbox({
            text("│") | color(MacGold),
            filler(),
            text("│") | color(MacGold),
        }));
    }

    // Radio-style options: ○ / ◉
    const char* optionLabels[] = {
        "Allow Once",
        "Allow for Session",
        "Always Allow",
        "Deny Once",
        "Always Deny"
    };
    const Color optionColors[] = {
        MacMint,    // AllowOnce
        MacSage,    // AllowSession
        MacSage,    // AlwaysAllow
        MacRose,    // DenyOnce
        MacRose,    // AlwaysDeny
    };

    for (int i = 0; i < 5; i++) {
        bool focused = (i == state.permissionFocusedIndex);
        // Blank line separator between Allow and Deny groups
        if (i == 3) {
            permElems.push_back(hbox({
                text("│") | color(MacGold),
                filler(),
                text("│") | color(MacGold),
            }));
        }
        permElems.push_back(hbox({
            text("│ ") | color(MacGold),
            text(focused ? "◉ " : "○ ")
                | color(focused ? optionColors[i] : MacShadow),
            text(optionLabels[i])
                | (focused ? bold : dim)
                | color(focused ? optionColors[i] : MacCream),
            filler(),
            text(" │") | color(MacGold),
        }));
    }

    // Feedback text input (shown when Tab is pressed)
    if (state.permissionFeedbackActive) {
        // Determine placeholder based on whether selected option is Allow or Deny
        bool isAllow = (state.permissionFocusedIndex < 3);  // indices 0-2 are Allow
        String placeholder = isAllow
            ? "tell Claude what to do next"
            : "tell Claude what to do differently";

        // Render the feedback input field
        const String& fbText = state.permissionFeedbackText;
        size_t cursorPos = state.permissionFeedbackCursorPos;
        if (cursorPos > fbText.size()) cursorPos = fbText.size();

        String beforeCursor = fbText.substr(0, cursorPos);
        String cursorChar = " ";
        size_t nextCharPos = cursorPos;
        if (cursorPos < fbText.size()) {
            nextCharPos = cursorPos + 1;
            while (nextCharPos < fbText.size()) {
                auto c = static_cast<unsigned char>(fbText[nextCharPos]);
                if ((c & 0xC0) != 0x80) break;
                nextCharPos++;
            }
            cursorChar = fbText.substr(cursorPos, nextCharPos - cursorPos);
        }
        String afterCursor = (nextCharPos < fbText.size())
            ? fbText.substr(nextCharPos) : "";

        if (fbText.empty()) {
            // Show placeholder
            permElems.push_back(hbox({
                text("│ ") | color(MacGold),
                text("  ") | color(MacShadow),
                text(placeholder) | dim | color(MacShadow) | inverted,
                filler(),
                text(" │") | color(MacGold),
            }));
        } else {
            permElems.push_back(hbox({
                text("│ ") | color(MacGold),
                text("  "),
                text(beforeCursor),
                text(cursorChar) | inverted,
                text(afterCursor),
                filler(),
                text(" │") | color(MacGold),
            }));
        }
    }

    // Blank line before footer
    permElems.push_back(hbox({
        text("│") | color(MacGold),
        filler(),
        text("│") | color(MacGold),
    }));

    // Footer hint: "Esc to cancel ∙ Tab to amend"
    permElems.push_back(hbox({
        text("│ ") | color(MacGold),
        text("Esc to cancel") | dim | color(MacShadow),
        text(" ∙ ") | dim | color(MacShadow),
        text("Tab to amend") | dim | color(MacShadow),
        filler(),
        text(" │") | color(MacGold),
    }));

    // └─ bottom border ─┘
    permElems.push_back(hbox({
        text("└") | color(MacGold),
        text("─") | flex | color(MacGold),
        text("┘") | color(MacGold),
    }));

    return clear_under(vbox(std::move(permElems)));
}

ftxui::Element renderInputLine(const InputState& state) {
    using namespace ftxui;

    if (state.streaming) {
        return hbox({
            text("❯ ") | color(MacSage) | bold,
        });
    }

    // Clamp cursor position to a valid UTF-8 character boundary
    size_t cursorPos = state.cursorPos;
    const String& input = state.text;
    if (cursorPos > input.size()) cursorPos = input.size();
    while (cursorPos > 0 && cursorPos < input.size()) {
        auto c = static_cast<unsigned char>(input[cursorPos]);
        if ((c & 0xC0) == 0x80) cursorPos--;
        else break;
    }

    String beforeCursor = input.substr(0, cursorPos);
    String cursorChar = " ";
    size_t nextCharPos = cursorPos;
    if (cursorPos < input.size()) {
        nextCharPos = cursorPos + 1;
        while (nextCharPos < input.size()) {
            auto c = static_cast<unsigned char>(input[nextCharPos]);
            if ((c & 0xC0) != 0x80) break;
            nextCharPos++;
        }
        cursorChar = input.substr(cursorPos, nextCharPos - cursorPos);
    }
    String afterCursor = (nextCharPos < input.size())
        ? input.substr(nextCharPos) : "";

    return hbox({
        text("❯ ") | color(MacSage) | bold,
        text(beforeCursor),
        text(cursorChar) | inverted,
        text(afterCursor),
    });
}

ftxui::Element renderCompletions(const AppLayoutState& state) {
    using namespace ftxui;

    if (state.input.streaming || state.completions.empty()) {
        return text("");
    }

    std::vector<Element> compElems;
    compElems.push_back(hbox({
        text("  ╭─ completions ─") | color(MacSky) | dim,
        filler() | color(MacSky) | dim,
        text("╮") | color(MacSky) | dim,
    }));

    size_t displayCount = std::min(state.completions.size(), size_t(8));
    for (size_t ci = 0; ci < displayCount; ++ci) {
        bool selected = (ci == state.completionIndex);
        String compText = state.completions[ci];
        if (compText.size() > 60) compText = compText.substr(0, 57) + "...";
        compElems.push_back(hbox({
            text("  │ ") | color(MacSky) | dim,
            text(selected ? "❯ " : "  "),
            text(compText)
                | (selected ? bold : dim)
                | color(selected ? MacPeach : MacCream),
            filler(),
            text(" │") | color(MacSky) | dim,
        }));
    }
    if (state.completions.size() > displayCount) {
        compElems.push_back(hbox({
            text("  │ ") | color(MacSky) | dim,
            text("  ..." + std::to_string(state.completions.size() - displayCount) + " more")
                | dim | color(MacShadow),
            filler(),
            text(" │") | color(MacSky) | dim,
        }));
    }
    compElems.push_back(hbox({
        text("  ╰") | color(MacSky) | dim,
        text(" Tab accept · ↑↓ cycle · Esc dismiss ") | dim | color(MacShadow),
        filler() | color(MacSky) | dim,
        text("╯") | color(MacSky) | dim,
    }));
    return vbox(std::move(compElems));
}

ftxui::Element renderStatusBar(const StatusState& state) {
    using namespace ftxui;

    if (!state.visible) return emptyElement();

    Elements lines;

    // Line 1: duration · model
    Elements line1;
    if (!state.turnDuration.empty()) {
        line1.push_back(text("✻ Baked for " + state.turnDuration) | dim);
    }
    if (!state.modelName.empty()) {
        if (!line1.empty()) line1.push_back(text(" · ") | dim);
        line1.push_back(text(state.modelName) | dim);
    }
    if (state.isStreaming && line1.empty()) {
        line1.push_back(text("● Running...") | color(MacMint));
    }

    // Line 2: context · tokens · cost
    Elements line2;
    if (!state.contextStr.empty()) {
        line2.push_back(text(state.contextStr) | dim);
    }
    if (!state.tokenStr.empty()) {
        if (!line2.empty()) line2.push_back(text(" · ") | dim);
        line2.push_back(text(state.tokenStr) | dim);
    }
    if (!state.costStr.empty()) {
        if (!line2.empty()) line2.push_back(text(" · ") | dim);
        line2.push_back(text(state.costStr) | dim);
    }

    if (!line1.empty()) lines.push_back(hbox(std::move(line1)));
    if (!line2.empty()) lines.push_back(hbox(std::move(line2)));

    // System notice line
    if (!state.systemNotice.empty()) {
        lines.push_back(text("※ " + state.systemNotice) | dim | color(MacSky));
    }

    if (lines.empty()) return emptyElement();
    return vbox(std::move(lines));
}

ftxui::Element renderFooterBar(const FooterState& state) {
    using namespace ftxui;

    // Build footer state for the existing footer renderer
    ftxui_footer::FooterState footerState;
    footerState.mode = state.modeIndicator;
    footerState.modeHintDismissed = state.modeHintDismissed;
    footerState.isAuthenticated = state.authenticated;
    footerState.isStreaming = state.isStreaming;
    footerState.collapsibleNavActive = state.collapsibleNavActive;
    // The footer renderer handles the rest
    return ftxui_footer::renderFooter(footerState);
}

// ========== AppLayout component ==========

ftxui::Component AppLayoutComponent(AppLayoutState& state, const RenderContext& ctx) {
    // Capture pointers — caller ensures lifetime
    auto* s = &state;
    auto* ctxPtr = &ctx;

    return ftxui::Renderer([s, ctxPtr] {
        using namespace ftxui;

        // 1. Header
        auto header = renderHeaderBar(s->header);

        // 2. Content area — use ContentAreaComponent's rendering via RenderMessageList
        Element contentArea;
        {
            Elements contentEls;

            // "N messages above" indicator
            if (s->content.messagesAbove > 0) {
                contentEls.push_back(
                    hbox({
                        text(" ↑ " + std::to_string(s->content.messagesAbove) + " messages above ")
                        | dim | color(MacShadow)
                        | center
                    }) | flex
                );
            }

            // Committed content blocks (new pipeline)
            if (s->content.contentBlocks && !s->content.contentBlocks->empty()) {
                auto sepIndices = findAnswerSeparatorIndices(*s->content.contentBlocks);
                int focusSeq = 0;
                int collapsibleCount = s->collapsibleCount;
                int focusIndex = s->collapsibleFocusIndex;
                for (size_t i = 0; i < s->content.contentBlocks->size(); ++i) {
                    if (std::find(sepIndices.begin(), sepIndices.end(), i) != sepIndices.end()) {
                        contentEls.push_back(renderAnswerSeparator());
                    }
                    const auto& block = (*s->content.contentBlocks)[i];
                    BlockRenderOptions opts;
                    if (collapsibleCount > 0 &&
                        isCollapsibleFocusTarget(block)) {
                        opts.isFocusedCollapsible = (focusSeq == focusIndex);
                        focusSeq++;
                    }
                    contentEls.push_back(renderFtxuiElement(block, opts));
                }
            }

            // Streaming text — use cached elements from StreamingRenderer when available
            if (!s->content.streaming.text.empty()) {
                if (!contentEls.empty()) contentEls.push_back(text(""));

                // Prefer incremental render from StreamingRenderer (avoids full reparse)
                std::vector<Element> mdBlocks;
                if (!s->content.streaming.cachedElements.empty()) {
                    mdBlocks = std::move(s->content.streaming.cachedElements);
                } else {
                    mdBlocks = FtxuiMarkdown::render(s->content.streaming.text);
                }
                bool firstElem = true;
                for (auto& elem : mdBlocks) {
                    if (firstElem) {
                        contentEls.push_back(hbox({
                            text(String(" ") + AnsiStyle::ASSISTANT_PREFIX + " ") | color(MacCream),
                            std::move(elem) | flex,
                        }));
                        firstElem = false;
                    } else {
                        contentEls.push_back(std::move(elem));
                    }
                }

                // Blinking cursor
                bool cursorVisible = (s->content.streaming.tickCounter % 20) < 10;
                Color cursorColor = cursorVisible ? MacPeach : MacCream;
                contentEls.push_back(hbox({
                    text(cursorVisible ? "  ◉" : "  ○") | color(cursorColor) | blink,
                }));
            }

            // Thinking indicator — transient running status line.
            // Matches TS SpinnerAnimationRow format:
            //   · Wandering… (11s · ↑ 70 tokens · thinking)
            if (s->content.thinking.active) {
                if (!contentEls.empty()) contentEls.push_back(text(""));

                Color thinkColor = MacLavender;
                std::vector<Element> thinkingElems;

                // · + verb…
                if (!s->content.thinking.runningVerb.empty()) {
                    thinkingElems.push_back(
                        text("· " + s->content.thinking.runningVerb + "…")
                        | color(thinkColor)
                    );
                } else {
                    thinkingElems.push_back(
                        text("· Thinking…") | color(thinkColor)
                    );
                }

                // (Xs · ↑ N tokens · thinking)
                std::string suffix;
                if (s->content.thinking.elapsedSeconds > 0) {
                    int secs = s->content.thinking.elapsedSeconds;
                    if (secs >= 60) {
                        suffix += std::to_string(secs / 60) + "m " +
                                  std::to_string(secs % 60) + "s";
                    } else {
                        suffix += std::to_string(secs) + "s";
                    }
                }
                if (s->content.thinking.tokenEstimate > 0) {
                    // Format token estimate compactly (matching TS formatNumber)
                    int n = s->content.thinking.tokenEstimate;
                    std::string tokStr;
                    if (n >= 1000) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d.%dk", n / 1000,
                                 (n % 1000) / 100);
                        tokStr = buf;
                    } else {
                        tokStr = std::to_string(n);
                    }
                    if (!suffix.empty()) suffix += " · ";
                    suffix += "↑ " + tokStr + " tokens";
                }
                if (!suffix.empty()) suffix += " · ";
                suffix += "thinking";

                if (!suffix.empty()) {
                    thinkingElems.push_back(text("  ") | dim);
                    thinkingElems.push_back(text("(" + suffix + ")") | dim | color(thinkColor));
                }

                // Glimmer dot
                bool glimmerPhase = (s->content.thinking.tickCounter % 20) < 10;
                if (glimmerPhase) {
                    thinkingElems.push_back(text(" ●") | color(thinkColor) | dim);
                }

                contentEls.push_back(hbox(std::move(thinkingElems)));
            }

            // Add focus anchor at bottom for auto-scroll
            // yframe scrolls to show focused elements, so placing focus
            // at the bottom forces auto-scroll when autoScroll is on
            if (s->content.autoScroll) {
                contentEls.push_back(text("") | focus);
            }

            auto messagesContent = vbox(std::move(contentEls))
                | focusPositionRelative(0.f, s->content.scrollRatio)
                | yframe
                | vscroll_indicator
                | flex;

            // Permission overlay on top of content
            if (s->permissionActive) {
                auto overlay = renderPermissionOverlay(*s);
                contentArea = dbox({
                    messagesContent | flex,
                    vbox({ filler(), overlay | hcenter | size(WIDTH, LESS_THAN, 70) }),
                }) | flex;
            } else {
                contentArea = std::move(messagesContent);
            }
        }

        // 3. Status bar (turn metadata)
        auto statusBar = renderStatusBar(s->status);

        // 4. Completions
        auto completionArea = renderCompletions(*s);

        // 5. Input line
        auto inputLine = renderInputLine(s->input);

        // 6. Footer
        auto footer = renderFooterBar(s->footer);

        return vbox({
            header,
            separatorLight(),
            contentArea | flex,
            separatorLight(),
            statusBar,
            completionArea,
            inputLine | (s->input.streaming ? dim : bold),
            footer,
        });
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
