#include <claude/ui/XmlTagDispatcher.hpp>
#include <regex>

namespace claude::ui {

const std::unordered_map<std::string, DisplayMessage::Type> XmlTagDispatcher::tagMap_ = {
    {"bash-stdout",              DisplayMessage::Type::UserBashOutput},
    {"bash-stderr",              DisplayMessage::Type::UserBashOutput},
    {"bash-input",               DisplayMessage::Type::UserBashInput},
    {"command-message",          DisplayMessage::Type::UserCommandMessage},
    {"local-command-stdout",     DisplayMessage::Type::UserLocalCommandOutput},
    {"teammate-message",         DisplayMessage::Type::UserTeammateMessage},
    {"task-notification",        DisplayMessage::Type::UserTaskNotification},
    {"mcp-resource-update",      DisplayMessage::Type::UserMcpResourceUpdate},
    {"github-webhook-activity",  DisplayMessage::Type::UserGitHubWebhook},
    {"fork-boilerplate",         DisplayMessage::Type::UserForkBoilerplate},
    {"cross-session-message",    DisplayMessage::Type::UserCrossSessionMessage},
    {"channel",                  DisplayMessage::Type::UserChannelMessage},
    {"user-memory-input",        DisplayMessage::Type::UserMemoryInput},
};

DisplayMessage::Type XmlTagDispatcher::dispatch(const std::string& text) {
    auto parsed = parseFirstTag(text);
    if (!parsed) {
        return DisplayMessage::Type::UserPrompt;
    }
    auto it = tagMap_.find(parsed->tagName);
    if (it != tagMap_.end()) {
        return it->second;
    }
    return DisplayMessage::Type::UserPrompt;
}

std::optional<XmlTagDispatcher::ParsedTag> XmlTagDispatcher::parseFirstTag(const std::string& text) {
    // Match opening tag: <tagName> or <tagName attr="val" ...>
    // Tag names: lowercase letters, digits, hyphens
    static const std::regex openTagRegex(
        R"(<([a-z][a-z0-9-]*)((?:\s+[a-z][a-zA-Z0-9-]*="[^"]*")*)\s*>)",
        std::regex::optimize
    );

    std::smatch openMatch;
    if (!std::regex_search(text, openMatch, openTagRegex)) {
        return std::nullopt;
    }

    ParsedTag result;
    result.tagName = openMatch[1].str();

    // Parse attributes from the attribute string
    std::string attrStr = openMatch[2].str();
    if (!attrStr.empty()) {
        static const std::regex attrRegex(R"re(([a-z][a-zA-Z0-9-]*)="([^"]*)")re");
        std::sregex_iterator it(attrStr.begin(), attrStr.end(), attrRegex);
        std::sregex_iterator end;
        for (; it != end; ++it) {
            result.attrs[(*it)[1].str()] = (*it)[2].str();
        }
    }

    // Find the closing tag and extract content between them
    std::string closeTag = "</" + result.tagName + ">";
    auto closePos = text.find(closeTag, openMatch.position() + openMatch.length());
    if (closePos != std::string::npos) {
        auto contentStart = openMatch.position() + openMatch.length();
        result.content = text.substr(contentStart, closePos - contentStart);
    }

    return result;
}

} // namespace claude::ui
