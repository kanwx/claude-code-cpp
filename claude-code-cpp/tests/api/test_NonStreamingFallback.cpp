#include <catch2/catch_test_macros.hpp>
#include "claude/api/AnthropicClient.hpp"
#include "claude/stream/TypedStreamEvent.hpp"
#include <nlohmann/json.hpp>

using Json = nlohmann::json;
using namespace claude;

TEST_CASE("convertNonStreamingResponse splits text on paragraph boundaries", "[api]") {
    Json response = {
        {"content", Json::array({
            {{"type", "text"}, {"text", "Paragraph one.\n\nParagraph two.\n\nParagraph three."}},
            {{"type", "tool_use"}, {"id", "call_1"}, {"name", "Read"}, {"input", {{"path", "/tmp"}}}}
        })},
        {"stop_reason", "end_turn"},
        {"usage", {{"input_tokens", 100}, {"output_tokens", 50}}}
    };
    auto events = AnthropicClient::convertNonStreamingResponse(response);
    int textDeltas = 0;
    int toolUseCompletes = 0;
    for (auto& e : events) {
        if (e.type == StreamEventType::TextDelta) textDeltas++;
        if (e.type == StreamEventType::ToolUseComplete) toolUseCompletes++;
    }
    CHECK(textDeltas == 3);
    CHECK(toolUseCompletes == 1);
    CHECK(events.front().type == StreamEventType::StreamStart);
    CHECK(events.back().type == StreamEventType::StreamEnd);
}

TEST_CASE("convertNonStreamingResponse handles thinking blocks", "[api]") {
    Json response = {
        {"content", Json::array({
            {{"type", "thinking"}, {"thinking", "Let me analyze this..."}}
        })},
        {"stop_reason", "end_turn"},
        {"usage", {{"input_tokens", 100}, {"output_tokens", 50}}}
    };
    auto events = AnthropicClient::convertNonStreamingResponse(response);
    bool hasThinking = false;
    for (auto& e : events) {
        if (e.type == StreamEventType::ThinkingDelta) hasThinking = true;
    }
    CHECK(hasThinking);
}

TEST_CASE("convertNonStreamingResponse empty content", "[api]") {
    Json response = {
        {"content", Json::array()},
        {"stop_reason", "end_turn"}
    };
    auto events = AnthropicClient::convertNonStreamingResponse(response);
    CHECK(events.front().type == StreamEventType::StreamStart);
    CHECK(events.back().type == StreamEventType::StreamEnd);
}
