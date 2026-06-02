#include <claude/ui/components/HeaderBar.hpp>

#ifdef HAS_FTXUI

#include "../FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
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

ftxui::Component HeaderBar(HeaderState& state) {
    return ftxui::Renderer([&state] {
        using namespace ftxui;

        // Context usage progress bar
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
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
