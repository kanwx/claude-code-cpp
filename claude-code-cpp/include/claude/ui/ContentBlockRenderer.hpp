#pragma once

#include "claude/stream/ContentBlock.hpp"
#include <string>

namespace claude {

class ContentBlockRenderer {
public:
    static String renderAnsi(const ContentBlock& block);
    static String renderFtxuiText(const ContentBlock& block);

    static String toolBadge(const String& toolName);
    static String formatGroupSummary(const std::vector<ContentBlock>& children);
};

} // namespace claude
