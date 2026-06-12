#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "claude/core/ContentBlockParam.hpp"

using namespace claude;

// ========== Anthropic ContentMessage serialization ==========

TEST_CASE("Anthropic serializes user ContentMessage", "[serialize][anthropic]") {
    auto msg = ContentMessage::user("Hello");
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"].is_array());
    REQUIRE(j["content"].size() == 1);
    REQUIRE(j["content"][0]["type"] == "text");
    REQUIRE(j["content"][0]["text"] == "Hello");
}

TEST_CASE("Anthropic serializes assistant ContentMessage with toolUse", "[serialize][anthropic]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I will read the file."),
        ContentBlockParam::makeToolUse("toolu_123", "Read", Json::object({{"path", "/foo.txt"}}))
    });
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "assistant");
    REQUIRE(j["content"].size() == 2);
    REQUIRE(j["content"][0]["type"] == "text");
    REQUIRE(j["content"][0]["text"] == "I will read the file.");
    REQUIRE(j["content"][1]["type"] == "tool_use");
    REQUIRE(j["content"][1]["id"] == "toolu_123");
    REQUIRE(j["content"][1]["name"] == "Read");
    REQUIRE(j["content"][1]["input"]["path"] == "/foo.txt");
}

TEST_CASE("Anthropic serializes toolResult ContentMessage", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("toolu_456", "file contents here"));
    auto j = serializeContentMessageForAnthropic(msg);

    // ToolResult maps to user role in Anthropic API
    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"].size() == 1);
    REQUIRE(j["content"][0]["type"] == "tool_result");
    REQUIRE(j["content"][0]["tool_use_id"] == "toolu_456");
    REQUIRE(j["content"][0]["content"] == "file contents here");
}

TEST_CASE("Anthropic serializes toolResult with isError", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("toolu_789", "error occurred", true));
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"][0]["type"] == "tool_result");
    REQUIRE(j["content"][0]["is_error"] == true);
}

TEST_CASE("Anthropic serializes thinking blocks", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::Assistant;
    msg.content.push_back(ContentBlockParam::makeThinking("Let me think...", "sig123"));
    msg.content.push_back(ContentBlockParam::makeText("The answer is 42."));
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["content"][0]["type"] == "thinking");
    REQUIRE(j["content"][0]["thinking"] == "Let me think...");
    REQUIRE(j["content"][0]["signature"] == "sig123");
    REQUIRE(j["content"][1]["type"] == "text");
}

TEST_CASE("Anthropic serializes redacted_thinking blocks", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::Assistant;
    msg.content.push_back(ContentBlockParam::makeRedactedThinking("encrypted_data_here"));
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["content"][0]["type"] == "redacted_thinking");
    REQUIRE(j["content"][0]["data"] == "encrypted_data_here");
}

// ========== buildAnthropicApiMessages ==========

TEST_CASE("Anthropic merges consecutive toolResult messages", "[serialize][anthropic][merge]") {
    std::vector<ContentMessage> history;
    // First tool result message
    ContentMessage tr1;
    tr1.role = MessageRole::ToolResult;
    tr1.content.push_back(ContentBlockParam::makeToolResult("toolu_1", "result1"));
    history.push_back(tr1);

    // Second tool result message (consecutive)
    ContentMessage tr2;
    tr2.role = MessageRole::ToolResult;
    tr2.content.push_back(ContentBlockParam::makeToolResult("toolu_2", "result2"));
    history.push_back(tr2);

    auto messages = buildAnthropicApiMessages(history);
    // Should be merged into a single user message
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0]["role"] == "user");
    REQUIRE(messages[0]["content"].size() == 2);
    REQUIRE(messages[0]["content"][0]["tool_use_id"] == "toolu_1");
    REQUIRE(messages[0]["content"][1]["tool_use_id"] == "toolu_2");
}

TEST_CASE("Anthropic does NOT merge mixed user+toolResult messages", "[serialize][anthropic][merge]") {
    std::vector<ContentMessage> history;
    // User message with text + toolResult (mixed)
    ContentMessage mixed;
    mixed.role = MessageRole::User;
    mixed.content.push_back(ContentBlockParam::makeText("Here is the context"));
    mixed.content.push_back(ContentBlockParam::makeToolResult("toolu_1", "result1"));
    history.push_back(mixed);

    // Pure tool-result message
    ContentMessage tr;
    tr.role = MessageRole::ToolResult;
    tr.content.push_back(ContentBlockParam::makeToolResult("toolu_2", "result2"));
    history.push_back(tr);

    auto messages = buildAnthropicApiMessages(history);
    // The mixed message should NOT be merged with the following tool-result message
    REQUIRE(messages.size() == 2);
    REQUIRE(messages[0]["role"] == "user");
    REQUIRE(messages[1]["role"] == "user");
}

