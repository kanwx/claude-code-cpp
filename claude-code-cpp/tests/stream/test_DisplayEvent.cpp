#include <catch2/catch_test_macros.hpp>
#include <claude/stream/DisplayEvent.hpp>
#include <utility>

using namespace claude;

// ---------- DisplayEventType enum coverage ----------

TEST_CASE("All 12 DisplayEventType enum values exist", "[display_event]") {
    // Verify every enumerator is accessible and distinct
    auto t0 = DisplayEventType::TextParagraph;
    auto t1 = DisplayEventType::TextPartial;
    auto t2 = DisplayEventType::ThinkingBlock;
    auto t3 = DisplayEventType::ToolProgress;
    auto t4 = DisplayEventType::ToolResult;
    auto t5 = DisplayEventType::ToolGroup;
    auto t6 = DisplayEventType::AnswerStart;
    auto t7 = DisplayEventType::AnswerEnd;
    auto t8 = DisplayEventType::TurnMetadata;
    auto t9 = DisplayEventType::SystemNotice;
    auto t10 = DisplayEventType::Tombstone;
    auto t11 = DisplayEventType::Error;

    // All values must be distinct
    REQUIRE(t0 != t1);
    REQUIRE(t1 != t2);
    REQUIRE(t2 != t3);
    REQUIRE(t3 != t4);
    REQUIRE(t4 != t5);
    REQUIRE(t5 != t6);
    REQUIRE(t6 != t7);
    REQUIRE(t7 != t8);
    REQUIRE(t8 != t9);
    REQUIRE(t9 != t10);
    REQUIRE(t10 != t11);
}

// ---------- Default construction ----------

TEST_CASE("DisplayEvent default construction", "[display_event]") {
    DisplayEvent e;
    REQUIRE(e.type == DisplayEventType::TextParagraph);
    REQUIRE(e.text.empty());
    REQUIRE(e.thinkingText.empty());
    REQUIRE(e.toolCallId.empty());
    REQUIRE(e.toolName.empty());
    REQUIRE(e.activity.empty());
    REQUIRE(e.summary.empty());
    REQUIRE(e.rawResultPath.empty());
    REQUIRE(e.noticeText.empty());
}

TEST_CASE("TurnMetadata default values", "[display_event]") {
    TurnMetadata m;
    REQUIRE(m.modelName.empty());
    REQUIRE(m.contextUsed == 0);
    REQUIRE(m.contextTotal == 0);
    REQUIRE(m.inputTokens == 0);
    REQUIRE(m.outputTokens == 0);
    REQUIRE(m.durationStr.empty());
    REQUIRE(m.costStr.empty());
    REQUIRE(m.isStreaming == false);
}

// ---------- Designated-initializer construction ----------

TEST_CASE("DisplayEvent designated initializer construction", "[display_event]") {
    DisplayEvent e{
        .type = DisplayEventType::ToolResult,
        .toolCallId = "call_123",
        .toolName = "Bash",
        .summary = ToolResultSummary::success("Done", true, "in 0.5s"),
        .rawResultPath = "/tmp/result.txt"
    };
    REQUIRE(e.type == DisplayEventType::ToolResult);
    REQUIRE(e.toolCallId == "call_123");
    REQUIRE(e.toolName == "Bash");
    REQUIRE(e.summary.primaryText == "Done");
    REQUIRE(e.summary.primaryBold == true);
    REQUIRE(e.summary.secondaryText == "in 0.5s");
    REQUIRE(e.rawResultPath == "/tmp/result.txt");
}

TEST_CASE("TurnMetadata with values", "[display_event]") {
    TurnMetadata m{
        .modelName = "claude-sonnet-4-20250514",
        .contextUsed = 50000,
        .contextTotal = 200000,
        .inputTokens = 45000,
        .outputTokens = 1200,
        .durationStr = "2.3s",
        .costStr = "$0.14",
        .isStreaming = true
    };
    REQUIRE(m.modelName == "claude-sonnet-4-20250514");
    REQUIRE(m.contextUsed == 50000);
    REQUIRE(m.contextTotal == 200000);
    REQUIRE(m.inputTokens == 45000);
    REQUIRE(m.outputTokens == 1200);
    REQUIRE(m.durationStr == "2.3s");
    REQUIRE(m.costStr == "$0.14");
    REQUIRE(m.isStreaming == true);
}

// ---------- Convenience static factories ----------

