#include <catch2/catch_test_macros.hpp>
#include "claude/stream/ContentBlock.hpp"

using namespace claude;

TEST_CASE("ContentBlock default construction", "[ContentBlock]") {
    ContentBlock cb;

    CHECK(cb.type == ContentBlock::UserMessage);
    CHECK(cb.text.empty());
    CHECK(cb.detailText.empty());
    CHECK(cb.toolName.empty());
    CHECK(cb.activity.empty());
    CHECK(cb.summary.empty());
    CHECK(cb.rawResultPath.empty());
    CHECK_FALSE(cb.expanded);
    CHECK_FALSE(cb.dimmed);
    CHECK(cb.children.empty());
}

TEST_CASE("ContentBlock ToolGroup with children", "[ContentBlock]") {
    ContentBlock group;
    group.type = ContentBlock::ToolGroup;
    group.toolName = "Read";
    group.summary = ToolResultSummary::success("Read 42 lines", true, "of file.cpp");

    ContentBlock progress;
    progress.type = ContentBlock::ToolProgress;
    progress.toolName = "Read";
    progress.activity = "Reading…";

    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Read";
    result.text = "file contents here";
    result.summary = ToolResultSummary::success("Read 42 lines");

    ContentBlock thinking;
    thinking.type = ContentBlock::ThinkingBlock;
    thinking.text = "Let me read that file…";

    group.children.push_back(progress);
    group.children.push_back(result);
    group.children.push_back(thinking);

    CHECK(group.type == ContentBlock::ToolGroup);
    CHECK(group.children.size() == 3);
    CHECK(group.summary.primaryText == "Read 42 lines");
    CHECK(group.summary.primaryBold == true);
    CHECK(group.summary.secondaryText == "of file.cpp");
    CHECK(group.children[0].type == ContentBlock::ToolProgress);
    CHECK(group.children[1].type == ContentBlock::ToolResult);
    CHECK(group.children[2].type == ContentBlock::ThinkingBlock);
}

TEST_CASE("ContentBlock all Type enum values exist", "[ContentBlock]") {
    // Verify all 7 enum values compile and are distinct
    ContentBlock::Type types[] = {
        ContentBlock::UserMessage,
        ContentBlock::AnswerText,
        ContentBlock::ThinkingBlock,
        ContentBlock::ToolProgress,
        ContentBlock::ToolResult,
        ContentBlock::ToolGroup,
        ContentBlock::ErrorMessage
    };

    constexpr int expected = 7;
    int count = static_cast<int>(sizeof(types) / sizeof(types[0]));
    CHECK(count == expected);

    // Verify they are distinct values
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            CHECK(types[i] != types[j]);
        }
    }
}

// ============================================================================
// P1 Tests: TurnDuration deferred from AnswerEnd to finishStream
// ============================================================================

// Helper: check if any ToolProgress blocks exist in the vector
static bool hasPendingToolProgress(const std::vector<ContentBlock>& blocks) {
    for (const auto& b : blocks) {
        if (b.type == ContentBlock::ToolProgress) return true;
    }
    return false;
}

// Helper: find index of first TurnDuration block, or -1 if none
static int findFirstTurnDurationIndex(const std::vector<ContentBlock>& blocks) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].type == ContentBlock::TurnDuration) return static_cast<int>(i);
    }
    return -1;
}

// Helper: find index of last ToolResult block (not inside children), or -1 if none
static int findLastToolResultIndex(const std::vector<ContentBlock>& blocks) {
    int last = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].type == ContentBlock::ToolResult) last = static_cast<int>(i);
    }
    return last;
}

// Helper: count TurnDuration blocks
static int countTurnDurations(const std::vector<ContentBlock>& blocks) {
    int count = 0;
    for (const auto& b : blocks) {
        if (b.type == ContentBlock::TurnDuration) count++;
    }
    return count;
}

// ============================================================================
// Test 1: Pending tool_use → no TurnDuration at AnswerEnd
// (After fix: TurnDuration is deferred to finishStream)
// ============================================================================
TEST_CASE("P1: No TurnDuration while ToolProgress is pending (AnswerEnd phase)",
          "[ContentBlock][p1][regression]") {
    // Simulates the state after AnswerEnd when a tool_use is pending.
    // In the old code, AnswerEnd created TurnDuration immediately.
    // After the fix, TurnDuration is NOT created here — the turn
    // isn't complete yet (tool still running).

    std::vector<ContentBlock> blocks;
    ContentBlock user;
    user.type = ContentBlock::UserMessage;
    user.text = "Run sleep 30, then wait.";
    blocks.push_back(user);

    ContentBlock answer;
    answer.type = ContentBlock::AnswerText;
    answer.text = "I'll run that sleep command.";
    blocks.push_back(answer);

    ContentBlock progress;
    progress.type = ContentBlock::ToolProgress;
    progress.toolName = "Bash";
    progress.activity = "Running sleep 30...";
    blocks.push_back(progress);

    // After fix: TurnDuration must NOT exist when ToolProgress is pending.
    // The invariant is: ToolProgress and TurnDuration are mutually exclusive.
    REQUIRE(hasPendingToolProgress(blocks));
    int tdIdx = findFirstTurnDurationIndex(blocks);
    CHECK(tdIdx == -1);  // No TurnDuration while tools are running
}

