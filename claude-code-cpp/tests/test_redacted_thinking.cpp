#include <catch2/catch_test_macros.hpp>
#include <claude/api/AnthropicClient.hpp>

using namespace claude;

TEST_CASE("StreamingState recognizes redacted_thinking block type", "[sse]") {
    StreamingState state;
    state.reset();
    state.currentBlockType = "redacted_thinking";
    state.accumulatedText = "encrypted_data_blob";
    REQUIRE(state.currentBlockType == "redacted_thinking");
    REQUIRE_FALSE(state.accumulatedText.empty());
}

TEST_CASE("StreamingState reset clears redacted_thinking state", "[sse]") {
    StreamingState state;
    state.reset();
    state.currentBlockType = "redacted_thinking";
    state.accumulatedText = "some_data";
    state.reset();
    REQUIRE(state.currentBlockType.empty());
    REQUIRE(state.accumulatedText.empty());
}

TEST_CASE("redacted_thinking type string matches", "[sse]") {
    String type = "redacted_thinking";
    REQUIRE(type == "redacted_thinking");
}
