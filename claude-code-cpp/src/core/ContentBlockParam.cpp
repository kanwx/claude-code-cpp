#include "claude/core/ContentBlockParam.hpp"

namespace claude {

Json serializeContentBlock(const ContentBlockParam& block, const String& provider) {
    Json j;
    switch (block.type) {
        case ContentBlockParam::Text:
            j = {{"type", "text"}, {"text", block.text}};
            break;
        case ContentBlockParam::ToolUse:
            if (provider == "anthropic") {
                j = {{"type", "tool_use"}, {"id", block.id},
                     {"name", block.name}, {"input", block.input}};
            } else {
                // OpenAI: "arguments" must be a JSON string, not an object
                j = {{"type", "function"}, {"id", block.id},
                     {"function", {{"name", block.name},
                                   {"arguments", block.input.dump()}}}};
            }
            break;
        case ContentBlockParam::ToolResult: {
            String content = block.resultContent;
            if (block.truncated) {
                content += "\n[truncated]";
            }
            j = {{"type", "tool_result"}, {"tool_use_id", block.toolUseId},
                 {"content", content}};
            if (block.isError) j["is_error"] = true;
            break;
        }
        case ContentBlockParam::Thinking:
            j = {{"type", "thinking"}, {"thinking", block.thinking},
                 {"signature", block.signature}};
            break;
        case ContentBlockParam::RedactedThinking:
            j = {{"type", "redacted_thinking"}, {"data", block.redactedData}};
            break;
    }
    return j;
}

} // namespace claude
