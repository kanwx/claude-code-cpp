#include <catch2/catch_test_macros.hpp>
#include "claude/metrics/HeadlessContentBlockAccumulator.hpp"
#include "claude/stream/DisplayEvent.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace claude {
namespace fs = std::filesystem;

// Helper to read JSONL lines from a file
static std::vector<std::string> readJsonl(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// Helper: count blocks recursively
static int countByType(const std::vector<ContentBlock>& blocks, ContentBlock::Type t) {
    int count = 0;
    for (const auto& b : blocks) {
        if (b.type == t) count++;
        count += countByType(b.children, t);
    }
    return count;
}

TEST_CASE("HeadlessContentBlockAccumulator basic event handling", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    SECTION("AnswerStart/End creates turn boundary with AnswerText") {
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::textPartial("Hello world"));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() >= 1);
        auto it = std::find_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::AnswerText; });
        REQUIRE(it != blocks.end());
        CHECK(it->text == "Hello world");
    }

    SECTION("ToolProgress replaced by ToolResult (then collapsed into CollapsedGroup)") {
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(
            DisplayEvent::toolProgress("call_1", "Bash", "Running command..."));
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_1", "Bash",
                ToolResultSummary::success("Done")));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        // No top-level ToolProgress
        auto toolProgresses = countByType(blocks, ContentBlock::ToolProgress);
        CHECK(toolProgresses == 0);
        // ToolResult moved into CollapsedGroup children by pipeline
        auto toolResultsTop = std::count_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::ToolResult; });
        CHECK(toolResultsTop == 0);
        // Should have a CollapsedGroup with child ToolResult
        auto collapsedGroups = std::count_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::CollapsedGroup; });
        CHECK(collapsedGroups == 1);
        // ToolResult inside the CollapsedGroup
        int toolResultsTotal = countByType(blocks, ContentBlock::ToolResult);
        CHECK(toolResultsTotal == 1);
    }

    SECTION("Error event creates ErrorMessage block") {
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::error("Connection refused"));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        auto errors = std::count_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::ErrorMessage; });
        CHECK(errors == 1);
    }
}

TEST_CASE("HeadlessContentBlockAccumulator streaming text accumulation", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    SECTION("Streaming text is committed before tool result") {
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::textPartial("Let me "));
        acc.handleDisplayEvent(DisplayEvent::textPartial("check "));
        acc.handleDisplayEvent(DisplayEvent::textPartial("something."));
        acc.handleDisplayEvent(
            DisplayEvent::toolProgress("call_1", "Bash", "Running..."));
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_1", "Bash",
                ToolResultSummary::success("Done")));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        auto firstAnswerText = std::find_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::AnswerText; });
        REQUIRE(firstAnswerText != blocks.end());
        CHECK(firstAnswerText->text == "Let me check something.");
        CHECK(firstAnswerText->isFirst == true);
    }

    SECTION("Remaining text committed at AnswerEnd") {
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::textPartial("Final output"));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        auto textBlocks = std::count_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::AnswerText; });
        CHECK(textBlocks == 1);
    }
}

