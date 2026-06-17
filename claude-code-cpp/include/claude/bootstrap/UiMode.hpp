#pragma once

#include <string>

namespace claude {

/// Result of UI mode selection during startup.
struct UiModeDecision {
    bool interactive = false;
    bool useFtxui = false;
    std::string mode;    // "ftxui", "plain", "headless"
    std::string reason;  // human-readable explanation
};

/// Decide the UI mode based on CLI flags and terminal state.
///
/// Pure function — no side effects, testable without a real terminal.
///
/// Parameters are the raw inputs BEFORE any default-setting logic:
///   hasFtxuiBuild    — whether the binary was compiled with FTXUI support
///   explicitNoFtxui  — --no-ftxui flag was passed
///   explicitInteractive — -i / --interactive flag was passed
///   hasPrompt        — -p / --prompt was provided
///   stdoutIsTty      — isatty(STDOUT_FILENO) result
inline UiModeDecision decideUiMode(
        bool hasFtxuiBuild,
        bool explicitNoFtxui,
        bool explicitInteractive,
        bool hasPrompt,
        bool stdoutIsTty) {

    UiModeDecision d;

    // Step 1: determine interactive_
    // -p / --prompt always means non-interactive print mode.
    // Otherwise, default to interactive unless explicitly overridden.
    if (hasPrompt) {
        d.interactive = false;
    } else if (explicitInteractive) {
        d.interactive = true;
    } else {
        d.interactive = true;  // default: interactive
    }

    // Step 2: determine useFtxui_
    d.useFtxui = hasFtxuiBuild;

    if (explicitNoFtxui) {
        d.useFtxui = false;
        d.reason = "no_ftxui_flag";
    } else if (!stdoutIsTty) {
        d.useFtxui = false;
        d.reason = "stdout_not_tty";
    } else if (!d.interactive) {
        d.useFtxui = false;
        d.reason = "print_mode";
    }

    // Step 3: classify mode for debug / reporting
    if (d.useFtxui) {
        d.mode = "ftxui";
        if (d.reason.empty()) {
            d.reason = "interactive_tty_default";
        }
    } else if (!d.interactive) {
        d.mode = "headless";
    } else {
        d.mode = "plain";
    }

    return d;
}

} // namespace claude
