#pragma once

#include <optional>
#include <chrono>
#include <vector>
#include <nlohmann/json.hpp>
#include "claude/core/ApiTypes.hpp"  // for CacheControl, CacheScope, MessageRole, String, Json

namespace claude {

struct ContentBlockParam {
    enum Type { Text, ToolUse, ToolResult, Thinking, RedactedThinking };
    Type type;

    // Text block
    String text;

    // ToolUse block
    String id;          // tool_use_id (e.g., "toolu_xxx")
    String name;        // tool name
    Json input;         // parsed JSON object

    // ToolResult block
    String toolUseId;   // matches ToolUse.id
    String resultContent;
    bool isError = false;
    bool truncated = false;

    // Thinking block
    String thinking;
    String signature;

    // RedactedThinking block
    String redactedData;

    // Cache control (optional, for system prompt blocks)
    std::optional<CacheControl> cacheControl;

    // Convenience constructors — use makeText/makeToolUse/etc. naming
    // (can't use "text" both as field name and static method name)
    static ContentBlockParam makeText(String t) {
        return {.type = Text, .text = std::move(t)};
    }
    static ContentBlockParam makeToolUse(String id, String name, Json input) {
        return {.type = ToolUse, .id = std::move(id), .name = std::move(name),
                .input = std::move(input)};
    }
    static ContentBlockParam makeToolResult(String toolUseId, String content, bool err = false) {
        return {.type = ToolResult, .toolUseId = std::move(toolUseId),
                .resultContent = std::move(content), .isError = err};
    }
    static ContentBlockParam makeThinking(String t, String sig) {
        return {.type = Thinking, .thinking = std::move(t), .signature = std::move(sig)};
    }
    static ContentBlockParam makeRedactedThinking(String data) {
        return {.type = RedactedThinking, .redactedData = std::move(data)};
    }
};

// New ContentBlock-based message type (replaces flat-string Message)
struct ContentMessage {
    MessageRole role;
    std::vector<ContentBlockParam> content;
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    int apiRound = 0;

    static ContentMessage user(String text) {
        ContentMessage m; m.role = MessageRole::User;
        m.content.push_back(ContentBlockParam::makeText(std::move(text)));
        return m;
    }
    static ContentMessage userBlocks(std::vector<ContentBlockParam> blocks) {
        ContentMessage m; m.role = MessageRole::User;
        m.content = std::move(blocks);
        return m;
    }
    static ContentMessage assistant(String text) {
        ContentMessage m; m.role = MessageRole::Assistant;
        m.content.push_back(ContentBlockParam::makeText(std::move(text)));
        return m;
    }
    static ContentMessage assistantBlocks(std::vector<ContentBlockParam> blocks) {
        ContentMessage m; m.role = MessageRole::Assistant;
        m.content = std::move(blocks);
        return m;
    }
    static ContentMessage system(String text) {
        ContentMessage m; m.role = MessageRole::System;
        m.content.push_back(ContentBlockParam::makeText(std::move(text)));
        return m;
    }
    static ContentMessage toolResultBlocks(std::vector<ContentBlockParam> blocks) {
        ContentMessage m; m.role = MessageRole::User;  // tool results go in user messages
        m.content = std::move(blocks);
        return m;
    }

    String textContent() const {
        String result;
        for (auto& b : content) {
            if (b.type == ContentBlockParam::Text && !b.text.empty()) {
                if (!result.empty()) result += " ";
                result += b.text;
            }
        }
        return result;
    }

    std::vector<ContentBlockParam> toolUseBlocks() const {
        std::vector<ContentBlockParam> result;
        for (auto& b : content) {
            if (b.type == ContentBlockParam::ToolUse) {
                result.push_back(b);
            }
        }
        return result;
    }

    bool hasContent() const {
        for (auto& b : content) {
            if (b.type == ContentBlockParam::Text && !b.text.empty()) return true;
            if (b.type == ContentBlockParam::ToolUse) return true;
            if (b.type == ContentBlockParam::ToolResult && !b.resultContent.empty()) return true;
            if (b.type == ContentBlockParam::RedactedThinking) return true;
        }
        return false;
    }

    bool hasToolCalls() const {
        for (auto& b : content) {
            if (b.type == ContentBlockParam::ToolUse) return true;
        }
        return false;
    }
};

// Shared serialization: provider-specific ContentBlockParam -> JSON
Json serializeContentBlock(const ContentBlockParam& block, const String& provider);

// ContentMessage -> provider-specific JSON
Json serializeContentMessageForAnthropic(const ContentMessage& msg);
Json buildAnthropicApiMessages(const std::vector<ContentMessage>& history);
Json serializeContentMessageForOpenAI(const ContentMessage& msg);

// Legacy Message -> ContentMessage conversion
ContentMessage convertLegacyMessage(const Message& old);

} // namespace claude
