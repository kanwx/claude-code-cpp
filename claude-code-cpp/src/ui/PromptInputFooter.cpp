#ifdef HAS_FTXUI

#include "claude/ui/PromptInputFooter.hpp"
#include "FtxuiColors.hpp"
#include <cstdio>

namespace claude::ftxui_footer {

using namespace ftxui;
using namespace claude::ftxui_colors;

struct ModeDisplay {
    const char* symbol;
    const char* label;
    ftxui::Color color;
};

ModeDisplay getModeDisplay(const String& mode) {
    if (mode == "auto")        return {"⚙", "auto on",         MacSky};
    if (mode == "plan")        return {"✦", "plan on",         MacLavender};
    if (mode == "bypass")      return {"⚡", "bypass on",      MacRose};
    if (mode == "acceptEdits") return {"✎", "accept edits on", MacMint};
    if (mode == "dontAsk")     return {"⊙", "dont ask on",    MacCream};
    return {"", "", MacShadow};
}

ftxui::Element renderFooter(const FooterState& state) {
    std::vector<Element> leftParts;

    // Mode indicator
    auto modeDisp = getModeDisplay(state.mode);
    if (modeDisp.label[0] != '\0') {
        leftParts.push_back(hbox({
            text(String(modeDisp.symbol) + " ") | color(modeDisp.color),
            text(modeDisp.label) | color(modeDisp.color) | dim,
        }));
        if (!state.modeHintDismissed) {
            leftParts.push_back(text(" shift+tab to cycle") | color(MacShadow) | dim);
        }
    }

    // Keyboard hints (context-dependent)
    if (state.isStreaming) {
        leftParts.push_back(text("esc to interrupt") | color(MacShadow) | dim);
    } else {
        leftParts.push_back(text("? for shortcuts") | color(MacShadow) | dim);
    }

    // Right side: auth + model + context + cost
    std::vector<Element> rightParts;

    // Auth status
    if (state.isAuthenticated) {
        rightParts.push_back(text("✓") | color(MacMint));
        rightParts.push_back(text(" logged in") | color(MacShadow) | dim);
    } else {
        rightParts.push_back(text("⚠") | color(MacGold));
        rightParts.push_back(text(" not authenticated") | color(MacGold) | dim);
    }

    rightParts.push_back(text(" · ") | color(MacShadow));

    // Model + context + cost
    if (!state.modelInfo.empty()) {
        rightParts.push_back(text(state.modelInfo) | color(MacCream) | dim);
        rightParts.push_back(text(" · ") | color(MacShadow));
    }

    auto barColor = (state.contextPct >= 85) ? MacContextCrit
                  : (state.contextPct >= 70) ? MacContextWarn
                  :                             MacContextOk;

    rightParts.push_back(text(std::to_string(state.contextPct) + "% ctx") | color(barColor) | dim);
    rightParts.push_back(text(" · ") | color(MacShadow));

    char costBuf[16];
    snprintf(costBuf, sizeof(costBuf), "$%.4f", state.costUsd);
    rightParts.push_back(text(costBuf) | color(MacCream) | dim);

    // Compose
    auto leftSide = leftParts.empty() ? emptyElement()
                  : leftParts.size() == 1 ? leftParts[0]
                  : hbox(leftParts) | flex;

    auto rightSide = rightParts.empty() ? emptyElement()
                   : hbox(rightParts);

    return hbox({
        text(" "),
        leftSide | flex,
        rightSide,
        text(" "),
    }) | bgcolor(MacBgDark);
}

} // namespace claude::ftxui_footer

#endif // HAS_FTXUI
