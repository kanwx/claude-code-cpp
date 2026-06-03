#ifdef HAS_FTXUI

#include <claude/ui/SearchOverlay.hpp>
#include "FtxuiColors.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>

namespace claude::ui {

using namespace ftxui_colors;

SearchOverlay::SearchOverlay() = default;

void SearchOverlay::activate() {
    active_ = true;
    searchQuery_.clear();
    results_.clear();
    currentMatch_ = -1;
}

void SearchOverlay::deactivate() {
    active_ = false;
    results_.clear();
    currentMatch_ = -1;
}

void SearchOverlay::nextMatch() {
    if (results_.empty()) return;
    currentMatch_ = (currentMatch_ + 1) % static_cast<int>(results_.size());
}

void SearchOverlay::prevMatch() {
    if (results_.empty()) return;
    currentMatch_ = (currentMatch_ - 1 + static_cast<int>(results_.size()))
                    % static_cast<int>(results_.size());
}

int SearchOverlay::highlightMessageIndex() const {
    if (currentMatch_ < 0 || currentMatch_ >= static_cast<int>(results_.size()))
        return -1;
    return results_[currentMatch_].messageIndex;
}

int SearchOverlay::highlightMatchOffset() const {
    if (currentMatch_ < 0 || currentMatch_ >= static_cast<int>(results_.size()))
        return -1;
    return results_[currentMatch_].matchOffset;
}

void SearchOverlay::performSearch() {
    results_.clear();
    currentMatch_ = -1;
    if (searchQuery_.empty() || !messages_) return;

    std::string query = searchQuery_;
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);

    for (size_t i = 0; i < messages_->size(); ++i) {
        auto& msg = (*messages_)[i];
        std::string text = msg.searchableText();

        std::string lowerText = text;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

        size_t pos = 0;
        while ((pos = lowerText.find(query, pos)) != std::string::npos) {
            SearchResult r;
            r.messageIndex = static_cast<int>(i);
            r.matchOffset = static_cast<int>(pos);
            // Extract context around match
            auto start = pos > 30 ? pos - 30 : 0;
            auto end = std::min(pos + query.size() + 30, text.size());
            r.contextLine = text.substr(start, end - start);
            results_.push_back(r);
            pos += 1; // Find all occurrences
        }
    }

    if (!results_.empty()) currentMatch_ = 0;
}

ftxui::Element SearchOverlay::OnRender() {
    using namespace ftxui;
    if (!active_) return text("");

    std::string countStr;
    if (!results_.empty()) {
        countStr = " " + std::to_string(currentMatch_ + 1) + "/" +
                   std::to_string(results_.size()) + " matches ";
    } else if (!searchQuery_.empty()) {
        countStr = " no matches ";
    }

    return hbox({
        text("Search: ") | color(MacSky),
        text(searchQuery_) | bold | color(MacPeach),
        text(countStr) | dim | color(MacCream),
        text(" Enter/Up/Down/Esc") | dim | color(MacShadow)
    });
}

bool SearchOverlay::OnEvent(ftxui::Event event) {
    if (!active_) return false;

    if (event == ftxui::Event::Escape) {
        deactivate();
        return true;
    }
    if (event == ftxui::Event::Return) {
        nextMatch();
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        prevMatch();
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        nextMatch();
        return true;
    }

    // Text input
    if (event.is_character()) {
        searchQuery_ += event.character();
        performSearch();
        return true;
    }
    if (event == ftxui::Event::Backspace && !searchQuery_.empty()) {
        searchQuery_.pop_back();
        performSearch();
        return true;
    }

    return false;
}

} // namespace claude::ui

#endif // HAS_FTXUI
