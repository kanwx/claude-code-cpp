#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/ui/ToolResultFormatter.hpp"
#include "claude/console/AnsiStyle.hpp"
#include <sstream>

namespace claude {

String ContentBlockRenderer::renderAnsi(const ContentBlock& block) {
    switch (block.type) {
        case ContentBlock::UserMessage:
            return String(AnsiStyle::BOLD) + "> " + block.text + AnsiStyle::RESET;

        case ContentBlock::AnswerText:
            return block.text;

        case ContentBlock::ThinkingBlock:
            if (block.expanded) {
                return String(AnsiStyle::DIM) + "Thinking...\n" + block.detailText +
                       "\nThinking" + AnsiStyle::RESET;
            }
            return String(AnsiStyle::DIM) + "Thinking  (Ctrl+O to expand)" + AnsiStyle::RESET;

        case ContentBlock::ToolProgress:
            return String(AnsiStyle::DIM) + "  \xe2\x8e\xbf \xe2\x97\x8f " + block.activity + "..." + AnsiStyle::RESET;

        case ContentBlock::ToolResult: {
            auto dm = formatToolResult(block);
            String result = "  \xe2\x8e\xbf ";

            if (dm.isError) {
                String errDisplay = dm.errorText.empty()
                    ? "Error" : dm.errorText;
                result += String(AnsiStyle::RED) + errDisplay + AnsiStyle::RESET;
            }
            else if (dm.isCancelled || dm.isRejected) {
                String label = dm.isRejected ? "Rejected" : "Interrupted";
                result += String(AnsiStyle::DIM) + "\xe2\x8a\x98 " + label + AnsiStyle::RESET;
            }
            else {
                // Build per-tool display text
                String displayText;
                if (dm.toolName == "Read" && dm.lineCount > 0) {
                    displayText = dm.filePath.empty()
                        ? "Read " + std::to_string(dm.lineCount) + " lines"
                        : dm.filePath + " (" + std::to_string(dm.lineCount) + " lines)";
                }
                else if (dm.toolName == "Grep" && dm.matchCount > 0) {
                    displayText = "Found " + std::to_string(dm.matchCount) + " matches";
                }
                else if ((dm.toolName == "Glob" || dm.toolName == "LS") && dm.fileCount > 0) {
                    displayText = "Found " + std::to_string(dm.fileCount) + " files";
                }
                else if (dm.toolName == "Edit" || dm.toolName == "Write") {
                    std::ostringstream oss;
                    if (dm.linesAdded > 0) oss << "Added " << dm.linesAdded << " lines";
                    if (dm.linesRemoved > 0) {
                        if (oss.tellp() > 0) oss << ", ";
                        oss << "Removed " << dm.linesRemoved << " lines";
                    }
                    if (oss.tellp() == 0) oss << dm.primaryText;
                    displayText = oss.str();
                    if (!dm.filePath.empty()) {
                        displayText += "\n    " + dm.filePath;
                    }
                }
                else if (dm.toolName == "Bash") {
                    displayText = dm.primaryText.empty() ? "Done" : dm.primaryText;
                }
                else if (dm.toolName == "WebSearch" && dm.resultCount > 0) {
                    displayText = "Found " + std::to_string(dm.resultCount) + " results";
                }
                else if (dm.toolName == "WebFetch") {
                    displayText = dm.pageTitle.empty() ? "Fetched" : dm.pageTitle;
                }
                else {
                    displayText = dm.primaryText.empty() ? "completed" : dm.primaryText;
                }
                result += String(AnsiStyle::DIM) + displayText + AnsiStyle::RESET;
            }

            if (!block.expanded && !dm.isError && !dm.isCancelled && !dm.isRejected) {
                if (!dm.expandHint.empty()) {
                    result += "  " + dm.expandHint;
                } else {
                    result += "  [Ctrl+O]";
                }
            }
            return result;
        }

        case ContentBlock::ToolGroup: {
            String result = "  \xe2\x8e\xbf " + String(AnsiStyle::DIM) + block.summary.primaryText + AnsiStyle::RESET;
            if (!block.expanded) {
                result += "  [Ctrl+O]";
            } else {
                for (auto& child : block.children) {
                    result += "\n    " + renderAnsi(child);
                }
            }
            return result;
        }

        case ContentBlock::ErrorMessage:
            return String(AnsiStyle::RED) + "X " + block.text + AnsiStyle::RESET;

        case ContentBlock::AgentProgress:
            return String(AnsiStyle::DIM) + "  \xe2\x8e\xbf \xe2\x97\x8e " +
                   block.toolName + ": " + block.text + AnsiStyle::RESET;

        case ContentBlock::CollapsedGroup: {
            String result = "  \xe2\x8e\xbf " + String(AnsiStyle::DIM) +
                           block.summary.primaryText + AnsiStyle::RESET;
            if (!block.expanded) {
                result += "  [Ctrl+O]";
            } else {
                for (auto& child : block.children) {
                    result += "\n    \xe2\x94\x9c\xe2\x94\x80 " + renderAnsi(child);
                }
            }
            return result;
        }

        case ContentBlock::CompactBoundary:
            return String(AnsiStyle::DIM) + "  \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 " +
                   block.text + " \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80" + AnsiStyle::RESET;

        case ContentBlock::SystemMessage:
            return String(AnsiStyle::DIM) + "  \xe2\x8e\xbf " + block.text + AnsiStyle::RESET;

        case ContentBlock::TurnDuration:
            return String(AnsiStyle::DIM) + "  \xe2\x97\x8f " + block.text + AnsiStyle::RESET;

        default:
            return block.text;
    }
}

String ContentBlockRenderer::renderFtxuiText(const ContentBlock& block) {
    return renderAnsi(block);
}

String ContentBlockRenderer::toolBadge(const String& toolName) {
    return "[" + toolName + "]";
}

String ContentBlockRenderer::formatGroupSummary(const std::vector<ContentBlock>& children) {
    std::map<String, int> counts;
    for (auto& child : children) {
        counts[child.toolName]++;
    }
    String result;
    for (auto& [name, cnt] : counts) {
        if (!result.empty()) result += ", ";
        result += name + " " + std::to_string(cnt) + (cnt > 1 ? " files" : " file");
    }
    return result;
}

} // namespace claude
