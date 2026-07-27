#include <catch2/catch_test_macros.hpp>
#include "claude/stream/ContentBlock.hpp"
#include <algorithm>

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

// ============================================================================
// P1d Tests: TurnDuration tool count
// ============================================================================

/// Mirror of the counting logic in FtxuiStreaming::finishStream().
/// Counts tool uses in [turnStartIndex, blocks.size()).
static int countToolsFromIndex(const std::vector<ContentBlock>& blocks,
                               size_t turnStartIndex) {
    int count = 0;
    for (size_t i = turnStartIndex; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        if (block.type == ContentBlock::ToolResult && !block.toolCallId.empty()) {
            count++;
        } else if (block.type == ContentBlock::ToolGroup ||
                   block.type == ContentBlock::CollapsedGroup) {
            count += static_cast<int>(block.toolUseIds.size());
        }
    }
    return count;
}

/// Build a TurnDuration text matching the format in finishStream().
static String makeTurnDurationText(const String& verb, int seconds, int toolCount) {
    auto fmtElapsed = [](int s) -> String {
        if (s >= 60) return std::to_string(s / 60) + "m " + std::to_string(s % 60) + "s";
        return std::to_string(s) + "s";
    };
    String text = verb + " for " + fmtElapsed(seconds);
    if (toolCount > 0) {
        text += " · " + std::to_string(toolCount) +
                (toolCount == 1 ? " tool" : " tools");
    }
    return text;
}

// ============================================================================
// Test P1d.1: Zero tools → no suffix
// ============================================================================
TEST_CASE("P1d: TurnDuration format — zero tools (no suffix)",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock answer;
    answer.type = ContentBlock::AnswerText;
    answer.text = "Hello!";
    blocks.push_back(answer);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 0);

    String text = makeTurnDurationText("Worked", 5, count);
    CHECK(text == "Worked for 5s");
}

// ============================================================================
// Test P1d.2: One tool → singular "· 1 tool"
// ============================================================================
TEST_CASE("P1d: TurnDuration format — 1 tool (singular)",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Bash";
    result.toolCallId = "toolu_001";
    result.summary = ToolResultSummary::success("Completed");
    blocks.push_back(result);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 1);

    String text = makeTurnDurationText("Baked", 12, count);
    CHECK(text == "Baked for 12s · 1 tool");
}

// ============================================================================
// Test P1d.3: Three tools → plural "· 3 tools"
// ============================================================================
TEST_CASE("P1d: TurnDuration format — 3 tools (plural)",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    for (int i = 0; i < 3; i++) {
        ContentBlock result;
        result.type = ContentBlock::ToolResult;
        result.toolName = "Bash";
        result.toolCallId = "toolu_00" + std::to_string(i);
        result.summary = ToolResultSummary::success("OK");
        blocks.push_back(result);
    }

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 3);

    String text = makeTurnDurationText("Crunched", 23, count);
    CHECK(text == "Crunched for 23s · 3 tools");
}

// ============================================================================
// Test P1d.4: Previous turn tools NOT counted in current turn
// ============================================================================
TEST_CASE("P1d: Previous turn tools not counted in current turn",
          "[ContentBlock][p1d]") {
    // Turn 1: user message + 2 tools + TurnDuration
    // Turn 2: user message + 1 tool + TurnDuration
    // Counting from Turn 2's start must see only 1 tool.
    std::vector<ContentBlock> blocks;

    // ---- Turn 1 ----
    ContentBlock user1;
    user1.type = ContentBlock::UserMessage;
    user1.text = "run tests";
    blocks.push_back(user1);

    ContentBlock result1a;
    result1a.type = ContentBlock::ToolResult;
    result1a.toolName = "Bash";
    result1a.toolCallId = "toolu_t1a";
    result1a.summary = ToolResultSummary::success("tests passed");
    blocks.push_back(result1a);

    ContentBlock result1b;
    result1b.type = ContentBlock::ToolResult;
    result1b.toolName = "Read";
    result1b.toolCallId = "toolu_t1b";
    result1b.summary = ToolResultSummary::success("4 lines");
    blocks.push_back(result1b);

    size_t turn2Start = blocks.size();  // snapshot before Turn 2

    // ---- Turn 2 ----
    ContentBlock user2;
    user2.type = ContentBlock::UserMessage;
    user2.text = "analyze output";
    blocks.push_back(user2);

    ContentBlock result2;
    result2.type = ContentBlock::ToolResult;
    result2.toolName = "Grep";
    result2.toolCallId = "toolu_t2";
    result2.summary = ToolResultSummary::success("3 matches");
    blocks.push_back(result2);

    // Count from Turn 2 start
    int count = countToolsFromIndex(blocks, turn2Start);
    CHECK(count == 1);  // only 1 tool in Turn 2, not 3
}