TEST_CASE("Anthropic skips system messages from messages array", "[serialize][anthropic]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::system("You are helpful."));
    history.push_back(ContentMessage::user("Hi"));

    auto messages = buildAnthropicApiMessages(history);
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0]["role"] == "user");
}

// ========== OpenAI ContentMessage serialization ==========

TEST_CASE("OpenAI serializes assistant with tool_calls", "[serialize][openai]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I will check."),
        ContentBlockParam::makeToolUse("call_abc", "Bash", Json::object({{"command", "ls"}}))
    });
    auto j = serializeContentMessageForOpenAI(msg);

    REQUIRE(j["role"] == "assistant");
    // content is text-only portion
    REQUIRE(j["content"] == "I will check.");
    // tool_calls is array of OpenAI-format function calls
    REQUIRE(j["tool_calls"].is_array());
    REQUIRE(j["tool_calls"].size() == 1);
    REQUIRE(j["tool_calls"][0]["type"] == "function");
    REQUIRE(j["tool_calls"][0]["id"] == "call_abc");
    REQUIRE(j["tool_calls"][0]["function"]["name"] == "Bash");
    REQUIRE(j["tool_calls"][0]["function"]["arguments"] == R"({"command":"ls"})");
}

TEST_CASE("OpenAI serializes tool result", "[serialize][openai]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("call_abc", "output here"));
    auto j = serializeContentMessageForOpenAI(msg);

    REQUIRE(j["role"] == "tool");
    REQUIRE(j["tool_call_id"] == "call_abc");
    REQUIRE(j["content"] == "output here");
}

// ========== convertLegacyMessage ==========

TEST_CASE("convertLegacyMessage converts flat content", "[serialize][legacy]") {
    Message old = Message::user("Hello world");
    auto result = convertLegacyMessage(old);

    REQUIRE(result.role == MessageRole::User);
    REQUIRE(result.content.size() == 1);
    REQUIRE(result.content[0].type == ContentBlockParam::Text);
    REQUIRE(result.content[0].text == "Hello world");
}

TEST_CASE("convertLegacyMessage converts tool_calls", "[serialize][legacy]") {
    Message old;
    old.role = MessageRole::Assistant;
    old.content = "I'll do it.";
    old.toolCalls.push_back({"call_1", "Read", R"({"path":"/foo"})"});
    auto result = convertLegacyMessage(old);

    REQUIRE(result.role == MessageRole::Assistant);
    // text + toolUse = 2 blocks
    REQUIRE(result.content.size() == 2);
    REQUIRE(result.content[1].type == ContentBlockParam::ToolUse);
    REQUIRE(result.content[1].id == "call_1");
    REQUIRE(result.content[1].name == "Read");
    REQUIRE(result.content[1].input["path"] == "/foo");
}

TEST_CASE("convertLegacyMessage converts toolResults", "[serialize][legacy]") {
    Message old = Message::toolResult({{"call_1", "Read", "file content", true}});
    auto result = convertLegacyMessage(old);

    REQUIRE(result.role == MessageRole::ToolResult);
    REQUIRE(result.content.size() == 1);
    REQUIRE(result.content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(result.content[0].toolUseId == "call_1");
    REQUIRE(result.content[0].resultContent == "file content");
    REQUIRE(result.content[0].isError == true);
}

TEST_CASE("convertLegacyMessage converts thinking", "[serialize][legacy]") {
    Message old;
    old.role = MessageRole::Assistant;
    old.content = "The answer.";
    old.thinking = "Let me reason...";
    old.signature = "sig_abc";
    auto result = convertLegacyMessage(old);

    // thinking + text = 2 blocks
    REQUIRE(result.content.size() == 2);
    REQUIRE(result.content[0].type == ContentBlockParam::Thinking);
    REQUIRE(result.content[0].thinking == "Let me reason...");
    REQUIRE(result.content[0].signature == "sig_abc");
}

TEST_CASE("convertLegacyMessage preserves block order", "[serialize][legacy]") {
    Message old;
    old.role = MessageRole::Assistant;
    old.thinking = "thinking text";
    old.signature = "sig";
    old.content = "response text";
    old.toolCalls.push_back({"tc_1", "Bash", R"({"cmd":"ls"})"});

    auto result = convertLegacyMessage(old);

    // Order: thinking -> text -> toolUse (API order)
    REQUIRE(result.content.size() == 3);
    REQUIRE(result.content[0].type == ContentBlockParam::Thinking);
    REQUIRE(result.content[1].type == ContentBlockParam::Text);
    REQUIRE(result.content[2].type == ContentBlockParam::ToolUse);
}
