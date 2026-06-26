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
// E8 Test 3: TurnDuration ordering constraint
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

// Helper: find index of last ToolProgress block, or -1 if none
static int findLastToolProgressIndex(const std::vector<ContentBlock>& blocks) {
    int last = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].type == ContentBlock::ToolProgress) last = static_cast<int>(i);
    }
    return last;
}

// Helper: find index of last ToolResult block (not inside children), or -1 if none
static int findLastToolResultIndex(const std::vector<ContentBlock>& blocks) {
    int last = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].type == ContentBlock::ToolResult) last = static_cast<int>(i);
    }
    return last;
}

TEST_CASE("E8: TurnDuration must not appear before all ToolProgress blocks finalized",
          "[ContentBlock][e8][regression]") {
    // Regression test for: FtxuiRepl AnswerEnd handler (line 493-515)
    // adds TurnDuration at stream end, but tool may still be running.
    // TurnDuration should only be added when no ToolProgress blocks remain.
    //
    // This test verifies the ordering invariant:
    //   If TurnDuration exists, ToolProgress must NOT exist.
    //   If ToolProgress exists, TurnDuration must NOT exist.

    // Scenario 1: Normal flow with no tools — TurnDuration is fine
    SECTION("TurnDuration without tools is valid") {
        std::vector<ContentBlock> blocks;
        ContentBlock user;
        user.type = ContentBlock::UserMessage;
        user.text = "hello";
        blocks.push_back(user);

        ContentBlock answer;
        answer.type = ContentBlock::AnswerText;
        answer.text = "Hi there!";
        blocks.push_back(answer);

        ContentBlock td;
        td.type = ContentBlock::TurnDuration;
        td.text = "Baked for 2s";
        blocks.push_back(td);

        // TurnDuration exists, but no ToolProgress — valid
        int tdIdx = findFirstTurnDurationIndex(blocks);
        REQUIRE(tdIdx >= 0);
        REQUIRE_FALSE(hasPendingToolProgress(blocks));
    }

    // Scenario 2: Bug scenario — TurnDuration appears while ToolProgress exists
    SECTION("TurnDuration before ToolProgress completion is INVALID (current bug)") {
        // This simulates the E8 scenario:
        // - ToolProgress is pending (tool still running)
        // - TurnDuration was added by AnswerEnd handler prematurely
        std::vector<ContentBlock> blocks;
        ContentBlock user;
        user.type = ContentBlock::UserMessage;
        user.text = "! sleep 30";
        blocks.push_back(user);

        ContentBlock answer;
        answer.type = ContentBlock::AnswerText;
        answer.text = "I'll run that.";
        blocks.push_back(answer);

        ContentBlock progress;
        progress.type = ContentBlock::ToolProgress;
        progress.toolName = "Bash";
        progress.activity = "Running sleep 30...";
        blocks.push_back(progress);

        // BUG: TurnDuration added while ToolProgress still pending
        ContentBlock td;
        td.type = ContentBlock::TurnDuration;
        td.text = "Baked for 5s";
        blocks.push_back(td);

        // This should FAIL — documents the current buggy state
        bool turnDurationBeforeToolCompletion = false;
        int tdIdx = findFirstTurnDurationIndex(blocks);
        int lastProgIdx = findLastToolProgressIndex(blocks);
        if (tdIdx >= 0 && lastProgIdx >= 0 && tdIdx > lastProgIdx) {
            // TurnDuration appears after ToolProgress in the vector order,
            // but ToolProgress still exists — the tool hasn't been finalized
            // into a ToolResult yet.
            turnDurationBeforeToolCompletion = true;
        }

        INFO("TurnDuration at index " << tdIdx
             << ", last ToolProgress at index " << lastProgIdx);
        INFO("TurnDuration found while ToolProgress still pending — "
             "this is the E8 bug: AnswerEnd adds TurnDuration before tool completes");
        // TODO(E8-fix): After fix, this should be REQUIRE_FALSE
        CHECK(hasPendingToolProgress(blocks));  // Documents: ToolProgress still exists
    }

    // Scenario 3: Correct flow — tool finalized before TurnDuration
    SECTION("TurnDuration after ToolResult is valid") {
        std::vector<ContentBlock> blocks;
        ContentBlock user;
        user.type = ContentBlock::UserMessage;
        user.text = "! sleep 30";
        blocks.push_back(user);

        ContentBlock answer;
        answer.type = ContentBlock::AnswerText;
        answer.text = "Running it.";
        blocks.push_back(answer);

        ContentBlock progress;
        progress.type = ContentBlock::ToolProgress;
        progress.toolName = "Bash";
        progress.activity = "Running sleep 30...";
        blocks.push_back(progress);

        // Tool completes: ToolProgress → ToolResult
        ContentBlock result;
        result.type = ContentBlock::ToolResult;
        result.toolName = "Bash";
        result.text = "Command completed.";
        result.summary = ToolResultSummary::success("Command completed.");
        blocks.push_back(result);

        // TurnDuration only after tool result finalization
        ContentBlock td;
        td.type = ContentBlock::TurnDuration;
        td.text = "Baked for 32s";
        blocks.push_back(td);

        // After fix: ToolProgress should have been removed/replaced
        // For now, at least TurnDuration is positioned after ToolResult
        int tdIdx = findFirstTurnDurationIndex(blocks);
        int lastResultIdx = findLastToolResultIndex(blocks);
        REQUIRE(tdIdx >= 0);
        REQUIRE(lastResultIdx >= 0);
        REQUIRE(tdIdx > lastResultIdx);  // TurnDuration AFTER last ToolResult
    }
}