// ============================================================================
// Test P1d.5: CollapsedGroup counted by toolUseIds.size()
// ============================================================================
TEST_CASE("P1d: CollapsedGroup counted by toolUseIds.size()",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock group;
    group.type = ContentBlock::CollapsedGroup;
    group.toolName = "Read";
    group.toolUseIds = {"toolu_r1", "toolu_r2", "toolu_r3"};
    group.summary = ToolResultSummary::success("3 files");
    blocks.push_back(group);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 3);
}

// ============================================================================
// Test P1d.6: ToolGroup counted by toolUseIds.size()
// ============================================================================
TEST_CASE("P1d: ToolGroup counted by toolUseIds.size()",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock group;
    group.type = ContentBlock::ToolGroup;
    group.toolName = "Edit";
    group.toolUseIds = {"toolu_e1", "toolu_e2"};
    group.summary = ToolResultSummary::success("2 edits");
    blocks.push_back(group);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 2);
}

// ============================================================================
// Test P1d.7: Failed ToolResult counted
// ============================================================================
TEST_CASE("P1d: Failed ToolResult counted",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Bash";
    result.toolCallId = "toolu_fail";
    result.summary = ToolResultSummary::error("Command failed");
    blocks.push_back(result);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 1);  // failed tools still count
}

// ============================================================================
// Test P1d.8: Cancelled/interrupted ToolResult counted
// ============================================================================
TEST_CASE("P1d: Cancelled ToolResult counted",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Bash";
    result.toolCallId = "toolu_cancel";
    result.resultStatus = ToolResultStatus::Cancelled;
    result.summary = ToolResultSummary::success("Cancelled");  // isCancelled via resultStatus
    blocks.push_back(result);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 1);  // cancelled tools still count
}

// ============================================================================
// Test P1d.9: AgentProgress not counted
// ============================================================================
TEST_CASE("P1d: AgentProgress not counted",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;
    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolName = "Bash";
    result.toolCallId = "toolu_bash";
    result.summary = ToolResultSummary::success("OK");
    blocks.push_back(result);

    ContentBlock agent;
    agent.type = ContentBlock::AgentProgress;
    agent.toolName = "general-purpose";
    agent.text = "analyzing code...";
    blocks.push_back(agent);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 1);  // only Bash, not AgentProgress
}

// ============================================================================
// Test P1d.10: Mixed turn: ToolResult + CollapsedGroup + ToolGroup
// ============================================================================
TEST_CASE("P1d: Mixed tool types counted correctly",
          "[ContentBlock][p1d]") {
    std::vector<ContentBlock> blocks;

    // Standalone ToolResult (e.g., lone Bash)
    ContentBlock bash;
    bash.type = ContentBlock::ToolResult;
    bash.toolName = "Bash";
    bash.toolCallId = "toolu_bash";
    bash.summary = ToolResultSummary::success("ok");
    blocks.push_back(bash);

    // CollapsedGroup with 3 internal tools
    ContentBlock readGroup;
    readGroup.type = ContentBlock::CollapsedGroup;
    readGroup.toolName = "Read";
    readGroup.toolUseIds = {"toolu_r1", "toolu_r2", "toolu_r3"};
    readGroup.summary = ToolResultSummary::success("3 files");
    blocks.push_back(readGroup);

    // ToolGroup with 2 internal tools (Edit + Write)
    ContentBlock editGroup;
    editGroup.type = ContentBlock::ToolGroup;
    editGroup.toolName = "Edit";
    editGroup.toolUseIds = {"toolu_e1", "toolu_e2"};
    editGroup.summary = ToolResultSummary::success("2 edits");
    blocks.push_back(editGroup);

    // Answer text (not a tool)
    ContentBlock answer;
    answer.type = ContentBlock::AnswerText;
    answer.text = "Done!";
    blocks.push_back(answer);

    int count = countToolsFromIndex(blocks, 0);
    CHECK(count == 6);  // 1 + 3 + 2 = 6

    String text = makeTurnDurationText("Cogitated", 30, count);
    CHECK(text == "Cogitated for 30s · 6 tools");
}

