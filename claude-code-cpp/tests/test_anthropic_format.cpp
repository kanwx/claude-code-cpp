#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <claude/core/Types.hpp>
#include <nlohmann/json.hpp>

using namespace claude;
using Catch::Matchers::ContainsSubstring;  // NOLINT

TEST_CASE("Message stores redacted thinking data", "[anthropic][thinking]") {
    Message msg = Message::assistant("I analyzed the code.");

    SECTION("Default has no redacted thinking") {
        REQUIRE(msg.redactedThinking.empty());
    }

    SECTION("Can store redacted thinking blocks") {
        Json rt1;
        rt1["type"] = "redacted_thinking";
        rt1["data"] = "encrypted_blob_123";
        msg.redactedThinking.push_back(rt1);

        REQUIRE(msg.redactedThinking.size() == 1);
        REQUIRE(msg.redactedThinking[0]["type"] == "redacted_thinking");
        REQUIRE(msg.redactedThinking[0]["data"] == "encrypted_blob_123");
    }

    SECTION("Thinking and signature fields work") {
        REQUIRE_FALSE(msg.hasThinking());

        msg.thinking = "Let me reason about this...";
        REQUIRE(msg.hasThinking());

        msg.signature = "sig_abc123";
        REQUIRE(msg.signature.has_value());
    }
}

TEST_CASE("Anthropic format conversion: assistant with tool_use", "[anthropic][format]") {
    // Simulate the format conversion that AnthropicClient::buildRequest does.
    // OpenAI format assistant message:
    Json openaiAssistant;
    openaiAssistant["role"] = "assistant";
    openaiAssistant["content"] = "Let me read that file.";
    openaiAssistant["tool_calls"] = Json::array();
    openaiAssistant["tool_calls"].push_back({
        {"id", "toolu_01ABC"},
        {"type", "function"},
        {"function", {{"name", "Read"}, {"arguments", "{\"file_path\":\"/tmp/test.txt\"}"}}}
    });

    // Convert to Anthropic content-block format
    Json contentBlocks = Json::array();
    contentBlocks.push_back({{"type", "text"}, {"text", "Let me read that file."}});

    Json toolUseBlock;
    toolUseBlock["type"] = "tool_use";
    toolUseBlock["id"] = "toolu_01ABC";
    toolUseBlock["name"] = "Read";
    toolUseBlock["input"] = Json::parse("{\"file_path\":\"/tmp/test.txt\"}");
    contentBlocks.push_back(toolUseBlock);

    Json anthropicAssistant;
    anthropicAssistant["role"] = "assistant";
    anthropicAssistant["content"] = contentBlocks;

    // Verify structure
    REQUIRE(anthropicAssistant["role"] == "assistant");
    REQUIRE(anthropicAssistant["content"].is_array());
    REQUIRE(anthropicAssistant["content"].size() == 2);
    REQUIRE(anthropicAssistant["content"][0]["type"] == "text");
    REQUIRE(anthropicAssistant["content"][1]["type"] == "tool_use");
    REQUIRE(anthropicAssistant["content"][1]["id"] == "toolu_01ABC");
    REQUIRE(anthropicAssistant["content"][1]["name"] == "Read");
    REQUIRE(anthropicAssistant["content"][1]["input"]["file_path"] == "/tmp/test.txt");
}

TEST_CASE("Anthropic format conversion: tool results into user message", "[anthropic][format]") {
    // OpenAI format: separate tool messages
    Json toolResult1, toolResult2;
    toolResult1["role"] = "tool";
    toolResult1["tool_call_id"] = "toolu_01ABC";
    toolResult1["content"] = "File contents here";

    toolResult2["role"] = "tool";
    toolResult2["tool_call_id"] = "toolu_02DEF";
    toolResult2["content"] = "Error: not found";
    toolResult2["is_error"] = true;

    // Convert: merge into single user message with tool_result content blocks
    Json contentBlocks = Json::array();
    contentBlocks.push_back({
        {"type", "tool_result"},
        {"tool_use_id", "toolu_01ABC"},
        {"content", "File contents here"}
    });
    contentBlocks.push_back({
        {"type", "tool_result"},
        {"tool_use_id", "toolu_02DEF"},
        {"content", "Error: not found"},
        {"is_error", true}
    });

    Json userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = contentBlocks;

    REQUIRE(userMsg["role"] == "user");
    REQUIRE(userMsg["content"].is_array());
    REQUIRE(userMsg["content"].size() == 2);
    REQUIRE(userMsg["content"][0]["type"] == "tool_result");
    REQUIRE(userMsg["content"][0]["tool_use_id"] == "toolu_01ABC");
    REQUIRE(userMsg["content"][1]["is_error"] == true);
}

TEST_CASE("Anthropic format conversion: assistant with thinking blocks", "[anthropic][format]") {
    // Assistant message with thinking and redacted_thinking
    Json contentBlocks = Json::array();

    // Thinking block
    contentBlocks.push_back({
        {"type", "thinking"},
        {"thinking", "Let me analyze this step by step..."},
        {"signature", "sig_abc"}
    });

    // Redacted thinking block
    contentBlocks.push_back({
        {"type", "redacted_thinking"},
        {"data", "encrypted_data_here"}
    });

    // Text block
    contentBlocks.push_back({{"type", "text"}, {"text", "Here is my answer."}});

    Json anthropicAssistant;
    anthropicAssistant["role"] = "assistant";
    anthropicAssistant["content"] = contentBlocks;

    REQUIRE(anthropicAssistant["content"].size() == 3);
    REQUIRE(anthropicAssistant["content"][0]["type"] == "thinking");
    REQUIRE(anthropicAssistant["content"][1]["type"] == "redacted_thinking");
    REQUIRE(anthropicAssistant["content"][1]["data"] == "encrypted_data_here");
    REQUIRE(anthropicAssistant["content"][2]["type"] == "text");
}

TEST_CASE("Truncated tool call generates synthetic error result", "[anthropic][tool-usage]") {
    // Simulate the truncated JSON handling
    String truncatedArgs = "{\"file_path\":\"/tmp/test.t\";";  // truncated - missing closing braces

    bool isValid = true;
    try {
        auto parsed = Json::parse(truncatedArgs);
        (void)parsed;
    } catch (...) {
        isValid = false;
    }

    REQUIRE_FALSE(isValid);

    // When truncated, we keep the tool call with empty args and generate error result
    ToolResponse syntheticError(
        "toolu_truncated",
        "Read",
        "Error: Tool call had truncated/malformed JSON arguments.",
        true
    );

    REQUIRE(syntheticError.isError);
    REQUIRE(syntheticError.callId == "toolu_truncated");
}
