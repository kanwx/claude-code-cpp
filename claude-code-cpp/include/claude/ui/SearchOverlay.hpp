#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <vector>
#include <string>

namespace claude::ui {

class SearchOverlay : public ftxui::ComponentBase {
public:
    SearchOverlay();

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;

    void activate();
    void deactivate();
    bool isActive() const { return active_; }

    struct SearchResult {
        int messageIndex;
        int matchOffset;
        std::string contextLine;
    };

    const std::vector<SearchResult>& results() const { return results_; }
    int currentMatchIndex() const { return currentMatch_; }
    void nextMatch();
    void prevMatch();

    void setMessages(const std::vector<DisplayMessage>* messages) {
        messages_ = messages;
    }

    int highlightMessageIndex() const;
    int highlightMatchOffset() const;

private:
    void performSearch();
    std::string searchQuery_;
    std::vector<SearchResult> results_;
    int currentMatch_ = -1;
    bool active_ = false;
    const std::vector<DisplayMessage>* messages_ = nullptr;
};

} // namespace claude::ui

#endif // HAS_FTXUI
