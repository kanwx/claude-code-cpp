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

Json serializeContentMessageForAnthropic(const ContentMessage& msg) {
    Json j;
    switch (msg.role) {
        case MessageRole::System:    j["role"] = "system"; break;
        case MessageRole::User:      j["role"] = "user"; break;
        case MessageRole::Assistant: j["role"] = "assistant"; break;
        case MessageRole::ToolResult: j["role"] = "user"; break;
    }
    Json contentArr = Json::array();
    for (auto& block : msg.content) {
        contentArr.push_back(serializeContentBlock(block, "anthropic"));
    }
    j["content"] = contentArr;
    return j;
}

Json buildAnthropicApiMessages(const std::vector<ContentMessage>& history) {
    Json messages = Json::array();
    for (size_t i = 0; i < history.size(); ++i) {
        auto& msg = history[i];
        if (msg.role == MessageRole::System) continue;

        auto serialized = serializeContentMessageForAnthropic(msg);

        // Merge consecutive tool-result-only messages (Anthropic requires tool_results in user messages,
        // and two consecutive user messages are invalid, so we merge when BOTH are tool-result-only)
        if (!messages.empty() && serialized["role"] == "user") {
            auto& last = messages.back();
            if (last["role"] == "user") {
                // Check if the previous message was tool-result-only
                bool lastAllToolResults = true;
                for (auto& b : last["content"]) {
                    if (b.value("type", "") != "tool_result") {
                        lastAllToolResults = false;
                        break;
                    }
                }
                // Check if current message is also tool-result-only
                bool curAllToolResults = true;
                for (auto& b : msg.content) {
                    if (b.type != ContentBlockParam::ToolResult) {
                        curAllToolResults = false;
                        break;
                    }
                }
                if (lastAllToolResults && curAllToolResults) {
                    for (auto& block : serialized["content"]) {
                        last["content"].push_back(block);
                    }
                    continue;
                }
            }
        }
        messages.push_back(serialized);
    }
    return messages;
}

Json serializeContentMessageForOpenAI(const ContentMessage& msg) {
    Json j;
    switch (msg.role) {
        case MessageRole::System:    j["role"] = "system"; break;
        case MessageRole::User:      j["role"] = "user"; break;
        case MessageRole::Assistant: j["role"] = "assistant"; break;
        case MessageRole::ToolResult: j["role"] = "tool"; break;
    }

    bool hasToolUse = false;
    for (auto& b : msg.content) {
        if (b.type == ContentBlockParam::ToolUse) { hasToolUse = true; break; }
    }

    if (hasToolUse) {
        j["content"] = msg.textContent();
        Json toolCalls = Json::array();
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolUse) {
                toolCalls.push_back(serializeContentBlock(block, "openai"));
            }
        }
        j["tool_calls"] = toolCalls;
    } else if (msg.role == MessageRole::ToolResult) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                j["tool_call_id"] = block.toolUseId;
                j["content"] = block.resultContent;
                break;
            }
        }
    } else {
        j["content"] = msg.textContent();
    }

    return j;
}

ContentMessage convertLegacyMessage(const Message& old) {
    ContentMessage result;
    result.role = old.role;
    result.timestamp = old.timestamp;
    result.apiRound = old.apiRound;

    if (old.thinking.has_value() && !old.thinking->empty()) {
        result.content.push_back(ContentBlockParam::makeThinking(
            *old.thinking, old.signature.value_or("")));
    }

    // When the legacy message carries toolResults, skip emitting msg.content as
    // a text block. Otherwise the text block lands before tool_result blocks in
    // the API message, violating the Anthropic constraint that a user message
    // after assistant tool_use must have tool_result blocks first.
    //
    // Two paths produce non-empty msg.content on tool-result-carrying messages:
    // 1. MicroCompact writes "[Old tool result content cleared...]" to both
    //    msg.content and each ToolResponse.content.
    // 2. PostCompactCleanup::enforceAlternation merges a regular User message
    //    with a ToolResult message, copying ToolResult's content.
    //
    // In both cases the tool_result blocks carry the essential information;
    // the text block is redundant at best, protocol-violating at worst.
    if (!old.content.empty() && old.toolResults.empty()) {
        result.content.push_back(ContentBlockParam::makeText(old.content));
    }

    for (auto& tc : old.toolCalls) {
        Json input = Json::object();
        try { input = Json::parse(tc.arguments); } catch (...) {}
        result.content.push_back(ContentBlockParam::makeToolUse(tc.id, tc.name, input));
    }

    for (auto& tr : old.toolResults) {
        auto block = ContentBlockParam::makeToolResult(tr.callId, tr.content, tr.isError);
        result.content.push_back(std::move(block));
    }

    for (auto& rt : old.redactedThinking) {
        result.content.push_back(ContentBlockParam::makeRedactedThinking(
            rt.value("data", rt.dump())));
    }

    return result;
}

void migrateLegacySession(Json& msg) {
    // Skip already-migrated format
    if (msg.value("_format_version", 0) >= 2) return;
    // Skip if content is already an array (new format)
    if (!msg.contains("content") || !msg["content"].is_string()) return;

    String flatContent = msg["content"].get<String>();
    auto role = msg.value("role", "");

    std::vector<Json> blocks;

    // 1. Thinking block (assistant only)
    if (role == "assistant" && msg.contains("thinking") && msg["thinking"].is_string()) {
        String thinkingText = msg["thinking"].get<String>();
        String signature = msg.value("signature", "");
        blocks.push_back({{"type", "thinking"}, {"thinking", thinkingText}, {"signature", signature}});
    }

    // 2. Text block
    if (!flatContent.empty()) {
        blocks.push_back({{"type", "text"}, {"text", flatContent}});
    }

    // 3. ToolUse blocks (assistant)
    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
        for (auto& tc : msg["tool_calls"]) {
            Json input = tc.value("input", Json::object());
            if (input.is_string()) {
                try { input = Json::parse(input.get<String>()); } catch (...) { input = Json::object(); }
            }
            // Handle both {id, name, input} and OpenAI {id, function:{name,arguments}} formats
            String name = tc.value("name", "");
            if (name.empty() && tc.contains("function")) {
                name = tc["function"].value("name", "");
                String args = tc["function"].value("arguments", "");
                if (!args.empty()) {
                    try { input = Json::parse(args); } catch (...) { input = Json::object(); }
                }
            }
            blocks.push_back({{"type", "tool_use"},
                {"id", tc.value("id", "")},
                {"name", name},
                {"input", input}});
        }
    }

    // 4. ToolResult blocks
    if (msg.contains("tool_results") && msg["tool_results"].is_array()) {
        for (auto& tr : msg["tool_results"]) {
            Json block = {{"type", "tool_result"},
                {"tool_use_id", tr.value("tool_use_id", tr.value("tool_call_id", tr.value("call_id", "")))},
                {"content", tr.value("content", "")}};
            if (tr.value("is_error", false)) block["is_error"] = true;
            blocks.push_back(block);
        }
    }

    // 5. RedactedThinking blocks (assistant)
    if (role == "assistant" && msg.contains("redacted_thinking") && msg["redacted_thinking"].is_array()) {
        for (auto& rt : msg["redacted_thinking"]) {
            blocks.push_back({{"type", "redacted_thinking"}, {"data", rt.value("data", rt.dump())}});
        }
    }

    // Replace content, remove old fields
    msg["content"] = blocks;
    msg.erase("tool_calls");
    msg.erase("tool_results");
    msg.erase("thinking");
    msg.erase("signature");
    msg.erase("redacted_thinking");
    msg["_format_version"] = 2;
}

} // namespace claude
