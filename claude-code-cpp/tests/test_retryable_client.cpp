#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <claude/api/RetryableClient.hpp>

using json = nlohmann::json;
using namespace claude;

TEST_CASE("stripThinkingBlocks removes top-level thinking/signature", "[retry]") {
    json messages = json::array({
        {{"role", "assistant"}, {"thinking", "some thinking"}, {"signature", "sig123"}, {"content", "hello"}}
    });
    auto result = RetryableClient::stripThinkingBlocks(messages);
    REQUIRE_FALSE(result[0].contains("thinking"));
    REQUIRE_FALSE(result[0].contains("signature"));
    REQUIRE(result[0]["content"].get<std::string>() == "hello");
}

TEST_CASE("stripThinkingBlocks removes thinking content blocks", "[retry]") {
    json messages = json::array({
        {{"role", "assistant"}, {"content", json::array({
            {{"type", "thinking"}, {"thinking", "internal thought"}},
            {{"type", "text"}, {"text", "visible output"}}
        })}}
    });
    auto result = RetryableClient::stripThinkingBlocks(messages);
    auto& content = result[0]["content"];
    REQUIRE(content.size() == 1);
    REQUIRE(content[0]["type"].get<std::string>() == "text");
}

TEST_CASE("stripThinkingBlocks removes redacted_thinking content blocks", "[retry]") {
    json messages = json::array({
        {{"role", "assistant"}, {"content", json::array({
            {{"type", "redacted_thinking"}, {"data", "encrypted_blob"}},
            {{"type", "text"}, {"text", "output"}}
        })}}
    });
    auto result = RetryableClient::stripThinkingBlocks(messages);
    auto& content = result[0]["content"];
    REQUIRE(content.size() == 1);
    REQUIRE(content[0]["type"].get<std::string>() == "text");
}

TEST_CASE("stripThinkingBlocks removes signature content blocks", "[retry]") {
    json messages = json::array({
        {{"role", "assistant"}, {"content", json::array({
            {{"type", "thinking"}, {"thinking", "thought"}},
            {{"type", "signature"}, {"signature", "sig_data"}},
            {{"type", "text"}, {"text", "visible"}}
        })}}
    });
    auto result = RetryableClient::stripThinkingBlocks(messages);
    auto& content = result[0]["content"];
    REQUIRE(content.size() == 1);
    REQUIRE(content[0]["type"].get<std::string>() == "text");
}

TEST_CASE("stripThinkingBlocks preserves non-thinking blocks", "[retry]") {
    json messages = json::array({
        {{"role", "assistant"}, {"content", json::array({
            {{"type", "tool_use"}, {"id", "tu_1"}, {"name", "Bash"}, {"input", {{"command", "ls"}}}},
            {{"type", "text"}, {"text", "result"}}
        })}}
    });
    auto result = RetryableClient::stripThinkingBlocks(messages);
    REQUIRE(result[0]["content"].size() == 2);
}

TEST_CASE("stripThinkingBlocks handles empty array", "[retry]") {
    json messages = json::array();
    auto result = RetryableClient::stripThinkingBlocks(messages);
    REQUIRE(result.is_array());
    REQUIRE(result.empty());
}

TEST_CASE("RetryableClient FallbackInfo struct", "[retry]") {
    RetryableClient::FallbackInfo info;
    info.fromModel = "claude-opus";
    info.toModel = "claude-sonnet";
    REQUIRE(info.fromModel == "claude-opus");
    REQUIRE(info.toModel == "claude-sonnet");
    REQUIRE(info.fromBaseUrl.empty());
    REQUIRE(info.toBaseUrl.empty());
}
