#include <catch2/catch_test_macros.hpp>
#include "claude/stream/StreamBuffer.hpp"
#include <sstream>

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

TEST_CASE("TextDelta with complete <think> tags stripped", "[streambuffer][think]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "<think>internal reasoning</think>real response\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundThink = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TextPartial || e.type == DisplayEventType::TextParagraph) {
            INFO("Text event content: " << e.text);
            if (e.text.find("<think>") != String::npos || e.text.find("</think>") != String::npos) {
                foundThink = true;
            }
            if (e.text.find("internal reasoning") != String::npos) {
                foundThink = true;
            }
        }
    }
    CHECK_FALSE(foundThink);
}

TEST_CASE("TextDelta with <think> split across chunks", "[streambuffer][think]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "<thin"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "k>internal reasoning</think>real response\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundThink = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TextPartial || e.type == DisplayEventType::TextParagraph) {
            INFO("Text event: [" << e.text << "]");
            if (e.text.find("<thin") != String::npos ||
                e.text.find("k>") != String::npos ||
                e.text.find("</think>") != String::npos ||
                e.text.find("internal reasoning") != String::npos) {
                foundThink = true;
            }
        }
    }
    CHECK_FALSE(foundThink);
}

TEST_CASE("TextDelta with <think> multi-chunk streaming", "[streambuffer][think]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "<think>"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "internal reasoning part 1"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "internal reasoning part 2</think>"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "real response\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundLeak = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TextPartial || e.type == DisplayEventType::TextParagraph) {
            INFO("Text event: [" << e.text << "]");
            if (e.text.find("</think>") != String::npos ||
                e.text.find("internal reasoning") != String::npos) {
                foundLeak = true;
            }
        }
    }
    CHECK_FALSE(foundLeak);
}

TEST_CASE("TextDelta with </think> in new turn cross-turn leak", "[streambuffer][think]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    // Turn 1: thinking starts but gets interrupted (no close tag)
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "<think>thinking started"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    // Turn 2: model resumes, closes the think tag
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "more thinking</think>real response\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    bool foundLeak = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TextPartial || e.type == DisplayEventType::TextParagraph) {
            INFO("Text event: [" << e.text << "]");
            if (e.text.find("</think>") != String::npos ||
                e.text.find("thinking started") != String::npos ||
                e.text.find("more thinking") != String::npos) {
                foundLeak = true;
            }
        }
    }
    CHECK_FALSE(foundLeak);
}

// ---- P0: TextPartial + TextParagraph dedup semantics ----

// Helper: apply the dedup algorithm (same as ANSI handler in AgentRunner.cpp)
static std::string dedupStreamEvents(const std::vector<DisplayEvent>& events) {
    std::ostringstream oss;
    size_t emittedLen = 0;
    for (const auto& e : events) {
        if (e.type == DisplayEventType::TextPartial) {
            oss << e.text;
            emittedLen += e.text.size();
        } else if (e.type == DisplayEventType::TextParagraph) {
            if (emittedLen < e.text.size()) {
                oss << e.text.substr(emittedLen);
            }
            oss << "\n";
            emittedLen = 0;
        }
    }
    return oss.str();
}

TEST_CASE("dedup: TextPartial + TextParagraph does not duplicate", "[streambuffer][dedup]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});

    // Feed > FLUSH_THRESHOLD (256) chars without paragraph boundary to
    // trigger TextPartial emission, then a paragraph boundary to trigger
    // TextParagraph.
    std::string block1(70, 'A');  block1 += " ";   // 71 chars
    std::string block2(70, 'B');  block2 += " ";   // 71 chars
    std::string block3(70, 'C');  block3 += " ";   // 71 chars
    std::string block4(70, 'D');  block4 += " ";   // 71 chars
    // block1+2+3 = 213 chars (under 256, no TextPartial trigger yet)
    // + block4 = 284 chars (over 256, should trigger TextPartial)
    std::string allText = block1 + block2 + block3 + block4;
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = block1});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = block2});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = block3});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = block4});
    // Trigger paragraph boundary
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    std::string deduped = dedupStreamEvents(events);
    // Deduped text should end with the original text (plus trailing newlines from paragraph markers)
    INFO("allText size: " << allText.size());
    INFO("deduped: [" << deduped << "]");
    // The deduped text should contain the original text exactly once
    CHECK(deduped.find(allText) == 0);
    // And should NOT contain it twice
    size_t first = deduped.find(allText);
    size_t second = deduped.find(allText, first + 1);
    CHECK(second == std::string::npos);
}

