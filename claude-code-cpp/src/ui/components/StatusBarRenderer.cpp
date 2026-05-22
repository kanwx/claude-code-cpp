#include <claude/ui/components/StatusBarRenderer.hpp>
#include <claude/core/Types.hpp>
#include <chrono>

namespace claude {

using namespace ftxui;

static const Color BrandOrange = Color::RGB(215, 119, 87);

static String formatElapsed(int seconds) {
    if (seconds >= 60) {
        return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    }
    return std::to_string(seconds) + "s";
}

Element renderHeader(const std::string& modelInfo, bool isStreaming) {
    auto orange = BrandOrange;
    return hbox({
        text(" ╭─") | color(orange),
        text(" Claude Code C++ ") | bold | color(orange),
        text("│ ") | color(Color::GrayDark),
        text(modelInfo) | dim | color(Color::GrayLight),
        filler(),
        text(isStreaming ? "● Running" : "○ Idle")
            | color(isStreaming ? Color::Green : Color::GrayDark),
        text(" ─╮") | color(orange),
    });
}

Element renderStatusBar(
    bool isStreaming,
    bool isThinking,
    int tickCounter,
    const std::chrono::steady_clock::time_point& startTime,
    const std::string& streamingText,
    long contextUsedTokens,
    long contextMaxTokens,
    double costUsd,
    const std::string& modelInfo)
{
    auto orange = BrandOrange;

    if (isStreaming || isThinking) {
        int elapsed = 0;
        if (startTime.time_since_epoch().count() > 0) {
            elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
        }
        String ts = formatElapsed(elapsed);
        String label = isThinking ? "Thinking" : "Running";
        int tokens = static_cast<int>(streamingText.size() / 4);

        String contextPct;
        if (contextMaxTokens > 0) {
            int pct = static_cast<int>(contextUsedTokens * 100 / contextMaxTokens);
            contextPct = " · " + std::to_string(pct) + "%";
        }

        return hbox({
            spinner(1, tickCounter) | color(orange),
            text(" " + label) | bold | color(orange),
            text("  ") | dim,
            text(ts) | color(Color::Yellow),
            text(" · ") | dim,
            text(std::to_string(tokens) + " tok") | color(Color::GrayLight),
            text(contextPct) | dim | color(Color::GrayLight),
            filler(),
        });
    }

    // Idle: show model + context + cost
    std::vector<Element> idleParts;
    if (!modelInfo.empty()) {
        idleParts.push_back(text(modelInfo) | color(Color::GrayLight));
    }
    if (contextMaxTokens > 0) {
        int pct = static_cast<int>(contextUsedTokens * 100 / contextMaxTokens);
        Color pctColor = pct >= 85 ? Color::Red : pct >= 70 ? Color::Yellow : Color::GrayLight;
        if (!idleParts.empty()) idleParts.push_back(text(" · ") | dim);
        idleParts.push_back(text(std::to_string(pct) + "% ctx") | color(pctColor));
    }
    if (costUsd > 0.0) {
        char costBuf[32];
        snprintf(costBuf, sizeof(costBuf), "$%.4f", costUsd);
        if (!idleParts.empty()) idleParts.push_back(text(" · ") | dim);
        idleParts.push_back(text(costBuf) | color(Color::GrayLight));
    }
    if (!idleParts.empty()) {
        idleParts.push_back(filler());
        return hbox(std::move(idleParts));
    }
    return hbox({ filler() });
}

} // namespace claude
