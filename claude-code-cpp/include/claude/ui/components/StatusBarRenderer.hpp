#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>

namespace claude {

/// Renders the status bar: spinner + elapsed + tokens during streaming,
/// model + context + cost during idle.
ftxui::Element renderStatusBar(
    bool isStreaming,
    bool isThinking,
    int tickCounter,
    const std::chrono::steady_clock::time_point& startTime,
    const std::string& streamingText,
    long contextUsedTokens,
    long contextMaxTokens,
    double costUsd,
    const std::string& modelInfo
);

/// Renders the header bar with brand.
ftxui::Element renderHeader(const std::string& modelInfo, bool isStreaming);

} // namespace claude
