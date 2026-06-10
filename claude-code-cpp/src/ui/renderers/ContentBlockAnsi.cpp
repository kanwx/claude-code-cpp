#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/console/AnsiStyle.hpp"

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
            String result = "  \xe2\x8e\xbf ";
            if (block.summary.isError) {
                result += String(AnsiStyle::RED) + block.summary.errorText + AnsiStyle::RESET;
            } else if (block.summary.isDim) {
                result += String(AnsiStyle::DIM) + block.summary.primaryText + AnsiStyle::RESET;
            } else if (block.summary.primaryBold) {
                result += String(AnsiStyle::BOLD) + block.summary.primaryText + AnsiStyle::RESET;
            } else {
                result += String(AnsiStyle::DIM) + block.summary.primaryText + AnsiStyle::RESET;
            }
            if (!block.expanded && !block.summary.isError && !block.summary.primaryText.empty()) {
                result += "  [Ctrl+O]";
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
