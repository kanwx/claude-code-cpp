#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/ui/ToolResultFormatter.hpp"
#include "claude/console/AnsiStyle.hpp"
#include "claude/console/AnsiSuppress.hpp"
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
                String displayText = dm.toDisplayText();
                // ANSI-specific: inline file path for Edit/Write
                if ((dm.toolName == "Edit" || dm.toolName == "Write") && !dm.filePath.empty()) {
                    displayText += "\n    " + dm.filePath;
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

String ContentBlockRenderer::renderPlain(const ContentBlock& block) {
    return stripAnsi(renderAnsi(block));
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
