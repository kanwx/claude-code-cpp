#pragma once

#include "UiMessageTypes.hpp"
#include <vector>

namespace claude {

class ThinkingFilter {
public:
    ThinkingFilter() = delete;

    [[nodiscard]] static std::vector<DisplayMessage> apply(const std::vector<DisplayMessage>& messages);
    [[nodiscard]] static std::vector<DisplayMessage> removeRedactedThinking(const std::vector<DisplayMessage>& messages);
    [[nodiscard]] static std::vector<DisplayMessage> stripTrailingThinking(const std::vector<DisplayMessage>& messages);
    [[nodiscard]] static std::vector<DisplayMessage> removeOrphanedThinking(const std::vector<DisplayMessage>& messages);
};

} // namespace claude