TEST_CASE("HeadlessContentBlockAccumulator metrics collection", "[HeadlessContentBlockAccumulator]") {
    auto tmpDir = fs::temp_directory_path() / "headless-acc-test";
    fs::create_directories(tmpDir);
    auto metricsPath = (tmpDir / "metrics.jsonl").string();
    std::remove(metricsPath.c_str());

    {
        HeadlessContentBlockAccumulator acc;
        acc.enableMetricsCollection(metricsPath);

        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::textPartial("Let me search for the definition."));
        acc.handleDisplayEvent(
            DisplayEvent::toolProgress("call_1", "Bash", "Running grep..."));
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_1", "Bash",
                ToolResultSummary::success("Found 3 matches")));
        acc.handleDisplayEvent(DisplayEvent::textPartial("I found the definition."));
        acc.handleDisplayEvent(DisplayEvent::thinkingBlock("Need to verify the type hierarchy"));

        TurnMetadata meta;
        meta.durationStr = "2.3s";
        meta.outputTokens = 1500;
        meta.costStr = "$0.02";
        acc.handleDisplayEvent(DisplayEvent::turnMeta(meta));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());
    }

    auto lines = readJsonl(metricsPath);
    REQUIRE(lines.size() == 1);

    auto j = nlohmann::json::parse(lines[0]);

    // Basic structure
    REQUIRE(j.contains("ts"));
    REQUIRE(j.contains("model"));
    CHECK(j["snapshot_index"] == 0);
    CHECK(j["user_turn_index"] == 0);
    CHECK(j["model"] == "headless");

    // M1: meta patterns
    auto& m1 = j["meta_patterns"];
    CHECK(m1["let_me"] == 1);

    // M2: inter-tool word counts
    // "Let me search for the definition." = 6 words → before ToolResult → inter-tool
    auto& m2 = j["inter_tool_words"];
    REQUIRE(m2.contains("values"));
    CHECK(m2["values"].size() == 1);
    CHECK(m2["values"][0] == 6);

    // M3: block counts — ToolResult is inside CollapsedGroup
    auto& m3 = j["block_counts"];
    CHECK(m3["answer_text"] == 2);
    // ToolResult moved to CollapsedGroup children by pipeline
    CHECK(m3["tool_result"] == 0);
    CHECK(m3["collapsed_group"] == 1);

    // M5: not silent
    CHECK(j["is_silent_round"] == false);

    // M4: total words = 6 + 4 = 10
    CHECK(j["total_user_words"] == 10);

    // Text samples
    REQUIRE(j.contains("text_samples"));
    CHECK(j["text_samples"].size() == 2);
    REQUIRE(j.contains("combined_text"));

    fs::remove_all(tmpDir);
}

TEST_CASE("HeadlessContentBlockAccumulator orphaned ToolProgress cleanup", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    acc.handleDisplayEvent(DisplayEvent::answerStart());
    acc.handleDisplayEvent(DisplayEvent::textPartial("Testing..."));
    acc.handleDisplayEvent(
        DisplayEvent::toolProgress("call_orphan", "Bash", "Running..."));
    acc.handleDisplayEvent(DisplayEvent::answerEnd());

    const auto& blocks = acc.contentBlocks();
    // Orphaned ToolProgress → Cancelled ToolResult → collapsed by pipeline
    int cancelled = countByType(blocks, ContentBlock::ToolResult);
    CHECK(cancelled == 1);

    // Verify it's marked Cancelled
    for (const auto& b : blocks) {
        if (b.type == ContentBlock::CollapsedGroup) {
            for (const auto& child : b.children) {
                if (child.type == ContentBlock::ToolResult) {
                    CHECK(child.resultStatus == ToolResultStatus::Cancelled);
                }
            }
        }
    }
}

TEST_CASE("HeadlessContentBlockAccumulator FTXUI consistency", "[HeadlessContentBlockAccumulator]") {
    auto tmpDir = fs::temp_directory_path() / "headless-consistency";
    fs::create_directories(tmpDir);
    auto metricsPath = (tmpDir / "consistency.jsonl").string();
    std::remove(metricsPath.c_str());

    // Collect metrics after scope ends (flushMetrics in destructor defers
    // collection to capture late-arriving ToolResult events)
    {
        HeadlessContentBlockAccumulator acc;
        acc.enableMetricsCollection(metricsPath);

        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::textPartial("Let me check the codebase structure."));

        // Tool call 1: Bash
        acc.handleDisplayEvent(
            DisplayEvent::toolProgress("call_a", "Bash", "ls -la"));
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_a", "Bash",
                ToolResultSummary::success("README.md\nsrc/\ntests/")));

        // Inter-tool narration
        acc.handleDisplayEvent(DisplayEvent::textPartial("Now let me read the main file."));

        // Tool call 2: Read
        acc.handleDisplayEvent(
            DisplayEvent::toolProgress("call_b", "Read", "src/main.cpp"));
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_b", "Read",
                ToolResultSummary::success("#include <iostream>...")));

        // Post-tool narration (no tool after → NOT inter-tool)
        acc.handleDisplayEvent(DisplayEvent::textPartial("The codebase has 3 directories."));

        acc.handleDisplayEvent(DisplayEvent::thinkingBlock("The project structure is clean"));

        TurnMetadata meta;
        meta.durationStr = "3.5s";
        meta.outputTokens = 2500;
        acc.handleDisplayEvent(DisplayEvent::turnMeta(meta));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        // Verify ContentBlock tree (before destructor flushMetrics)
        const auto& blocks = acc.contentBlocks();

        // AnswerText blocks
        int answerTextCount = std::count_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::AnswerText; });
        CHECK(answerTextCount >= 2);

        // Tool results are inside CollapsedGroups or ToolGroups after pipeline
        int toolResultsTotal = countByType(blocks, ContentBlock::ToolResult);
        CHECK(toolResultsTotal == 2);
    }

    // Verify metrics (written by destructor → flushMetrics)
    auto lines = readJsonl(metricsPath);
    REQUIRE(lines.size() == 1);
    auto j = nlohmann::json::parse(lines[0]);

    // M2: inter-tool words
    // P6-P0a: "Let me check the codebase structure." (6w) is tool narration that
    // passes through without breaking the group. It is no longer classified as
    // inter-tool since it appears before the first CollapsedGroup.
    // "Now let me read the main file." = 7 words → between CollapsedGroups → inter-tool.
    // "The codebase has 3 directories." = 5 words → next is ThinkingBlock → NOT inter-tool.
    auto& m2 = j["inter_tool_words"];
    REQUIRE(m2.contains("values"));
    CHECK(m2["values"].size() == 1);
    CHECK(m2["values"][0] == 7);

    // M4: total words = 6 + 7 + 5 = 18
    CHECK(j["total_user_words"] == 18);

    // M6: 2 tool calls
    CHECK(j["tool_call_count"] == 2);

    fs::remove_all(tmpDir);
}