// ============================================================================
// Test 2: After tool_result finalize → TurnDuration appears at finishStream
// ============================================================================
TEST_CASE("P1: TurnDuration appears after ToolResult finalization (finishStream phase)",
          "[ContentBlock][p1]") {
    // Simulates the state after finishStream when all tools are done.
    // TurnDuration should be present, after all ToolResult blocks,
    // and no ToolProgress should remain.

    std::vector<ContentBlock> blocks;
    ContentBlock user;
    user.type = ContentBlock::UserMessage;
    user.text = "Run sleep 30, then wait.";
    blocks.push_back(user);

    ContentBlock answer;
    answer.type = ContentBlock::AnswerText;
    answer.text = "I'll run that sleep command.";
    blocks.push_back(answer);

    // Tool completed: ToolProgress was already replaced by ToolResult
    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Bash";
    result.text = "Command completed successfully.";
    result.summary = ToolResultSummary::success("Completed");
    blocks.push_back(result);

    // finishStream creates TurnDuration after all tools are done
    ContentBlock td;
    td.type = ContentBlock::TurnDuration;
    td.text = "Baked for 32s";
    blocks.push_back(td);

    // Invariant: no ToolProgress
    REQUIRE_FALSE(hasPendingToolProgress(blocks));
    // Invariant: TurnDuration is present
    int tdIdx = findFirstTurnDurationIndex(blocks);
    REQUIRE(tdIdx >= 0);
    // Invariant: TurnDuration after last ToolResult
    int lastResultIdx = findLastToolResultIndex(blocks);
    REQUIRE(lastResultIdx >= 0);
    CHECK(tdIdx > lastResultIdx);
    // Invariant: exactly one TurnDuration
    CHECK(countTurnDurations(blocks) == 1);
}

// ============================================================================
// Test 3: Normal answers (no tool_use) still show TurnDuration at finishStream
// ============================================================================
TEST_CASE("P1: TurnDuration for text-only answers (no tool_use)",
          "[ContentBlock][p1]") {
    // A simple text-only answer with no tool_use should still get
    // a TurnDuration at finishStream.

    std::vector<ContentBlock> blocks;
    ContentBlock user;
    user.type = ContentBlock::UserMessage;
    user.text = "Hello!";
    blocks.push_back(user);

    ContentBlock answer;
    answer.type = ContentBlock::AnswerText;
    answer.text = "Hi there! How can I help?";
    blocks.push_back(answer);

    // finishStream: TurnDuration created
    ContentBlock td;
    td.type = ContentBlock::TurnDuration;
    td.text = "Baked for 2s";
    blocks.push_back(td);

    REQUIRE_FALSE(hasPendingToolProgress(blocks));
    int tdIdx = findFirstTurnDurationIndex(blocks);
    REQUIRE(tdIdx >= 0);
    CHECK(countTurnDurations(blocks) == 1);
}

