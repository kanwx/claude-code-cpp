#pragma once

#include <claude/ui/UiMessageTypes.hpp>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace claude::ui {

class XmlTagDispatcher {
public:
    struct ParsedTag {
        std::string tagName;
        std::string content;
        std::map<std::string, std::string> attrs;
    };

    /// Determine the DisplayMessage::Type for a user text message.
    /// Parses the first XML-like tag and looks it up in the tag map.
    /// Returns UserPrompt for plain text (no tag) or unknown tags.
    static DisplayMessage::Type dispatch(const std::string& text);

    /// Parse the first XML-like tag from text.
    /// Supports `<tagName>content</tagName>` and `<tagName attr="val">content</tagName>`.
    /// Returns nullopt if no tag is found.
    static std::optional<ParsedTag> parseFirstTag(const std::string& text);

private:
    static const std::unordered_map<std::string, DisplayMessage::Type> tagMap_;
};

} // namespace claude::ui
