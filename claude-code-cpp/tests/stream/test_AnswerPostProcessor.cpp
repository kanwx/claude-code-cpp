#include <catch2/catch_test_macros.hpp>
#include "claude/stream/AnswerPostProcessor.hpp"

using namespace claude;

TEST_CASE("Process passes through TextParagraph unchanged", "[postprocessor]") {
    AnswerPostProcessor proc;
    auto result = proc.process(DisplayEvent{.type = DisplayEventType::TextParagraph, .text = "Hello world"});
    CHECK(result.type == DisplayEventType::TextParagraph);
    CHECK(result.text == "Hello world");
}

TEST_CASE("Process strips thinking tags from text", "[postprocessor]") {
    AnswerPostProcessor proc;
    auto result = proc.process(DisplayEvent{.type = DisplayEventType::TextParagraph, .text = "<thinking>leaked</thinking>Real text"});
    CHECK(result.text.find("<thinking>") == String::npos);
    CHECK(result.text.find("Real text") != String::npos);
}

TEST_CASE("Finalize groups consecutive collapsible tool results", "[postprocessor]") {
    AnswerPostProcessor proc;
    proc.process(DisplayEvent{.type = DisplayEventType::ToolResult, .toolCallId = "1", .toolName = "Read", .summary = ToolResultSummary::success("Read 42 lines")});
    proc.process(DisplayEvent{.type = DisplayEventType::ToolResult, .toolCallId = "2", .toolName = "Read", .summary = ToolResultSummary::success("Read 15 lines")});
    proc.process(DisplayEvent{.type = DisplayEventType::ToolResult, .toolCallId = "3", .toolName = "Write", .summary = ToolResultSummary::success("Wrote file")});

    auto final_events = proc.finalize();

    int tombstones = 0;
    int groups = 0;
    int individual = 0;
    for (auto& e : final_events) {
        if (e.type == DisplayEventType::Tombstone) tombstones++;
        if (e.type == DisplayEventType::ToolGroup) groups++;
        if (e.type == DisplayEventType::ToolResult) individual++;
    }
    CHECK(tombstones == 2);
    CHECK(groups == 1);
    CHECK(individual == 1);
}

TEST_CASE("Finalize does not group non-collapsible tools", "[postprocessor]") {
    AnswerPostProcessor proc;
    proc.process(DisplayEvent{.type = DisplayEventType::ToolResult, .toolName = "Write", .toolCallId = "1", .summary = ToolResultSummary::success("Wrote 10 lines")});
    proc.process(DisplayEvent{.type = DisplayEventType::ToolResult, .toolName = "Write", .toolCallId = "2", .summary = ToolResultSummary::success("Wrote 5 lines")});

    auto final_events = proc.finalize();
    int groups = 0;
    for (auto& e : final_events) {
        if (e.type == DisplayEventType::ToolGroup) groups++;
    }
    CHECK(groups == 0);
}

TEST_CASE("Finalize with empty events returns empty", "[postprocessor]") {
    AnswerPostProcessor proc;
    auto final_events = proc.finalize();
    CHECK(final_events.empty());
}

TEST_CASE("Reset clears accumulated events", "[postprocessor]") {
    AnswerPostProcessor proc;
    proc.process(DisplayEvent{.type = DisplayEventType::TextParagraph, .text = "Hello"});
    proc.reset();
    auto final_events = proc.finalize();
    CHECK(final_events.empty());
}

TEST_CASE("isCollapsibleToolName identifies MCP write tools as non-collapsible", "[postprocessor]") {
    CHECK_FALSE(AnswerPostProcessor::isCollapsibleToolName("write_file"));
    CHECK_FALSE(AnswerPostProcessor::isCollapsibleToolName("update_config"));
    CHECK_FALSE(AnswerPostProcessor::isCollapsibleToolName("delete_record"));
    CHECK_FALSE(AnswerPostProcessor::isCollapsibleToolName("create_issue"));
    CHECK(AnswerPostProcessor::isCollapsibleToolName("search_docs"));
    CHECK(AnswerPostProcessor::isCollapsibleToolName("query_database"));
}
