#include <catch2/catch_test_macros.hpp>
#include <claude/core/ApiTypes.hpp>

using namespace claude;

TEST_CASE("StreamToolEventType enum values", "[StreamToolEvent]") {
    CHECK(static_cast<int>(StreamToolEventType::Queued) == 0);
    CHECK(static_cast<int>(StreamToolEventType::Started) == 1);
    CHECK(static_cast<int>(StreamToolEventType::Progress) == 2);
    CHECK(static_cast<int>(StreamToolEventType::Completed) == 3);
    CHECK(static_cast<int>(StreamToolEventType::Error) == 4);
    CHECK(static_cast<int>(StreamToolEventType::Rejected) == 5);
    CHECK(static_cast<int>(StreamToolEventType::Cancelled) == 6);
}

TEST_CASE("StreamToolEvent default construction", "[StreamToolEvent]") {
    StreamToolEvent ev;
    CHECK(ev.type == StreamToolEventType::Queued);
    CHECK(ev.toolCallId.empty());
    CHECK(ev.toolName.empty());
    CHECK(ev.activity.empty());
    CHECK(ev.rawResultPath.empty());
    CHECK(ev.isParallel == false);
    CHECK(ev.durationMs == 0.0);
    CHECK(ev.summary.empty());
}

TEST_CASE("StreamToolEvent fields can be set", "[StreamToolEvent]") {
    StreamToolEvent ev;
    ev.type = StreamToolEventType::Started;
    ev.toolCallId = "call_123";
    ev.toolName = "Read";
    ev.activity = "Reading file.txt";
    ev.rawResultPath = "/tmp/result.json";
    ev.isParallel = true;
    ev.durationMs = 42.5;
    ev.summary = ToolResultSummary::success("Read 10 lines", true, "of file.txt");

    CHECK(ev.type == StreamToolEventType::Started);
    CHECK(ev.toolCallId == "call_123");
    CHECK(ev.toolName == "Read");
    CHECK(ev.activity == "Reading file.txt");
    CHECK(ev.rawResultPath == "/tmp/result.json");
    CHECK(ev.isParallel == true);
    CHECK(ev.durationMs == 42.5);
    CHECK(ev.summary.primaryText == "Read 10 lines");
    CHECK(ev.summary.primaryBold == true);
    CHECK(ev.summary.secondaryText == "of file.txt");
}

TEST_CASE("ToolUseRenderData has displaySummary field", "[StreamToolEvent]") {
    ToolUseRenderData data;
    CHECK(data.displaySummary.empty());

    data.displaySummary = ToolResultSummary::success("Wrote 3 files");
    CHECK_FALSE(data.displaySummary.empty());
    CHECK(data.displaySummary.primaryText == "Wrote 3 files");
}

TEST_CASE("StreamToolEvent does not conflict with ToolEvent", "[StreamToolEvent]") {
    // Both types should coexist without issue
    ToolEvent old_event;
    old_event.phase = ToolEventPhase::Start;
    old_event.toolName = "Bash";

    StreamToolEvent new_event;
    new_event.type = StreamToolEventType::Started;
    new_event.toolName = "Bash";

    CHECK(old_event.phase == ToolEventPhase::Start);
    CHECK(new_event.type == StreamToolEventType::Started);
    CHECK(old_event.toolName == new_event.toolName);
}
