#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <claude/api/AnthropicClient.hpp>

using json = nlohmann::json;
using namespace claude;

TEST_CASE("StreamingState tracks partially-completed tool_use blocks", "[abort]") {
    StreamingState state;
    state.reset();
    state.currentBlockIndex = 0;
    state.currentBlockType = "tool_use";
    state.currentBlockId = "tu_partial_123";
    state.currentBlockName = "Bash";
    state.accumulatedInputJson = R"({"command": "ls")";
    REQUIRE(state.hasPartialBlocks());
}

TEST_CASE("StreamingState finalizePartialBlocks completes unfinished blocks", "[abort]") {
    StreamingState state;
    state.reset();
    state.currentBlockIndex = 0;
    state.currentBlockType = "tool_use";
    state.currentBlockId = "tu_partial_123";
    state.currentBlockName = "Bash";
    state.accumulatedInputJson = R"({"command": "ls"})";

    state.finalizePartialBlocks();
    REQUIRE_FALSE(state.contentBlocks.empty());
    REQUIRE(state.contentBlocks[0]["type"].get<String>() == "tool_use");
    REQUIRE(state.contentBlocks[0]["id"].get<String>() == "tu_partial_123");
}

TEST_CASE("StreamingState with no partial blocks reports false", "[abort]") {
    StreamingState state;
    state.reset();
    REQUIRE_FALSE(state.hasPartialBlocks());
}

TEST_CASE("StreamingState finalizePartialBlocks handles invalid JSON gracefully", "[abort]") {
    StreamingState state;
    state.reset();
    state.currentBlockIndex = 0;
    state.currentBlockType = "tool_use";
    state.currentBlockId = "tu_bad_json";
    state.currentBlockName = "Read";
    state.accumulatedInputJson = R"({"command": "ls)";  // truncated JSON

    state.finalizePartialBlocks();
    REQUIRE_FALSE(state.contentBlocks.empty());
    REQUIRE(state.contentBlocks[0]["type"].get<String>() == "tool_use");
    REQUIRE(state.contentBlocks[0]["input"].is_object());
    REQUIRE(state.contentBlocks[0]["partial"].get<bool>() == true);
}

TEST_CASE("StreamingState finalizePartialBlocks handles thinking blocks", "[abort]") {
    StreamingState state;
    state.reset();
    state.currentBlockIndex = 0;
    state.currentBlockType = "thinking";
    state.accumulatedText = "I need to analyze this code...";

    state.finalizePartialBlocks();
    REQUIRE_FALSE(state.contentBlocks.empty());
    REQUIRE(state.contentBlocks[0]["type"].get<String>() == "thinking");
    REQUIRE(state.contentBlocks[0]["thinking"].get<String>() == "I need to analyze this code...");
    REQUIRE(state.contentBlocks[0]["partial"].get<bool>() == true);
}

TEST_CASE("StreamingState finalizePartialBlocks clears accumulation state", "[abort]") {
    StreamingState state;
    state.reset();
    state.currentBlockIndex = 2;
    state.currentBlockType = "tool_use";
    state.currentBlockId = "tu_123";
    state.currentBlockName = "Bash";
    state.accumulatedInputJson = R"({"command":"echo hi"})";

    state.finalizePartialBlocks();
    REQUIRE(state.currentBlockIndex == -1);
    REQUIRE(state.currentBlockType.empty());
    REQUIRE(state.currentBlockId.empty());
    REQUIRE(state.currentBlockName.empty());
    REQUIRE(state.accumulatedInputJson.empty());
    REQUIRE_FALSE(state.hasPartialBlocks());
}