TEST_CASE("dedup: TextParagraph-only does not drop text", "[streambuffer][dedup]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    // Short text with immediate boundary — no TextPartial should fire
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "Short answer\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    // Verify no TextPartial events
    bool hasTextPartial = false;
    for (auto& e : events) {
        if (e.type == DisplayEventType::TextPartial) {
            hasTextPartial = true;
        }
    }
    CHECK_FALSE(hasTextPartial);

    std::string deduped = dedupStreamEvents(events);
    CHECK(deduped.find("Short answer") != std::string::npos);
}

TEST_CASE("dedup: multi-paragraph does not cross-contaminate", "[streambuffer][dedup]") {
    StreamBuffer buf;
    std::vector<DisplayEvent> events;
    buf.setDisplayCallback([&](DisplayEvent&& e) { events.push_back(std::move(e)); });

    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamStart});
    // P1: short paragraph
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "First paragraph.\n\n"});
    // P2: long paragraph with TextPartial
    std::string p2(100, 'X');
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = p2});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = p2});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = p2});
    buf.feed(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = "\n\n"});
    buf.feed(TypedStreamEvent{.type = StreamEventType::StreamEnd});

    std::string deduped = dedupStreamEvents(events);
    // "First paragraph." should appear exactly once
    size_t fp1 = deduped.find("First paragraph.");
    size_t fp2 = deduped.find("First paragraph.", fp1 + 1);
    CHECK(fp2 == std::string::npos);
    // The second paragraph content should appear exactly once
    std::string expectedP2 = p2 + p2 + p2;
    size_t p2_first = deduped.find(expectedP2);
    size_t p2_second = deduped.find(expectedP2, p2_first + 1);
    CHECK(p2_second == std::string::npos);
}

TEST_CASE("dedup: consecutive TextPartial chunks accumulate correctly", "[streambuffer][dedup]") {
    // Direct unit test of the dedup algorithm without StreamBuffer:
    // TextPartial("abc") + TextPartial("def") + TextParagraph("abcdef")
    // Expected: "abcdef\n"
    std::vector<DisplayEvent> events;
    events.push_back(DisplayEvent::textPartial("abc"));
    events.push_back(DisplayEvent::textPartial("def"));
    events.push_back(DisplayEvent::textParagraph("abcdef"));

    std::string deduped = dedupStreamEvents(events);
    INFO("deduped: [" << deduped << "]");
    CHECK(deduped == "abcdef\n");
}

TEST_CASE("dedup: TextParagraph-only fallback works", "[streambuffer][dedup]") {
    // TextParagraph("abcdef")
    // Expected: "abcdef\n"
    std::vector<DisplayEvent> events;
    events.push_back(DisplayEvent::textParagraph("abcdef"));

    std::string deduped = dedupStreamEvents(events);
    INFO("deduped: [" << deduped << "]");
    CHECK(deduped == "abcdef\n");
}

TEST_CASE("dedup: multi-paragraph sequence", "[streambuffer][dedup]") {
    // Partial "abc" + Paragraph "abc" + Partial "def" + Paragraph "def"
    // Expected: "abc\ndef\n"
    std::vector<DisplayEvent> events;
    events.push_back(DisplayEvent::textPartial("abc"));
    events.push_back(DisplayEvent::textParagraph("abc"));
    events.push_back(DisplayEvent::textPartial("def"));
    events.push_back(DisplayEvent::textParagraph("def"));

    std::string deduped = dedupStreamEvents(events);
    INFO("deduped: [" << deduped << "]");
    CHECK(deduped == "abc\ndef\n");
}
