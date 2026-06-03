#include <claude/ui/UiMessageTypes.hpp>

namespace claude {

// ========== Tool Pairing ==========

void buildToolPairing(std::vector<DisplayMessage>& messages) {
    // Build a lookup: toolId -> index of the ToolUse message
    std::unordered_map<String, size_t> toolUseIndex;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].type == DisplayMessage::Type::AssistantToolUse) {
            toolUseIndex[messages[i].toolUse.toolId] = i;
        }
    }

    // For each ToolResult, set the toolUseId and also update the
    // corresponding ToolUse message's toolId if it was placeholder
    for (auto& msg : messages) {
        if (msg.type == DisplayMessage::Type::UserToolResult) {
            // Try to find the matching ToolUse by toolName (fallback)
            // The toolUseId should ideally be set from the API,
            // but if it's empty, we scan backwards for a match
            if (msg.toolResult.toolUseId.empty()) {
                for (int i = static_cast<int>(&msg - &messages[0]) - 1; i >= 0; --i) {
                    if (messages[i].type == DisplayMessage::Type::AssistantToolUse &&
                        messages[i].toolUse.toolName == msg.toolResult.toolName) {
                        msg.toolResult.toolUseId = messages[i].toolUse.toolId;
                        break;
                    }
                }
            }
        }
    }
}

// ========== Height Estimation ==========

int estimateMessageHeight(const DisplayMessage& msg, int terminalWidth) {
    if (terminalWidth <= 0) terminalWidth = 80;

    switch (msg.type) {
        case DisplayMessage::Type::UserPrompt:
            return 1 + static_cast<int>(msg.text.size()) / terminalWidth;

        case DisplayMessage::Type::AssistantText:
            return std::max(2, 1 + static_cast<int>(msg.text.size()) / (terminalWidth - 3));

        case DisplayMessage::Type::AssistantThinking:
            if (msg.expanded) {
                return 2 + static_cast<int>(msg.thinking.text.size()) / (terminalWidth - 4);
            }
            return 1;

        case DisplayMessage::Type::AssistantToolUse: {
            int h = 2;  // Tool badge line + input summary
            if (!msg.toolUse.input.empty()) {
                h += static_cast<int>(msg.toolUse.input.size()) / (terminalWidth - 6);
            }
            return h;
        }

        case DisplayMessage::Type::UserToolResult: {
            int h = 2;
            if (!msg.toolResult.result.empty()) {
                bool hasDiff = msg.toolResult.result.find("\n-") != String::npos ||
                               msg.toolResult.result.find("\n+") != String::npos;
                if (hasDiff) {
                    // Count lines in diff
                    int lines = 1;
                    for (char c : msg.toolResult.result) {
                        if (c == '\n') lines++;
                    }
                    h = lines;
                } else {
                    h += static_cast<int>(msg.toolResult.result.size()) / (terminalWidth - 6);
                }
            }
            return h;
        }

        case DisplayMessage::Type::SystemInfo:
        case DisplayMessage::Type::SystemError:
        case DisplayMessage::Type::TurnDuration:
            return 1;

        case DisplayMessage::Type::PermissionPrompt:
            return 8;  // Header + tool name + activity + blank + 4 options + blank

        case DisplayMessage::Type::CollapsedReadSearch:
            return msg.expanded ? static_cast<int>(msg.collapsedGroup.toolIndices.size()) * 2 + 2 : 2;

        case DisplayMessage::Type::GroupedToolUse:
            return msg.expanded ? static_cast<int>(msg.groupedTools.size()) * 2 + 2 : 2;

        case DisplayMessage::Type::CompactBoundary:
        case DisplayMessage::Type::HookSummary:
        case DisplayMessage::Type::AgentProgress:
        case DisplayMessage::Type::UserToolSuccess:
        case DisplayMessage::Type::UserToolRejected:
        case DisplayMessage::Type::UserToolCanceled:
        case DisplayMessage::Type::AssistantRedactedThinking:
        // P1: XML-tag dispatched types
        case DisplayMessage::Type::UserBashOutput:
        case DisplayMessage::Type::UserBashInput:
        case DisplayMessage::Type::UserCommandMessage:
        case DisplayMessage::Type::UserLocalCommandOutput:
        case DisplayMessage::Type::UserTeammateMessage:
        case DisplayMessage::Type::UserTaskNotification:
        case DisplayMessage::Type::UserMcpResourceUpdate:
        case DisplayMessage::Type::UserGitHubWebhook:
        case DisplayMessage::Type::UserForkBoilerplate:
        case DisplayMessage::Type::UserCrossSessionMessage:
        case DisplayMessage::Type::UserChannelMessage:
        case DisplayMessage::Type::UserMemoryInput:
        // P2: Server tool use / Image attachment
        case DisplayMessage::Type::AssistantServerToolUse:
        case DisplayMessage::Type::UserImageAttachment:
            return 1;

        case DisplayMessage::Type::UserToolError:
            return 2;
    }
    return 1;
}

// ========== Searchable Text Extraction ==========

std::string DisplayMessage::searchableText() const {
    switch (type) {
        case Type::UserPrompt:
        case Type::SystemInfo:
        case Type::SystemError:
        case Type::TurnDuration:
        case Type::HookSummary:
        case Type::AgentProgress:
            return text;
        case Type::AssistantText:
            return text;
        case Type::AssistantThinking:
            return thinking.text;
        case Type::AssistantToolUse:
            return toolUse.toolName + " " + toolUse.input;
        case Type::UserToolResult:
        case Type::UserToolSuccess:
        case Type::UserToolError:
            return toolResult.toolName + " " + toolResult.result;
        case Type::CollapsedReadSearch:
            return collapsedGroup.summaryText();
        default:
            return text;
    }
}

// ========== Read/Search Tool Detection ==========

bool isReadSearchTool(const String& toolName) {
    return toolName == "Read" || toolName == "Grep" || toolName == "Glob" ||
           toolName == "Bash" ||
           toolName == "GlobTool" || toolName == "GrepTool" || toolName == "FileReadTool" ||
           toolName == "BashTool";
}

} // namespace claude