TEST_CASE("HeadlessContentBlockAccumulator no-env-var zero overhead", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    acc.handleDisplayEvent(DisplayEvent::answerStart());
    acc.handleDisplayEvent(DisplayEvent::textPartial("test"));
    acc.handleDisplayEvent(DisplayEvent::answerEnd());

    const auto& blocks = acc.contentBlocks();
    CHECK(blocks.size() > 0);
}

TEST_CASE("HeadlessContentBlockAccumulator TurnDuration insertion", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    acc.handleDisplayEvent(DisplayEvent::answerStart());
    acc.handleDisplayEvent(DisplayEvent::textPartial("Done."));

    TurnMetadata meta;
    meta.durationStr = "5.1s";
    meta.outputTokens = 1000;
    acc.handleDisplayEvent(DisplayEvent::turnMeta(meta));
    acc.handleDisplayEvent(DisplayEvent::answerEnd());

    const auto& blocks = acc.contentBlocks();
    auto it = std::find_if(blocks.begin(), blocks.end(),
        [](const auto& b) { return b.type == ContentBlock::TurnDuration; });
    REQUIRE(it != blocks.end());
    CHECK(it->text.find("5.1s") != String::npos);
    // Token formatting: 1000 → "10.0K" (fmtK divides by 100)
    CHECK(it->text.find("10.0K token") != String::npos);
}

TEST_CASE("HeadlessContentBlockAccumulator error/interrupted/rejected tool status", "[HeadlessContentBlockAccumulator]") {
    SECTION("Error tool result") {
        HeadlessContentBlockAccumulator acc;
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_err", "Bash",
                ToolResultSummary::error("Permission denied")));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        // Error tools are NOT collapsible → remain as top-level ToolResult
        auto it = std::find_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::ToolResult; });
        REQUIRE(it != blocks.end());
        CHECK(it->resultStatus == ToolResultStatus::Error);
    }

    SECTION("Interrupted (Cancelled) tool result") {
        HeadlessContentBlockAccumulator acc;
        auto intSummary = ToolResultSummary::dim("Interrupted");
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_int", "Bash", intSummary));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        int cancelled = countByType(blocks, ContentBlock::ToolResult);
        CHECK(cancelled == 1);
        // Cancelled tools are dim, not collapsible → remain as ToolResult
        auto it = std::find_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::ToolResult; });
        REQUIRE(it != blocks.end());
        CHECK(it->resultStatus == ToolResultStatus::Cancelled);
    }

    SECTION("Rejected tool result") {
        HeadlessContentBlockAccumulator acc;
        auto rejSummary = ToolResultSummary::dim("Rejected by user");
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(
            DisplayEvent::toolResult("call_rej", "Bash", rejSummary));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        auto it = std::find_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.type == ContentBlock::ToolResult; });
        REQUIRE(it != blocks.end());
        CHECK(it->resultStatus == ToolResultStatus::Rejected);
    }
}

