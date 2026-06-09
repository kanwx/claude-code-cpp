#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "claude/stream/TypedStreamEvent.hpp"

using namespace claude;

// ========== Default construction ==========

TEST_CASE("TypedStreamEvent default construction", "[stream]")
{
    TypedStreamEvent ev{};

    CHECK(ev.type == StreamEventType::TextDelta);
    CHECK(ev.text.empty());
    CHECK(ev.blockIndex == -1);
    CHECK(ev.toolCall.id.empty());
    CHECK(ev.toolCall.name.empty());
    CHECK(ev.toolCall.arguments.empty());
    CHECK(ev.usage.promptTokens == 0);
    CHECK(ev.usage.completionTokens == 0);
    CHECK(ev.usage.cacheReadTokens == 0);
    CHECK(ev.usage.cacheCreationTokens == 0);
    CHECK(ev.stopReason.empty());
    CHECK(ev.error.empty());
}

// ========== Designated initializer construction ==========

TEST_CASE("TypedStreamEvent designated initializer construction", "[stream]")
{
    TypedStreamEvent ev = {
        .type      = StreamEventType::ToolUseStart,
        .text      = "hello",
        .blockIndex = 3,
        .toolCall  = ToolCall{.id = "call_1", .name = "Read", .arguments = R"({"path":"/tmp"})"},
        .usage     = UsageInfo{.promptTokens = 100, .completionTokens = 50,
                               .cacheReadTokens = 30, .cacheCreationTokens = 10},
        .stopReason = "end_turn",
        .error     = ""
    };

    CHECK(ev.type == StreamEventType::ToolUseStart);
    CHECK(ev.text == "hello");
    CHECK(ev.blockIndex == 3);
    CHECK(ev.toolCall.id == "call_1");
    CHECK(ev.toolCall.name == "Read");
    CHECK(ev.toolCall.arguments == R"({"path":"/tmp"})");
    CHECK(ev.usage.promptTokens == 100);
    CHECK(ev.usage.completionTokens == 50);
    CHECK(ev.usage.cacheReadTokens == 30);
    CHECK(ev.usage.cacheCreationTokens == 10);
    CHECK(ev.stopReason == "end_turn");
    CHECK(ev.error.empty());
}

TEST_CASE("UsageInfo designated initializer partial", "[stream]")
{
    UsageInfo u = {.promptTokens = 42};
    CHECK(u.promptTokens == 42);
    CHECK(u.completionTokens == 0);
    CHECK(u.cacheReadTokens == 0);
    CHECK(u.cacheCreationTokens == 0);
}

// ========== Move semantics ==========

TEST_CASE("TypedStreamEvent move construction", "[stream]")
{
    TypedStreamEvent src = {
        .type      = StreamEventType::ThinkingDelta,
        .text      = "pondering",
        .blockIndex = 7,
        .toolCall  = ToolCall{.id = "c2", .name = "Write", .arguments = "{}"},
        .usage     = UsageInfo{.promptTokens = 200, .completionTokens = 80,
                               .cacheReadTokens = 60, .cacheCreationTokens = 20},
        .stopReason = "max_tokens",
        .error     = ""
    };

    TypedStreamEvent dst = std::move(src);

    CHECK(dst.type == StreamEventType::ThinkingDelta);
    CHECK(dst.text == "pondering");
    CHECK(dst.blockIndex == 7);
    CHECK(dst.toolCall.id == "c2");
    CHECK(dst.toolCall.name == "Write");
    CHECK(dst.usage.promptTokens == 200);
    CHECK(dst.stopReason == "max_tokens");
}

TEST_CASE("TypedStreamEvent move assignment", "[stream]")
{
    TypedStreamEvent a = {
        .type = StreamEventType::StreamEnd,
        .text = "fin",
        .stopReason = "end_turn"
    };
    TypedStreamEvent b{};

    b = std::move(a);

    CHECK(b.type == StreamEventType::StreamEnd);
    CHECK(b.text == "fin");
    CHECK(b.stopReason == "end_turn");
}

// ========== All 13 enum values exist ==========

TEST_CASE("StreamEventType all values are distinct", "[stream]")
{
    // Verify all 13 enum values compile and are distinct
    StreamEventType values[] = {
        StreamEventType::TextDelta,
        StreamEventType::ThinkingDelta,
        StreamEventType::InputJsonDelta,
        StreamEventType::ToolUseStart,
        StreamEventType::ToolUseComplete,
        StreamEventType::TextBlockStart,
        StreamEventType::TextBlockStop,
        StreamEventType::ThinkingBlockStart,
        StreamEventType::ThinkingBlockStop,
        StreamEventType::StreamStart,
        StreamEventType::StreamEnd,
        StreamEventType::UsageUpdate,
        StreamEventType::Error
    };

    constexpr int count = sizeof(values) / sizeof(values[0]);
    CHECK(count == 13);

    // Verify all values are distinct
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            CHECK(values[i] != values[j]);
        }
    }
}

TEST_CASE("TypedStreamEvent with Error type", "[stream]")
{
    TypedStreamEvent ev = {
        .type  = StreamEventType::Error,
        .error = "connection reset"
    };

    CHECK(ev.type == StreamEventType::Error);
    CHECK(ev.error == "connection reset");
    CHECK(ev.text.empty());
    CHECK(ev.blockIndex == -1);
}

TEST_CASE("TypedStreamEvent with UsageUpdate type", "[stream]")
{
    TypedStreamEvent ev = {
        .type  = StreamEventType::UsageUpdate,
        .usage = UsageInfo{.promptTokens = 1000, .completionTokens = 500,
                           .cacheReadTokens = 200, .cacheCreationTokens = 50}
    };

    CHECK(ev.type == StreamEventType::UsageUpdate);
    CHECK(ev.usage.promptTokens == 1000);
    CHECK(ev.usage.completionTokens == 500);
}