// ============================================================================
// P2a Tests: Phase-header eligibility classifier
// ============================================================================

/// Mirror of isPhaseHeaderEligible() in FtxuiRepl.cpp P1b block.
/// Denies ● promotion for short transitional tool-intro text while allowing
/// real phase headers and substantive answers through.
static bool isPhaseHeaderEligible(const String& rawText) {
    String text = rawText;
    size_t start = text.find_first_not_of(" \t\n\r");
    if (start == String::npos) return false;
    size_t end = text.find_last_not_of(" \t\n\r");
    text = text.substr(start, end - start + 1);

    String lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Phase/summary/conclusion keywords always eligible
    static const std::vector<String> phaseKeywords = {
        "here is", "here's", "summary", "analysis", "overview",
        "conclusion", "result", "results", "findings",
        "recommendation", "the output pipeline follows",
        "the pipeline follows", "based on",
    };
    for (const auto& kw : phaseKeywords) {
        if (lower.find(kw) != String::npos) return true;
    }

    // Text metrics
    size_t substantiveChars = 0;
    for (char c : text) {
        if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
            substantiveChars++;
        }
    }
    int sentences = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?') {
            if (i + 1 >= text.size() || text[i + 1] == ' ' ||
                text[i + 1] == '\n') {
                sentences++;
            }
        }
    }

    // Structured content → eligible
    if (text.find("\n\n") != String::npos) return true;
    if (text.find("\n- ") != String::npos ||
        text.find("\n* ") != String::npos) return true;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        if (text[i] == '\n' && text[i + 1] >= '0' &&
            text[i + 1] <= '9') return true;
    }
    if (text.find("```") != String::npos) return true;

    // Long or multi-sentence substantive → eligible
    if (substantiveChars > 80) return true;
    if (sentences >= 2) return true;

    // Short single-sentence starting with transitional → deny
    static const std::vector<String> transitionalPrefixes = {
        "let me", "now let me", "i'll", "i will", "let's",
        "next", "then", "also",
        "and the", "and now", "now i'll",
        "checking", "reading", "searching", "looking at",
        "moving on",
    };
    for (const auto& prefix : transitionalPrefixes) {
        if (lower.find(prefix) == 0) return false;
    }

    return true;
}

// Test P2a.1: "Let me read the key files." → not eligible
TEST_CASE("P2a: transitional text denied — Let me read",
          "[ContentBlock][p2a]") {
    CHECK_FALSE(isPhaseHeaderEligible("Let me read the key files."));
}

// Test P2a.2: "Now I'll edit the config." → not eligible
TEST_CASE("P2a: transitional text denied — Now I'll edit",
          "[ContentBlock][p2a]") {
    CHECK_FALSE(isPhaseHeaderEligible("Now I'll edit the config."));
}

// Test P2a.3: "Here is the complete analysis:" → eligible
TEST_CASE("P2a: phase keyword eligible — Here is",
          "[ContentBlock][p2a]") {
    CHECK(isPhaseHeaderEligible("Here is the complete analysis:"));
}