TEST_CASE("DisplayEvent convenience factories", "[display_event]") {
    auto tp = DisplayEvent::textParagraph("Hello world");
    REQUIRE(tp.type == DisplayEventType::TextParagraph);
    REQUIRE(tp.text == "Hello world");

    auto partial = DisplayEvent::textPartial("Hel");
    REQUIRE(partial.type == DisplayEventType::TextPartial);
    REQUIRE(partial.text == "Hel");

    auto think = DisplayEvent::thinkingBlock("Let me reason...");
    REQUIRE(think.type == DisplayEventType::ThinkingBlock);
    REQUIRE(think.thinkingText == "Let me reason...");

    auto prog = DisplayEvent::toolProgress("c1", "Read", "Running...");
    REQUIRE(prog.type == DisplayEventType::ToolProgress);
    REQUIRE(prog.toolCallId == "c1");
    REQUIRE(prog.toolName == "Read");
    REQUIRE(prog.activity == "Running...");

    auto result = DisplayEvent::toolResult("c2", "Bash",
        ToolResultSummary::success("Done"), "/tmp/out.txt");
    REQUIRE(result.type == DisplayEventType::ToolResult);
    REQUIRE(result.toolCallId == "c2");
    REQUIRE(result.toolName == "Bash");
    REQUIRE(result.summary.primaryText == "Done");
    REQUIRE(result.rawResultPath == "/tmp/out.txt");

    auto grp = DisplayEvent::toolGroup("c3", "Write");
    REQUIRE(grp.type == DisplayEventType::ToolGroup);
    REQUIRE(grp.toolCallId == "c3");

    auto as = DisplayEvent::answerStart();
    REQUIRE(as.type == DisplayEventType::AnswerStart);

    auto ae = DisplayEvent::answerEnd();
    REQUIRE(ae.type == DisplayEventType::AnswerEnd);

    TurnMetadata meta{.modelName = "test-model", .inputTokens = 100};
    auto tm = DisplayEvent::turnMeta(std::move(meta));
    REQUIRE(tm.type == DisplayEventType::TurnMetadata);
    REQUIRE(tm.metadata.modelName == "test-model");
    REQUIRE(tm.metadata.inputTokens == 100);

    auto sn = DisplayEvent::systemNotice("Auto-compact triggered");
    REQUIRE(sn.type == DisplayEventType::SystemNotice);
    REQUIRE(sn.noticeText == "Auto-compact triggered");

    auto ts = DisplayEvent::tombstone("[compacted]");
    REQUIRE(ts.type == DisplayEventType::Tombstone);
    REQUIRE(ts.text == "[compacted]");

    auto err = DisplayEvent::error("API rate limit");
    REQUIRE(err.type == DisplayEventType::Error);
    REQUIRE(err.text == "API rate limit");
}

// ---------- Move semantics ----------

TEST_CASE("DisplayEvent move semantics", "[display_event]") {
    DisplayEvent original = DisplayEvent::toolProgress("call_abc", "Grep", "Running...");
    DisplayEvent moved = std::move(original);

    REQUIRE(moved.type == DisplayEventType::ToolProgress);
    REQUIRE(moved.toolCallId == "call_abc");
    REQUIRE(moved.toolName == "Grep");
    REQUIRE(moved.activity == "Running...");
    // original is in valid-but-unspecified state after move; just ensure no crash
}

TEST_CASE("TurnMetadata move semantics", "[display_event]") {
    TurnMetadata original{.modelName = "opus-4", .costStr = "$1.50", .isStreaming = true};
    TurnMetadata moved = std::move(original);

    REQUIRE(moved.modelName == "opus-4");
    REQUIRE(moved.costStr == "$1.50");
    REQUIRE(moved.isStreaming == true);
}

// ---------- ToolResultSummary integration ----------

TEST_CASE("DisplayEvent with ToolResultSummary error", "[display_event]") {
    auto e = DisplayEvent::toolResult("c_err", "Bash",
        ToolResultSummary::error("Command failed with exit code 1"));
    REQUIRE(e.type == DisplayEventType::ToolResult);
    REQUIRE(e.summary.isError == true);
    REQUIRE(e.summary.errorText == "Command failed with exit code 1");
}

TEST_CASE("DisplayEvent with ToolResultSummary dim", "[display_event]") {
    auto e = DisplayEvent::toolResult("c_dim", "Read",
        ToolResultSummary::dim("Read 10 lines"));
    REQUIRE(e.summary.isDim == true);
    REQUIRE(e.summary.primaryText == "Read 10 lines");
    REQUIRE(e.summary.isError == false);
}
