#pragma once

#include "claude/stream/DisplayEvent.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <vector>

namespace claude {

class AnswerPostProcessor {
public:
    DisplayEvent process(DisplayEvent&& event);
    std::vector<DisplayEvent> finalize();
    void reset();

    static bool isCollapsibleToolName(const String& name);
    static bool isCollapsibleTool(const String& name);

private:
    std::vector<DisplayEvent> events_;

    void cleanThinkingTags(DisplayEvent& event);
    std::vector<DisplayEvent> groupConsecutiveToolResults();
    std::vector<DisplayEvent> reorderToolTrails();

    static const std::vector<String> COLLAPSIBLE_TOOLS;
};

} // namespace claude