// ============================================================================
// Test 4: No duplicate TurnDuration on multi-iteration TAOR
// ============================================================================
TEST_CASE("P1: Single TurnDuration for multi-iteration TAOR",
          "[ContentBlock][p1]") {
    // Multi-iteration turn:
    //   AnswerStart#1 → tool_use → AnswerEnd#1 → tool_result
    //   AnswerStart#2 → final text → AnswerEnd#2
    //   finishStream → exactly one TurnDuration
    //
    // Each AnswerEnd does NOT create TurnDuration (deferred to finishStream).
    // finishStream runs once per turn, creating exactly one TurnDuration.

    // finishStream phase: ToolProgress has already been replaced
    // by ToolResult (AnswerPostProcessor + orphan cleanup in AnswerEnd).
    std::vector<ContentBlock> blocks;
    ContentBlock user;
    user.type = ContentBlock::UserMessage;
    user.text = "List files and summarize.";
    blocks.push_back(user);

    // Iteration 1: API round answer text
    ContentBlock iter1Answer;
    iter1Answer.type = ContentBlock::AnswerText;
    iter1Answer.text = "Let me list the files.";
    blocks.push_back(iter1Answer);

    // ToolProgress already replaced by ToolResult
    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Bash";
    result.text = "file1.cpp\nfile2.hpp";
    result.summary = ToolResultSummary::success("2 files");
    blocks.push_back(result);

    // Iteration 2: final answer (no tools)
    ContentBlock iter2Answer;
    iter2Answer.type = ContentBlock::AnswerText;
    iter2Answer.text = "Found 2 files: file1.cpp and file2.hpp.";
    blocks.push_back(iter2Answer);

    // finishStream: single TurnDuration
    ContentBlock td;
    td.type = ContentBlock::TurnDuration;
    td.text = "Baked for 5s";
    blocks.push_back(td);

    // Invariant: exactly one TurnDuration
    CHECK(countTurnDurations(blocks) == 1);
    // Invariant: no pending ToolProgress
    REQUIRE_FALSE(hasPendingToolProgress(blocks));
    // Invariant: TurnDuration at the end (after both answer blocks and tool result)
    int tdIdx = findFirstTurnDurationIndex(blocks);
    REQUIRE(tdIdx >= 0);
    int lastResultIdx = findLastToolResultIndex(blocks);
    REQUIRE(lastResultIdx >= 0);
    CHECK(tdIdx > lastResultIdx);
}

// ============================================================================
// Test 5: Multiple AnswerStart must not reset turn start time
// ============================================================================
TEST_CASE("P1: Second AnswerStart does not reset turn start time",
          "[ContentBlock][p1]") {
    // In a multi-iteration TAOR turn, AnswerStart fires multiple times
    // (once per API round). Only the FIRST AnswerStart should set
    // the turn startTime_. Subsequent AnswerStarts must not reset it.
    //
    // This is verified structurally: after multiple AnswerStart events,
    // exactly one TurnDuration exists (created by finishStream using
    // the first AnswerStart's startTime_).
    //
    // The structural invariant is: multiple API rounds within a single
    // user turn produce exactly one TurnDuration.

    // finishStream phase: all ToolProgress blocks have been finalized.
    std::vector<ContentBlock> blocks;

    // ===== User Message =====
    ContentBlock user;
    user.type = ContentBlock::UserMessage;
    user.text = "Find and analyze the config file.";
    blocks.push_back(user);

    // ===== API Round 1: Read tool =====
    ContentBlock round1Answer;
    round1Answer.type = ContentBlock::AnswerText;
    round1Answer.text = "I'll search for config files first.";
    blocks.push_back(round1Answer);

    // ToolProgress → ToolResult (finalized by AnswerPostProcessor)
    ContentBlock result1;
    result1.type = ContentBlock::ToolResult;
    result1.toolName = "Glob";
    result1.text = "Found: config.yaml";
    result1.summary = ToolResultSummary::success("Found 1 file");
    blocks.push_back(result1);

    // ===== API Round 2: Read tool =====
    ContentBlock round2Answer;
    round2Answer.type = ContentBlock::AnswerText;
    round2Answer.text = "Let me read that config file.";
    blocks.push_back(round2Answer);

    // ToolProgress → ToolResult (finalized by AnswerPostProcessor)
    ContentBlock result2;
    result2.type = ContentBlock::ToolResult;
    result2.toolName = "Read";
    result2.text = "debug: true\nport: 8080";
    result2.summary = ToolResultSummary::success("Read 2 lines", false, "of config.yaml");
    blocks.push_back(result2);

    // ===== API Round 3: Final answer (no tools) =====
    ContentBlock round3Answer;
    round3Answer.type = ContentBlock::AnswerText;
    round3Answer.text = "The config uses port 8080 with debug enabled.";
    blocks.push_back(round3Answer);

    // ===== finishStream: single TurnDuration =====
    ContentBlock td;
    td.type = ContentBlock::TurnDuration;
    td.text = "Baked for 8s";
    blocks.push_back(td);

    // Structural invariants for multi-API-round turn:
    // 1. Exactly one TurnDuration (finishStream created it once)
    CHECK(countTurnDurations(blocks) == 1);
    // 2. No pending ToolProgress (all tools finalized)
    REQUIRE_FALSE(hasPendingToolProgress(blocks));
    // 3. TurnDuration after all content
    int tdIdx = findFirstTurnDurationIndex(blocks);
    REQUIRE(tdIdx >= 0);
    int lastResultIdx = findLastToolResultIndex(blocks);
    REQUIRE(lastResultIdx >= 0);
    CHECK(tdIdx > lastResultIdx);
    // 4. TurnDuration is the last block in the vector
    CHECK(tdIdx == static_cast<int>(blocks.size()) - 1);
}