TEST_CASE("HeadlessContentBlockAccumulator multi-turn tracking", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    acc.handleDisplayEvent(DisplayEvent::answerStart());
    acc.handleDisplayEvent(DisplayEvent::textPartial("First turn."));
    acc.handleDisplayEvent(DisplayEvent::answerEnd());

    acc.handleDisplayEvent(DisplayEvent::answerStart());
    acc.handleDisplayEvent(DisplayEvent::textPartial("Second turn."));
    acc.handleDisplayEvent(DisplayEvent::answerEnd());

    const auto& blocks = acc.contentBlocks();
    auto answerTexts = std::count_if(blocks.begin(), blocks.end(),
        [](const auto& b) { return b.type == ContentBlock::AnswerText; });
    CHECK(answerTexts >= 2);
}

TEST_CASE("HeadlessContentBlockAccumulator ThinkingBlock insertion", "[HeadlessContentBlockAccumulator]") {
    HeadlessContentBlockAccumulator acc;

    acc.handleDisplayEvent(DisplayEvent::answerStart());
    acc.handleDisplayEvent(DisplayEvent::textPartial("Result analysis."));
    acc.handleDisplayEvent(DisplayEvent::thinkingBlock(
        "Step 1: identify root cause\nStep 2: verify fix"));
    acc.handleDisplayEvent(DisplayEvent::answerEnd());

    const auto& blocks = acc.contentBlocks();
    auto it = std::find_if(blocks.begin(), blocks.end(),
        [](const auto& b) { return b.type == ContentBlock::ThinkingBlock; });
    REQUIRE(it != blocks.end());
    CHECK(it->detailText.find("Step 1") != String::npos);
    CHECK(it->detailText.find("Step 2") != String::npos);
}

// ========== P6-P0c: in-place ToolProgress → ToolResult replacement ==========

