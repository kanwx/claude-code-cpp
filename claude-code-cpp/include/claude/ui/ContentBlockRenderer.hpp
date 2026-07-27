#pragma once

#include "claude/stream/ContentBlock.hpp"
#include <string>

namespace claude {

class ContentBlockRenderer {
public:
    // ANSI backend — returns string with ANSI escape codes
    static String renderAnsi(const ContentBlock& block);

    // Plain-text backend — strips all ANSI escape codes for non-TTY output
    static String renderPlain(const ContentBlock& block);

    // Text-only fallback for testing — returns plain text representation.
    static String renderFtxuiText(const ContentBlock& block);

    static String toolBadge(const String& toolName);
    static String formatGroupSummary(const std::vector<ContentBlock>& children);
};

bool isToolLikeBlock(const ContentBlock& block);
std::vector<size_t> findAnswerSeparatorIndices(const std::vector<ContentBlock>& blocks);

} // namespace claude

#ifdef HAS_FTXUI
#include <ftxui/dom/elements.hpp>
namespace claude {

struct BlockRenderOptions {
    bool isFocusedCollapsible = false;
    bool isInExpandedGroup = false;  // true when rendered inside an expanded CollapsedGroup
};

ftxui::Element renderFtxuiElement(const ContentBlock& block,
                                  const BlockRenderOptions& opts = {});
ftxui::Element renderAnswerSeparator();

/// Check whether a ContentBlock is a collapsible focus target
/// (matches the collapsible tracking in FtxuiRepl::syncLayoutState).
bool isCollapsibleFocusTarget(const ContentBlock& block);

} // namespace claude
#endif
