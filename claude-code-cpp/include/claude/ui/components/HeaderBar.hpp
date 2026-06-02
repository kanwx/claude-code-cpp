#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <string>

namespace claude::ui {

struct HeaderState {
    std::string modelName;
    float contextPercent = 0.0f;
    int inputTokens = 0;
    int outputTokens = 0;
    float cost = 0.0f;
    std::string cwd;
    std::string gitBranch;
    bool isStreaming = false;
};

ftxui::Component HeaderBar(HeaderState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
