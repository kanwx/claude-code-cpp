#pragma once

#include "claude/stream/ContentBlock.hpp"
#include <string>

namespace claude {

class ContentBlockRenderer {
public:
    // ANSI backend — returns string with ANSI escape codes
    static String renderAnsi(const ContentBlock& block);

    // Text-only fallback for testing — returns plain text representation.
    // Actual FTXUI rendering is done by renderFtxuiElement() in ContentBlockFtxui.cpp
    // which returns ftxui::Element (not available in this header).
    static String renderFtxuiText(const ContentBlock& block);

    static String toolBadge(const String& toolName);
    static String formatGroupSummary(const std::vector<ContentBlock>& children);
};

} // namespace claude