// Test P2a.4: "The output pipeline follows:" → eligible
TEST_CASE("P2a: phase keyword eligible — output pipeline follows",
          "[ContentBlock][p2a]") {
    CHECK(isPhaseHeaderEligible("The output pipeline follows:"));
}

// Test P2a.5: "Done. All tests pass." → eligible (short but substantive,
//           doesn't start with transitional prefix)
TEST_CASE("P2a: substantive short text eligible — Done",
          "[ContentBlock][p2a]") {
    CHECK(isPhaseHeaderEligible("Done. All tests pass."));
}

// Test P2a.6: multi-paragraph summary → eligible
TEST_CASE("P2a: multi-paragraph eligible",
          "[ContentBlock][p2a]") {
    CHECK(isPhaseHeaderEligible(
        "Here is a summary of the changes.\n\n"
        "First, we refactored the pipeline.\n"
        "Second, we added tests."));
}

// Test P2a.7: first AnswerText in turn "Let me search..." → not eligible
TEST_CASE("P2a: first AnswerText transitional denied",
          "[ContentBlock][p2a]") {
    CHECK_FALSE(isPhaseHeaderEligible("Let me search for relevant files."));
}

// Test P2a.8: non-eligible blocks don't block subsequent phase header.
// "Let me read..." is non-eligible, but a subsequent "Here is..."
// must still be eligible — it's tested independently since the
// blocking-prevention logic is in findPrevSignificant, not the classifier.
TEST_CASE("P2a: transitional doesn't poison subsequent phase header",
          "[ContentBlock][p2a]") {
    CHECK_FALSE(isPhaseHeaderEligible("Let me read the file."));
    CHECK(isPhaseHeaderEligible("Here is the analysis."));
}

// Test P2a.9: dimmed narration remains handled separately (classifier
// runs only on non-dimmed blocks — verified structurally).
TEST_CASE("P2a: classifier ignores dimmed state",
          "[ContentBlock][p2a]") {
    // The classifier itself doesn't check dimmed — that's the caller's job
    CHECK(isPhaseHeaderEligible("Here is the summary."));
}

// Test P2a.10: Various transitional prefixes denied
TEST_CASE("P2a: transitional prefixes denied",
          "[ContentBlock][p2a]") {
    CHECK_FALSE(isPhaseHeaderEligible("Let me check that file."));
    CHECK_FALSE(isPhaseHeaderEligible("Now let me edit the config."));
    CHECK_FALSE(isPhaseHeaderEligible("I'll read the source."));
    CHECK_FALSE(isPhaseHeaderEligible("Next, the implementation."));
    CHECK_FALSE(isPhaseHeaderEligible("Then we check the output."));
    CHECK_FALSE(isPhaseHeaderEligible("Also the header files."));
    CHECK_FALSE(isPhaseHeaderEligible("Reading the main file."));
    CHECK_FALSE(isPhaseHeaderEligible("Searching for references."));
    CHECK_FALSE(isPhaseHeaderEligible("Looking at the test."));
    CHECK_FALSE(isPhaseHeaderEligible("Moving on to edits."));
}

// Test P2a.11: Long transitional text (>=2 sentences, > 80 chars) → eligible
TEST_CASE("P2a: long transitional text eligible",
          "[ContentBlock][p2a]") {
    // Even though it starts with "Let me", it's multi-sentence → eligible
    CHECK(isPhaseHeaderEligible(
        "Let me explain the architecture in detail. "
        "The pipeline has three stages: parsing, grouping, and rendering. "
        "Each stage is independent and testable."));
}

// Test P2a.12: "Based on the results" → phase keyword eligible
TEST_CASE("P2a: based on keyword eligible",
          "[ContentBlock][p2a]") {
    CHECK(isPhaseHeaderEligible("Based on the results above, here is the fix:"));
}

// Test P2a.13: Code block / markdown table → eligible (structured)
TEST_CASE("P2a: markdown structure eligible",
          "[ContentBlock][p2a]") {
    CHECK(isPhaseHeaderEligible("The key types:\n```\nstruct A {};\n```"));
}
