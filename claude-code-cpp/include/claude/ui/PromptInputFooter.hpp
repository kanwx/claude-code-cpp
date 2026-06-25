#pragma once

#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>
#include "../core/Types.hpp"

namespace claude::ftxui_footer {

struct FooterState {
    String mode;            // "default", "auto", "bypass", "plan", "acceptEdits", "dontAsk"
    bool modeHintDismissed = false;
    bool isAuthenticated = false;
    String modelInfo;
    int contextPct = 0;
    double costUsd = 0.0;
    bool isStreaming = false;
    bool collapsibleNavActive = false;
    String cwd;
    String gitBranch;
};

ftxui::Element renderFooter(const FooterState& state);

} // namespace claude::ftxui_footer

#endif // HAS_FTXUI
