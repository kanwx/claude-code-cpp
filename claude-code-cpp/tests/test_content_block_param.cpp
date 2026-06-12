#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "claude/core/ContentBlockParam.hpp"

using namespace claude;

// ========== Factory tests ==========

TEST_CASE("ContentBlockParam makeText factory", "[content_block_param]") {
    auto block = ContentBlockParam::makeText("hello");
    REQUIRE(block.type == ContentBlockParam::Text);
    REQUIRE(block.text == "hello");
}

TEST_CASE("ContentBlockParam makeToolUse factory", "[content_block_param]") {
    auto input = Json::parse(R"({"path": "/tmp"})");
    auto block = ContentBlockParam::makeToolUse("toolu_123", "read_file", input);
    REQUIRE(block.type == ContentBlockParam::ToolUse);
    REQUIRE(block.id == "toolu_123");
    REQUIRE(block.name == "read_file");
    REQUIRE(block.input["path"] == "/tmp");
}

TEST_CASE("ContentBlockParam makeToolResult factory", "[content_block_param]") {
    auto block = ContentBlockParam::makeToolResult("toolu_123", "file contents here");
    REQUIRE(block.type == ContentBlockParam::ToolResult);
    REQUIRE(block.toolUseId == "toolu_123");
    REQUIRE(block.resultContent == "file contents here");
    REQUIRE(block.isError == false);
}

TEST_CASE("ContentBlockParam makeToolResult with error", "[content_block_param]") {
    auto block = ContentBlockParam::makeToolResult("toolu_456", "permission denied", true);
    REQUIRE(block.type == ContentBlockParam::ToolResult);
    REQUIRE(block.isError == true);
}

TEST_CASE("ContentBlockParam makeThinking factory", "[content_block_param]") {
    auto block = ContentBlockParam::makeThinking("Let me think...", "sig_abc");
    REQUIRE(block.type == ContentBlockParam::Thinking);
    REQUIRE(block.thinking == "Let me think...");
    REQUIRE(block.signature == "sig_abc");
}

TEST_CASE("ContentBlockParam makeRedactedThinking factory", "[content_block_param]") {
    auto block = ContentBlockParam::makeRedactedThinking("redacted_data_xyz");
    REQUIRE(block.type == ContentBlockParam::RedactedThinking);
    REQUIRE(block.redactedData == "redacted_data_xyz");
}

TEST_CASE("ContentBlockParam truncated flag", "[content_block_param]") {
    auto block = ContentBlockParam::makeToolResult("toolu_789", "long output...");
    block.truncated = true;
    REQUIRE(block.truncated == true);
}

TEST_CASE("ContentBlockParam cacheControl", "[content_block_param]") {
    auto block = ContentBlockParam::makeText("system prompt");
    block.cacheControl = CacheControl{"ephemeral", std::nullopt, CacheScope::Global};
    REQUIRE(block.cacheControl.has_value());
    REQUIRE(block.cacheControl->type == "ephemeral");
    REQUIRE(block.cacheControl->scope.has_value());
    REQUIRE(block.cacheControl->scope.value() == CacheScope::Global);
}

// ========== Serialization tests ==========

TEST_CASE("serializeContentBlock Anthropic text", "[content_block_param][serialize]") {
    auto block = ContentBlockParam::makeText("hello");
    auto j = serializeContentBlock(block, "anthropic");
    REQUIRE(j["type"] == "text");
    REQUIRE(j["text"] == "hello");
}

TEST_CASE("serializeContentBlock Anthropic toolUse", "[content_block_param][serialize]") {
    auto input = Json::parse(R"({"path": "/tmp"})");
    auto block = ContentBlockParam::makeToolUse("toolu_123", "read_file", input);
    auto j = serializeContentBlock(block, "anthropic");
    REQUIRE(j["type"] == "tool_use");
    REQUIRE(j["id"] == "toolu_123");
    REQUIRE(j["name"] == "read_file");
    REQUIRE(j["input"]["path"] == "/tmp");
}

TEST_CASE("serializeContentBlock OpenAI toolUse", "[content_block_param][serialize]") {
    auto input = Json::parse(R"({"path": "/tmp"})");
    auto block = ContentBlockParam::makeToolUse("toolu_123", "read_file", input);
    auto j = serializeContentBlock(block, "openai");
    REQUIRE(j["type"] == "function");
    REQUIRE(j["id"] == "toolu_123");
    REQUIRE(j["function"]["name"] == "read_file");
    // OpenAI: arguments must be a JSON string, not an object
    REQUIRE(j["function"]["arguments"].is_string());
    auto argsJson = Json::parse(j["function"]["arguments"].get<String>());
    REQUIRE(argsJson["path"] == "/tmp");
}

TEST_CASE("serializeContentBlock toolResult with isError", "[content_block_param][serialize]") {
    auto block = ContentBlockParam::makeToolResult("toolu_456", "error occurred", true);
    auto j = serializeContentBlock(block, "anthropic");
    REQUIRE(j["type"] == "tool_result");
    REQUIRE(j["tool_use_id"] == "toolu_456");
    REQUIRE(j["is_error"] == true);
}

TEST_CASE("serializeContentBlock toolResult truncated", "[content_block_param][serialize]") {
    auto block = ContentBlockParam::makeToolResult("toolu_789", "partial output");
    block.truncated = true;
    auto j = serializeContentBlock(block, "anthropic");
    REQUIRE(j["type"] == "tool_result");
    REQUIRE_THAT(j["content"].get<String>(), Catch::Matchers::ContainsSubstring("[truncated]"));
}

TEST_CASE("serializeContentBlock thinking", "[content_block_param][serialize]") {
    auto block = ContentBlockParam::makeThinking("deep thought", "sig_def");
    auto j = serializeContentBlock(block, "anthropic");
    REQUIRE(j["type"] == "thinking");
    REQUIRE(j["thinking"] == "deep thought");
    REQUIRE(j["signature"] == "sig_def");
}

TEST_CASE("serializeContentBlock redactedThinking", "[content_block_param][serialize]") {
    auto block = ContentBlockParam::makeRedactedThinking("secret_data");
    auto j = serializeContentBlock(block, "anthropic");
    REQUIRE(j["type"] == "redacted_thinking");
    REQUIRE(j["data"] == "secret_data");
}
