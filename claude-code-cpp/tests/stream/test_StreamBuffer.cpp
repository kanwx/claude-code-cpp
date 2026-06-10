#include <catch2/catch_test_macros.hpp>
#include "claude/stream/StreamBuffer.hpp"

using namespace claude;

TEST_CASE("TextParagraph emitted on blank line boundary", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "Hello world\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundParagraph = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TextParagraph) {
            CHECK(e.text.find("Hello world") != String::npos);
            foundParagraph = true;
        }
    }
    CHECK(foundParagraph);
}

TEST_CASE("Thinking never appears in text events", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::ThinkingDelta, .text = "I need to think about this"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "Here is the answer\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    for (auto& e : events) {
        if (e.type == DisplayEventType::TextParagraph || e.type == DisplayEventType::TextPartial) {
            CHECK(e.text.find("think about this") == String::npos);
        }
    }
}

TEST_CASE("ThinkingBlock emitted on StreamEnd", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::ThinkingDelta, .text = "Thinking content here"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundThinking = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::ThinkingBlock) {
            foundThinking = true;
        }
    }
    CHECK(foundThinking);
}

TEST_CASE("StreamEnd flushes remaining text as TextParagraph", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "Unfinished text"});
    size_t before = events.size();
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundFinalParagraph = false;
    for (auto i = before; i < events.size(); ++i) {
        if (events[i].type == DisplayEventType::TextParagraph) {
            CHECK(events[i].text.find("Unfinished text") != String::npos);
            foundFinalParagraph = true;
        }
    }
    CHECK(foundFinalParagraph);
}

TEST_CASE("ToolResult emitted on StreamToolEvent::Completed", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::ToolUseComplete,
                              .toolCall = ToolCall{.id = "call_1", .name = "Read", .arguments = "{}"}});
    buf.feed(StreamToolEvent{.type = StreamToolEventType::Completed, .toolCallId = "call_1", .toolName = "Read",
                       .summary = ToolResultSummary::success("Read 42 lines"), .rawResultPath = "/tmp/abc"});

    bool foundResult = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::ToolResult && e.toolCallId == "call_1") {
            CHECK(e.summary.primaryText == "Read 42 lines");
            CHECK(e.rawResultPath == "/tmp/abc");
            foundResult = true;
        }
    }
    CHECK(foundResult);
}

TEST_CASE("TurnMetadata emitted on StreamStart", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    bool foundMeta = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TurnMetadata || e.type == DisplayEventType::AnswerStart) {
            foundMeta = true;
        }
    }
    CHECK(foundMeta);
}

TEST_CASE("TurnMetadata never in content events", "[streambuffer]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::UsageUpdate});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    for (auto& e : events) {
        if (e.type == DisplayEventType::TextParagraph || e.type == DisplayEventType::TextPartial) {
            CHECK(e.text.find("Baked") == String::npos);
        }
    }
}
