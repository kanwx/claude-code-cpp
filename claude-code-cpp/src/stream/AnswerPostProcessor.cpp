#include "claude/stream/AnswerPostProcessor.hpp"
#include <algorithm>

namespace claude {

const std::vector<String> AnswerPostProcessor::COLLAPSIBLE_TOOLS = {
    "Read", "Grep", "Glob", "LS", "Bash", "WebFetch", "WebSearch",
    "memory_read", "memory_write"
};

DisplayEvent AnswerPostProcessor::process(DisplayEvent&& event) {
    cleanThinkingTags(event);
    events_.push_back(event);
    return DisplayEvent{events_.back()};
}

std::vector<DisplayEvent> AnswerPostProcessor::finalize() {
    auto grouped = groupConsecutiveToolResults();
    auto reordered = reorderToolTrails();
    events_.clear();
    return reordered;
}

void AnswerPostProcessor::reset() {
    events_.clear();
}

void AnswerPostProcessor::cleanThinkingTags(DisplayEvent& event) {
    if (event.type != DisplayEventType::TextParagraph && event.type != DisplayEventType::TextPartial) return;
    static const String openTag = "<thinking>";
    static const String closeTag = "</thinking>";
    size_t pos;
    while ((pos = event.text.find(openTag)) != String::npos) {
        size_t endPos = event.text.find(closeTag, pos);
        if (endPos != String::npos) {
            event.text.erase(pos, endPos + closeTag.size() - pos);
        } else {
            event.text.erase(pos);
            break;
        }
    }
}

std::vector<DisplayEvent> AnswerPostProcessor::groupConsecutiveToolResults() {
    std::vector<DisplayEvent> result;
    size_t i = 0;
    while (i < events_.size()) {
        if (events_[i].type == DisplayEventType::ToolResult && isCollapsibleTool(events_[i].toolName)) {
            size_t start = i;
            while (i < events_.size() &&
                   events_[i].type == DisplayEventType::ToolResult &&
                   isCollapsibleTool(events_[i].toolName)) {
                ++i;
            }
            size_t count = i - start;

            if (count >= 2) {
                for (size_t j = start; j < i; ++j) {
                    result.push_back(DisplayEvent{.type = DisplayEventType::Tombstone, .toolCallId = events_[j].toolCallId});
                }
                DisplayEvent group{.type = DisplayEventType::ToolGroup};
                std::map<String, int> counts;
                for (size_t j = start; j < i; ++j) {
                    counts[events_[j].toolName]++;
                }
                String summaryText;
                for (auto& [name, cnt] : counts) {
                    if (!summaryText.empty()) summaryText += ", ";
                    summaryText += name + " " + std::to_string(cnt) + (cnt > 1 ? " times" : " time");
                }
                group.summary = ToolResultSummary::success(summaryText);
                group.toolName = "Group";
                result.push_back(std::move(group));
            } else {
                result.push_back(std::move(events_[start]));
            }
        } else {
            result.push_back(std::move(events_[i]));
            ++i;
        }
    }
    events_ = std::move(result);
    return events_;
}

std::vector<DisplayEvent> AnswerPostProcessor::reorderToolTrails() {
    std::vector<DisplayEvent> textEvents;
    std::vector<DisplayEvent> toolEvents;
    std::vector<DisplayEvent> otherEvents;

    for (auto& e : events_) {
        if (e.type == DisplayEventType::TextParagraph || e.type == DisplayEventType::TextPartial) {
            textEvents.push_back(std::move(e));
        } else if (e.type == DisplayEventType::ToolResult || e.type == DisplayEventType::ToolGroup) {
            toolEvents.push_back(std::move(e));
        } else {
            otherEvents.push_back(std::move(e));
        }
    }

    std::vector<DisplayEvent> result;
    for (auto& e : otherEvents) {
        if (e.type != DisplayEventType::AnswerEnd) {
            result.push_back(std::move(e));
        }
    }
    for (auto& e : textEvents) result.push_back(std::move(e));
    for (auto& e : toolEvents) result.push_back(std::move(e));
    for (auto& e : otherEvents) {
        if (e.type == DisplayEventType::AnswerEnd) {
            result.push_back(std::move(e));
        }
    }
    return result;
}

bool AnswerPostProcessor::isCollapsibleTool(const String& name) {
    for (auto& t : COLLAPSIBLE_TOOLS) {
        if (name == t) return true;
    }
    if (name.find("mcp_") == 0 || name.find('_') != String::npos) {
        return isCollapsibleToolName(name);
    }
    return false;
}

bool AnswerPostProcessor::isCollapsibleToolName(const String& name) {
    if (name.find("write_") == 0 || name.find("update_") == 0 ||
        name.find("delete_") == 0 || name.find("create_") == 0) {
        return false;
    }
    return true;
}

} // namespace claude