TEST_CASE("P6-P0c: ToolProgress in-place replacement position stability", "[HeadlessContentBlockAccumulator][p0c]") {
    HeadlessContentBlockAccumulator acc;

    SECTION("Single tool: ToolResult stays at same index as ToolProgress") {
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Read", "Reading f1..."));
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_1", "Read",
            ToolResultSummary::success("f1 done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].type == ContentBlock::ToolResult);
        CHECK(blocks[0].toolCallId == "call_1");
        CHECK(blocks[0].toolName == "Read");
        auto toolProgresses = countByType(blocks, ContentBlock::ToolProgress);
        CHECK(toolProgresses == 0);
    }

    SECTION("Parallel tools: each result replaces its spinner, others stay at stable positions") {
        // Start 3 parallel tools
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Read", "Reading f1..."));
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_2", "Read", "Reading f2..."));
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_3", "Read", "Reading f3..."));

        const auto& blocks3 = acc.contentBlocks();
        REQUIRE(blocks3.size() == 3);
        REQUIRE(blocks3[0].type == ContentBlock::ToolProgress);
        REQUIRE(blocks3[1].type == ContentBlock::ToolProgress);
        REQUIRE(blocks3[2].type == ContentBlock::ToolProgress);
        CHECK(blocks3[0].toolCallId == "call_1");
        CHECK(blocks3[1].toolCallId == "call_2");
        CHECK(blocks3[2].toolCallId == "call_3");

        // Tool 1 completes — replaces spinner at index 0 only
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_1", "Read",
            ToolResultSummary::success("f1 done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 3);
        // Index 0: ToolProgress call_1 → ToolResult
        CHECK(blocks[0].type == ContentBlock::ToolResult);
        CHECK(blocks[0].toolCallId == "call_1");
        // Index 1: ToolProgress call_2 unchanged
        CHECK(blocks[1].type == ContentBlock::ToolProgress);
        CHECK(blocks[1].toolCallId == "call_2");
        // Index 2: ToolProgress call_3 unchanged
        CHECK(blocks[2].type == ContentBlock::ToolProgress);
        CHECK(blocks[2].toolCallId == "call_3");

        // Tool 2 completes — replaces spinner at index 1 only
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_2", "Read",
            ToolResultSummary::success("f2 done")));

        const auto& blocks2 = acc.contentBlocks();
        REQUIRE(blocks2.size() == 3);
        CHECK(blocks2[0].type == ContentBlock::ToolResult);      // call_1, unchanged
        CHECK(blocks2[1].type == ContentBlock::ToolResult);      // call_2, replaced
        CHECK(blocks2[1].toolCallId == "call_2");
        CHECK(blocks2[2].type == ContentBlock::ToolProgress);    // call_3, unchanged
        CHECK(blocks2[2].toolCallId == "call_3");
    }

    SECTION("Out-of-order completion: tool2 finishes before tool1, only tool2 position affected") {
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Read", "Reading f1..."));
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_2", "Read", "Reading f2..."));
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_3", "Read", "Reading f3..."));

        // Tool 2 completes out of order
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_2", "Read",
            ToolResultSummary::success("f2 done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 3);
        // Index 0: call_1 spinner — unchanged
        CHECK(blocks[0].type == ContentBlock::ToolProgress);
        CHECK(blocks[0].toolCallId == "call_1");
        // Index 1: call_2 spinner → result (out-of-order: 2nd completes before 1st)
        CHECK(blocks[1].type == ContentBlock::ToolResult);
        CHECK(blocks[1].toolCallId == "call_2");
        // Index 2: call_3 spinner — unchanged
        CHECK(blocks[2].type == ContentBlock::ToolProgress);
        CHECK(blocks[2].toolCallId == "call_3");
    }

    SECTION("Missing progress fallback: ToolResult appended when no matching ToolProgress") {
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_unknown", "Bash",
            ToolResultSummary::success("Done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].type == ContentBlock::ToolResult);
        CHECK(blocks[0].toolCallId == "call_unknown");
    }

    SECTION("Mismatched callId guard: ToolResult does not overwrite wrong ToolProgress") {
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Read", "Reading..."));
        // ToolResult with different callId — must not overwrite call_1 spinner
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_2", "Bash",
            ToolResultSummary::success("done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 2);
        // Index 0: call_1 spinner still there, not overwritten
        CHECK(blocks[0].type == ContentBlock::ToolProgress);
        CHECK(blocks[0].toolCallId == "call_1");
        // Index 1: call_2 result appended (fallback)
        CHECK(blocks[1].type == ContentBlock::ToolResult);
        CHECK(blocks[1].toolCallId == "call_2");
    }

    SECTION("stableId preserved after in-place replacement") {
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Read", "Reading..."));
        const auto& before = acc.contentBlocks();
        auto oldStableId = before[0].stableId;

        acc.handleDisplayEvent(DisplayEvent::toolResult("call_1", "Read",
            ToolResultSummary::success("Done")));

        const auto& after = acc.contentBlocks();
        REQUIRE(after.size() == 1);
        CHECK(after[0].type == ContentBlock::ToolResult);
        CHECK(after[0].stableId == oldStableId);
    }

    SECTION("Activity field cleared after in-place replacement") {
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Bash", "Running command..."));
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_1", "Bash",
            ToolResultSummary::success("Done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 1);
        REQUIRE(blocks[0].type == ContentBlock::ToolResult);
        CHECK(blocks[0].activity.empty());
    }

    SECTION("Orphan cleanup converts unfinished ToolProgress to Interrupted at AnswerEnd") {
        acc.handleDisplayEvent(DisplayEvent::answerStart());
        acc.handleDisplayEvent(DisplayEvent::toolProgress("call_1", "Read", "Reading..."));
        acc.handleDisplayEvent(DisplayEvent::answerEnd());

        const auto& blocks = acc.contentBlocks();
        // ToolProgress should be converted in-place to ToolResult with Interrupted status
        auto it = std::find_if(blocks.begin(), blocks.end(),
            [](const auto& b) { return b.toolCallId == "call_1"; });
        REQUIRE(it != blocks.end());
        CHECK(it->type == ContentBlock::ToolResult);
        CHECK(it->resultStatus == ToolResultStatus::Cancelled);
        CHECK(it->summary.primaryText.find("Interrupted") != String::npos);
    }

    SECTION("Fallback stableId assigned when no matching ToolProgress") {
        acc.handleDisplayEvent(DisplayEvent::toolResult("call_new", "Bash",
            ToolResultSummary::success("Done")));

        const auto& blocks = acc.contentBlocks();
        REQUIRE(blocks.size() == 1);
        // Fallback path assigns a new stableId (non-zero)
        CHECK(blocks[0].stableId > 0);
    }
}

} // namespace claude
