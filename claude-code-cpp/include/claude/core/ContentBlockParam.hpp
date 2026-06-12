#pragma once

#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "claude/core/ApiTypes.hpp"  // for CacheControl, CacheScope, MessageRole, etc.

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

// Shared serialization: provider-specific ContentBlockParam -> JSON
Json serializeContentBlock(const ContentBlockParam& block, const String& provider);

} // namespace claude
