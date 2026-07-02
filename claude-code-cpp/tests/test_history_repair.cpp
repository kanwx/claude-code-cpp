#include <catch2/catch_test_macros.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/core/ApiTypes.hpp>
#include <claude/core/ContentBlockParam.hpp>

using namespace claude;

// ============================================================================
// Helpers
// ============================================================================

static ToolCall makeTc(const String& id, const String& name = "Bash") {
    ToolCall tc;
    tc.id = id;
    tc.name = name;
    tc.arguments = "{}";
    return tc;
}

static ToolResponse makeTr(const String& callId, const String& name = "Bash",
                           const String& content = "ok", bool isError = false) {
    ToolResponse tr;
    tr.callId = callId;
    tr.toolName = name;
    tr.content = content;
    tr.isError = isError;
    return tr;
}

// Check that tool_result at history[idx] covers exactly the given callIds
static void checkToolResultAt(const std::vector<Message>& history, size_t idx,
                              const std::vector<String>& expectedCallIds) {
    REQUIRE(idx < history.size());
    REQUIRE(history[idx].role == MessageRole::ToolResult);
    REQUIRE(history[idx].toolResults.size() == expectedCallIds.size());
    for (size_t j = 0; j < expectedCallIds.size(); ++j) {
        CHECK(history[idx].toolResults[j].callId == expectedCallIds[j]);
    }
}

// ============================================================================
// P2 Test 1: Cancel after execution → tool_result written
// ============================================================================
TEST_CASE("P2: Cancel after tool execution — tool_result written to history",
          "[p2][history_repair][regression]") {
    // Simulates: user prompt → assistant with tool_use → tools execute →
    // cancel fires. Before the fix, toolResponses were discarded.
    // After the fix, they're inserted right after the assistant.

    std::vector<Message> history;
    history.push_back(Message::user("Run a command"));

    auto tc1 = makeTc("call-1", "Bash");
    auto tc2 = makeTc("call-2", "Read");
    history.push_back(Message::assistant("Let me run those.", {tc1, tc2}));

    std::vector<ToolCall> expected = {tc1, tc2};
    std::vector<ToolResponse> actual;
    actual.push_back(makeTr("call-1", "Bash", "command output"));
    actual.push_back(makeTr("call-2", "Read", "file content"));

    // Before repair: no tool_result after assistant
    REQUIRE(history.size() == 2);
    REQUIRE_FALSE(validateHistoryAfterRepair(history));

    // Apply repair
    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // After repair: tool_result inserted after assistant
    REQUIRE(history.size() == 3);
    CHECK(history[0].role == MessageRole::User);
    CHECK(history[1].role == MessageRole::Assistant);
    CHECK(history[2].role == MessageRole::ToolResult);
    checkToolResultAt(history, 2, {"call-1", "call-2"});
    CHECK(history[2].toolResults[0].content == "command output");
    CHECK(history[2].toolResults[1].content == "file content");
    CHECK_FALSE(history[2].toolResults[0].isError);
    CHECK_FALSE(history[2].toolResults[1].isError);

    // History must be valid after repair
    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P2 Test 2: Late result after new user → inserted before user, not appended
// ============================================================================
TEST_CASE("P2: Late result inserted before new user message (detached thread race)",
          "[p2][history_repair][regression]") {
    // Simulates the race: old agent thread cancelled, new prompt started,
    // new user message is already in history. Old thread's tool result
    // must be inserted after its assistant, BEFORE the new user message.

    std::vector<Message> history;
    history.push_back(Message::user("First prompt"));

    auto tc = makeTc("call-1", "Bash");
    history.push_back(Message::assistant("Running command.", {tc}));

    // New prompt started before old tool result arrived
    history.push_back(Message::user("Second prompt (new turn)"));

    std::vector<ToolCall> expected = {tc};
    std::vector<ToolResponse> actual;
    actual.push_back(makeTr("call-1", "Bash", "late result"));

    REQUIRE(history.size() == 3);

    // Apply repair
    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // Tool result must be between assistant and new user, NOT at end
    REQUIRE(history.size() == 4);
    CHECK(history[0].role == MessageRole::User);
    CHECK(history[0].content == "First prompt");
    CHECK(history[1].role == MessageRole::Assistant);
    CHECK(history[2].role == MessageRole::ToolResult);
    checkToolResultAt(history, 2, {"call-1"});
    CHECK(history[3].role == MessageRole::User);
    CHECK(history[3].content == "Second prompt (new turn)");

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P2 Test 3: Partial toolResponses → missing IDs get synthetic error
// ============================================================================
TEST_CASE("P2: Partial tool results — missing IDs get synthetic error",
          "[p2][history_repair]") {
    // When executeToolCalls returns fewer results than expected
    // (e.g., one tool crashed), missing IDs get synthetic error results.

    std::vector<Message> history;
    history.push_back(Message::user("Do three things"));

    auto tc1 = makeTc("call-1", "Bash");
    auto tc2 = makeTc("call-2", "Read");
    auto tc3 = makeTc("call-3", "Write");
    history.push_back(Message::assistant("Doing all three.", {tc1, tc2, tc3}));

    std::vector<ToolCall> expected = {tc1, tc2, tc3};
    std::vector<ToolResponse> actual;
    // Only tc1 completed — tc2 and tc3 are missing
    actual.push_back(makeTr("call-1", "Bash", "bash output"));

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    REQUIRE(history.size() == 3);
    CHECK(history[2].role == MessageRole::ToolResult);
    // Should have 3 results: 1 real + 2 synthetic
    REQUIRE(history[2].toolResults.size() == 3);

    // Real result
    CHECK(history[2].toolResults[0].callId == "call-1");
    CHECK(history[2].toolResults[0].content == "bash output");
    CHECK_FALSE(history[2].toolResults[0].isError);

    // Synthetic results
    CHECK(history[2].toolResults[1].callId == "call-2");
    CHECK(history[2].toolResults[1].content == "Interrupted");
    CHECK(history[2].toolResults[1].isError);
    CHECK(history[2].toolResults[1].isCancelled);

    CHECK(history[2].toolResults[2].callId == "call-3");
    CHECK(history[2].toolResults[2].content == "Interrupted");
    CHECK(history[2].toolResults[2].isError);
    CHECK(history[2].toolResults[2].isCancelled);

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P2 Test 4: Duplicate prevention — actual takes priority over existing
// ============================================================================
TEST_CASE("P2: Duplicate prevention — actual results beat existing late results",
          "[p2][history_repair]") {
    // A detached-thread race left a late tool_result in history.
    // The fresh actual result (from the current executeToolCalls) must win.

    std::vector<Message> history;
    history.push_back(Message::user("Run command"));

    auto tc = makeTc("call-1", "Bash");
    history.push_back(Message::assistant("Running.", {tc}));

    // Simulate: detached thread already wrote a stale tool_result at wrong position
    history.push_back(Message::user("Stale user message in between"));
    std::vector<ToolResponse> staleResults;
    staleResults.push_back(makeTr("call-1", "Bash", "stale output from old thread", false));
    history.push_back(Message::toolResult(std::move(staleResults)));

    REQUIRE(history.size() == 4);  // user, assistant, user, stale_tool_result

    std::vector<ToolCall> expected = {tc};
    std::vector<ToolResponse> actual;
    actual.push_back(makeTr("call-1", "Bash", "fresh output from current execution", false));

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // After repair:
    // - Stale tool_result at end is removed
    // - Fresh result inserted after assistant, before stale user
    REQUIRE(history.size() == 4);
    CHECK(history[0].role == MessageRole::User);
    CHECK(history[1].role == MessageRole::Assistant);
    CHECK(history[2].role == MessageRole::ToolResult);
    CHECK(history[3].role == MessageRole::User);
    CHECK(history[3].content == "Stale user message in between");

    // Fresh result wins
    REQUIRE(history[2].toolResults.size() == 1);
    CHECK(history[2].toolResults[0].callId == "call-1");
    CHECK(history[2].toolResults[0].content == "fresh output from current execution");
    CHECK_FALSE(history[2].toolResults[0].isError);

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P2 Test 5: Normal success path — existing tool_result preserved
// ============================================================================
TEST_CASE("P2: Normal success path — tool_result already at correct position",
          "[p2][history_repair]") {
    // When the tool_result is already in the correct position (normal TAOR path),
    // insertOrMergeToolResultsAfterAssistant should be a no-op that preserves
    // the existing data.

    std::vector<Message> history;
    history.push_back(Message::user("Run command"));

    auto tc = makeTc("call-1", "Bash");
    history.push_back(Message::assistant("Running.", {tc}));

    // Normal path already wrote tool_result
    std::vector<ToolResponse> existingResults;
    existingResults.push_back(makeTr("call-1", "Bash", "existing output", false));
    history.push_back(Message::toolResult(std::move(existingResults)));

    REQUIRE(history.size() == 3);
    REQUIRE(validateHistoryAfterRepair(history));

    // Save a copy for comparison
    auto historyBefore = history;

    // Call insert with empty actual (already in history)
    std::vector<ToolCall> expected = {tc};
    std::vector<ToolResponse> actual;  // empty — already in history

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // History should be unchanged (no new messages, no reordering)
    REQUIRE(history.size() == 3);
    CHECK(history[0].role == MessageRole::User);
    CHECK(history[1].role == MessageRole::Assistant);
    CHECK(history[2].role == MessageRole::ToolResult);
    REQUIRE(history[2].toolResults.size() == 1);
    CHECK(history[2].toolResults[0].callId == "call-1");
    CHECK(history[2].toolResults[0].content == "existing output");

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P2 Test 6: After repair, validateHistoryAfterRepair passes
//            (P0 synthetic errors no longer needed)
// ============================================================================
TEST_CASE("P2: validateHistoryAfterRepair passes after repair (no orphans)",
          "[p2][history_repair]") {
    // Every scenario that calls insertToolResultsIntoHistory must produce
    // a valid history where all tool_use blocks have matching tool_results
    // immediately after. This means P0's synthetic error injection
    // ("[Error: tool execution was interrupted]") is no longer triggered.

    SECTION("Single tool, cancel after execution") {
        std::vector<Message> history;
        history.push_back(Message::user("input"));

        auto tc = makeTc("call-1");
        history.push_back(Message::assistant("text", {tc}));

        std::vector<ToolCall> expected = {tc};
        std::vector<ToolResponse> actual;
        actual.push_back(makeTr("call-1", "Bash", "result"));

        REQUIRE_FALSE(validateHistoryAfterRepair(history));
        insertToolResultsIntoHistory(history, expected, actual, "Interrupted");
        REQUIRE(validateHistoryAfterRepair(history));
    }

    SECTION("Multiple tools, one cancelled") {
        std::vector<Message> history;
        history.push_back(Message::user("input"));

        auto tc1 = makeTc("call-1");
        auto tc2 = makeTc("call-2");
        auto tc3 = makeTc("call-3");
        history.push_back(Message::assistant("text", {tc1, tc2, tc3}));

        std::vector<ToolCall> expected = {tc1, tc2, tc3};
        std::vector<ToolResponse> actual;
        actual.push_back(makeTr("call-1", "Bash", "ok"));
        // call-2 and call-3 missing → synthetic

        REQUIRE_FALSE(validateHistoryAfterRepair(history));
        insertToolResultsIntoHistory(history, expected, actual, "Interrupted");
        REQUIRE(validateHistoryAfterRepair(history));
    }

    SECTION("All results missing (all synthetic)") {
        std::vector<Message> history;
        history.push_back(Message::user("input"));

        auto tc1 = makeTc("call-1");
        auto tc2 = makeTc("call-2");
        history.push_back(Message::assistant("text", {tc1, tc2}));

        std::vector<ToolCall> expected = {tc1, tc2};
        std::vector<ToolResponse> actual;  // empty — all synthetic

        REQUIRE_FALSE(validateHistoryAfterRepair(history));
        insertToolResultsIntoHistory(history, expected, actual, "Interrupted");
        REQUIRE(validateHistoryAfterRepair(history));
        // Both must be synthetic
        CHECK(history[2].toolResults[0].isError);
        CHECK(history[2].toolResults[1].isError);
    }
}

// ============================================================================
// P2 Test 7: Old assistant not last assistant (match by ID, not position)
// ============================================================================
TEST_CASE("P2: Match old assistant by tool_use ID when it is not the last assistant",
          "[p2][history_repair][regression]") {
    // Scenario: assistant A (iteration 1) has tool_use but its tool_result was
    // never written. A new user prompt started, producing assistant B
    // (iteration 2) with different tool_use and a proper tool_result.
    //
    // insertToolResultsIntoHistory must find assistant A by matching
    // its tool_use IDs, not by picking the last assistant.

    std::vector<Message> history;

    // --- Iteration 1: old turn ---
    history.push_back(Message::user("First prompt"));

    auto oldTc1 = makeTc("old-call-1", "Bash");
    auto oldTc2 = makeTc("old-call-2", "Read");
    history.push_back(Message::assistant("Old assistant doing work.", {oldTc1, oldTc2}));
    // OLD BUG: tool_result NEVER written for oldTc1/oldTc2 (cancel/interrupted)

    // --- New turn started before old results arrived ---
    history.push_back(Message::user("Second prompt (new turn)"));

    auto newTc = makeTc("new-call-1", "Glob");
    history.push_back(Message::assistant("New assistant finding files.", {newTc}));

    std::vector<ToolResponse> newResults;
    newResults.push_back(makeTr("new-call-1", "Glob", "found 3 files"));
    history.push_back(Message::toolResult(std::move(newResults)));

    REQUIRE(history.size() == 5);  // user, oldAsst, user, newAsst, newToolResult

    // History is currently valid for newAsst but NOT for oldAsst
    REQUIRE_FALSE(validateHistoryAfterRepair(history));

    // --- Old thread's tool results finally arrive ---
    std::vector<ToolCall> expected = {oldTc1, oldTc2};
    std::vector<ToolResponse> actual;
    actual.push_back(makeTr("old-call-1", "Bash", "late bash output"));
    actual.push_back(makeTr("old-call-2", "Read", "late read output"));

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // After repair:
    // [0] User "First prompt"
    // [1] Assistant (old) ← oldTc1, oldTc2
    // [2] ToolResult ← old results INSERTED HERE
    // [3] User "Second prompt"
    // [4] Assistant (new) ← newTc
    // [5] ToolResult ← new results (UNCHANGED)
    REQUIRE(history.size() == 6);

    CHECK(history[0].role == MessageRole::User);
    CHECK(history[0].content == "First prompt");

    CHECK(history[1].role == MessageRole::Assistant);
    REQUIRE(history[1].toolCalls.size() == 2);
    CHECK(history[1].toolCalls[0].id == "old-call-1");
    CHECK(history[1].toolCalls[1].id == "old-call-2");

    // Old tool results inserted here, BEFORE new user
    CHECK(history[2].role == MessageRole::ToolResult);
    checkToolResultAt(history, 2, {"old-call-1", "old-call-2"});
    CHECK(history[2].toolResults[0].content == "late bash output");
    CHECK(history[2].toolResults[1].content == "late read output");

    // New user is now at index 3
    CHECK(history[3].role == MessageRole::User);
    CHECK(history[3].content == "Second prompt (new turn)");

    // New assistant is now at index 4 (unchanged content)
    CHECK(history[4].role == MessageRole::Assistant);
    REQUIRE(history[4].toolCalls.size() == 1);
    CHECK(history[4].toolCalls[0].id == "new-call-1");

    // New tool result is now at index 5 (unchanged)
    CHECK(history[5].role == MessageRole::ToolResult);
    checkToolResultAt(history, 5, {"new-call-1"});
    CHECK(history[5].toolResults[0].content == "found 3 files");

    // Both assistants now have valid tool_results
    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P3 Tests: ESC cancel lifecycle — history and idempotency
// ============================================================================

// ============================================================================
// P3 Test 1: Cancelled tool result attributes are preserved
// ============================================================================
TEST_CASE("P3: Cancelled tool result preserves isCancelled/isError in history",
          "[p3][history_repair]") {
    // When ESC fires during tool execution, the tool returns isCancelled=true.
    // insertToolResultsIntoHistory must preserve these flags so the UI can
    // render "Interrupted" instead of showing a normal success result.

    std::vector<Message> history;
    history.push_back(Message::user("Run sleep 30"));

    auto tc = makeTc("call-1", "Bash");
    history.push_back(Message::assistant("Running sleep.", {tc}));

    std::vector<ToolCall> expected = {tc};
    std::vector<ToolResponse> actual;
    ToolResponse cancelledResp;
    cancelledResp.callId = "call-1";
    cancelledResp.toolName = "Bash";
    cancelledResp.content = "Slept for 30 seconds.";
    cancelledResp.isError = false;
    cancelledResp.isCancelled = true;  // ESC was pressed
    actual.push_back(cancelledResp);

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    REQUIRE(history.size() == 3);
    REQUIRE(history[2].toolResults.size() == 1);
    CHECK(history[2].toolResults[0].callId == "call-1");
    CHECK(history[2].toolResults[0].isCancelled == true);
    // Content is preserved even though cancelled (tool actually finished)
    CHECK(history[2].toolResults[0].content == "Slept for 30 seconds.");
    CHECK_FALSE(history[2].toolResults[0].isError);

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P3 Test 2: Idempotent — repeated inserts don't duplicate
// ============================================================================
TEST_CASE("P3: Repeated insertToolResultsIntoHistory calls are idempotent",
          "[p3][history_repair]") {
    // The ESC handler fires once, but finishStream + stale thread callbacks
    // could theoretically trigger a second insert attempt. The function
    // must be idempotent: no duplicate tool_result messages, no duplicate
    // tool responses within a message.

    std::vector<Message> history;
    history.push_back(Message::user("Run command"));

    auto tc = makeTc("call-1", "Bash");
    history.push_back(Message::assistant("Running.", {tc}));

    std::vector<ToolCall> expected = {tc};
    std::vector<ToolResponse> actual;
    actual.push_back(makeTr("call-1", "Bash", "result"));

    // First insert
    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");
    REQUIRE(history.size() == 3);
    REQUIRE(history[2].toolResults.size() == 1);

    // Second insert — must be idempotent (no-op on same data)
    std::vector<ToolResponse> actual2;
    actual2.push_back(makeTr("call-1", "Bash", "result"));
    insertToolResultsIntoHistory(history, expected, actual2, "Interrupted");

    // No duplicate tool_result message inserted
    REQUIRE(history.size() == 3);

    // No duplicate tool response within the message
    REQUIRE(history[2].toolResults.size() == 1);
    CHECK(history[2].toolResults[0].callId == "call-1");

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P3 Test 3: All-tools-cancelled path produces valid history
// ============================================================================
TEST_CASE("P3: All tools cancelled — every ID gets synthetic error",
          "[p3][history_repair]") {
    // ESC fires before any tool starts executing. The tool executor
    // cancels all pending tools, returning synthetic cancelled results.
    // insertToolResultsIntoHistory must fill in all tool_use IDs.

    std::vector<Message> history;
    history.push_back(Message::user("Run three commands"));

    auto tc1 = makeTc("call-1", "Bash");
    auto tc2 = makeTc("call-2", "Read");
    auto tc3 = makeTc("call-3", "Write");
    history.push_back(Message::assistant("Running all three.", {tc1, tc2, tc3}));

    std::vector<ToolCall> expected = {tc1, tc2, tc3};
    // Simulate: ESC cancelled all tools, no actual results
    std::vector<ToolResponse> actual;  // empty — all cancelled

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    REQUIRE(history.size() == 3);
    REQUIRE(history[2].toolResults.size() == 3);

    // All three must be synthetic error results
    for (int i = 0; i < 3; i++) {
        CHECK(history[2].toolResults[i].isError == true);
        CHECK(history[2].toolResults[i].isCancelled == true);
        CHECK(history[2].toolResults[i].content == "Interrupted");
    }

    // IDs must match expected order
    CHECK(history[2].toolResults[0].callId == "call-1");
    CHECK(history[2].toolResults[1].callId == "call-2");
    CHECK(history[2].toolResults[2].callId == "call-3");

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P3 Test 4: ESC + new prompt race with cancelled late result
// ============================================================================
TEST_CASE("P3: ESC cancel — late cancelled result inserted before new prompt",
          "[p3][history_repair][regression]") {
    // Real ESC scenario:
    // 1. User submits "Run sleep 30"
    // 2. Tool starts running
    // 3. User presses ESC → cancel flag set
    // 4. User immediately submits "你好"
    // 5. Sleep finishes, result marked isCancelled=true
    // 6. insertToolResultsIntoHistory must place result BEFORE "你好"

    std::vector<Message> history;
    history.push_back(Message::user("Run sleep 30"));

    auto sleepTc = makeTc("sleep-call", "Bash");
    history.push_back(Message::assistant("Running sleep.", {sleepTc}));

    // New prompt submitted while sleep was still running
    history.push_back(Message::user("你好"));

    // Sleep finishes, marked cancelled
    std::vector<ToolCall> expected = {sleepTc};
    std::vector<ToolResponse> actual;
    ToolResponse cancelledResp;
    cancelledResp.callId = "sleep-call";
    cancelledResp.toolName = "Bash";
    cancelledResp.content = "Slept for 30 seconds.";
    cancelledResp.isCancelled = true;
    actual.push_back(cancelledResp);

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // After repair:
    // [0] User "Run sleep 30"
    // [1] Assistant ← sleep tool_use
    // [2] ToolResult ← cancelled sleep result INSERTED HERE
    // [3] User "你好"
    REQUIRE(history.size() == 4);

    CHECK(history[0].role == MessageRole::User);
    CHECK(history[0].content == "Run sleep 30");

    CHECK(history[1].role == MessageRole::Assistant);
    REQUIRE(history[1].toolCalls.size() == 1);
    CHECK(history[1].toolCalls[0].id == "sleep-call");

    // Cancelled tool result must be between assistant and new user, not after
    CHECK(history[2].role == MessageRole::ToolResult);
    REQUIRE(history[2].toolResults.size() == 1);
    CHECK(history[2].toolResults[0].callId == "sleep-call");
    CHECK(history[2].toolResults[0].isCancelled == true);

    // New user message is untouched at the end
    CHECK(history[3].role == MessageRole::User);
    CHECK(history[3].content == "你好");

    // History must be valid — no tool_use followed by user text
    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// P3 Test 5: Partially cancelled batch — some succeed, some cancelled
// ============================================================================
TEST_CASE("P3: Partially cancelled batch — mixed success and cancelled results",
          "[p3][history_repair]") {
    // ESC fires mid-batch: tool 1 already completed (success), tools 2 & 3
    // were cancelled before they could start. The ToolResponse array has
    // mix of success and cancelled.

    std::vector<Message> history;
    history.push_back(Message::user("Run three tools"));

    auto tc1 = makeTc("call-1", "Read");
    auto tc2 = makeTc("call-2", "Bash");
    auto tc3 = makeTc("call-3", "Glob");
    history.push_back(Message::assistant("Running tools.", {tc1, tc2, tc3}));

    std::vector<ToolCall> expected = {tc1, tc2, tc3};
    std::vector<ToolResponse> actual;
    // Tool 1 succeeded before ESC
    actual.push_back(makeTr("call-1", "Read", "file contents"));
    // Tools 2 & 3 were cancelled (ESC fired during batch)
    ToolResponse cancelled2;
    cancelled2.callId = "call-2";
    cancelled2.toolName = "Bash";
    cancelled2.content = "Cancelled by user";
    cancelled2.isCancelled = true;
    actual.push_back(cancelled2);
    ToolResponse cancelled3;
    cancelled3.callId = "call-3";
    cancelled3.toolName = "Glob";
    cancelled3.content = "Cancelled by user";
    cancelled3.isCancelled = true;
    actual.push_back(cancelled3);

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    REQUIRE(history.size() == 3);
    REQUIRE(history[2].toolResults.size() == 3);

    // Tool 1: success
    CHECK(history[2].toolResults[0].callId == "call-1");
    CHECK(history[2].toolResults[0].content == "file contents");
    CHECK_FALSE(history[2].toolResults[0].isCancelled);
    CHECK_FALSE(history[2].toolResults[0].isError);

    // Tool 2: cancelled
    CHECK(history[2].toolResults[1].callId == "call-2");
    CHECK(history[2].toolResults[1].isCancelled == true);

    // Tool 3: cancelled
    CHECK(history[2].toolResults[2].callId == "call-3");
    CHECK(history[2].toolResults[2].isCancelled == true);

    REQUIRE(validateHistoryAfterRepair(history));
}

// ============================================================================
// cancelGeneration race tests: force-override after resetCancel() clears flag
// ============================================================================

// Test: CancelledRunLateSuccessIsInterruptedAfterResetCancel
//
// Simulates the cancelGeneration sticky cancel detection:
//   1. Run A starts tool_use sleep 30
//   2. cancel() increments cancelGeneration (N → N+1)
//   3. resetCancel() in new turn clears impl_->cancelled (now false)
//   4. Run A's sleep(30) completes, tool process returns "Slept for 30 seconds"
//   5. executeLoop detects cancelGeneration changed → turnCancelled=true
//   6. Force-override ALL responses: isCancelled=true,
//      content="Interrupted: tool execution was cancelled"
//   7. insertOrMergeToolResultsAfterAssistant writes corrected result to history
//
// Key assertions:
//   - History has "Interrupted: tool execution was cancelled" NOT "Slept for 30 seconds"
//   - isCancelled=true on the inserted result
//   - Tool_result placed BETWEEN assistant and next user (not after)
//   - validateHistoryAfterRepair passes
TEST_CASE("cancelGen: force-overridden result after resetCancel race — "
          "Interrupted content, not real tool output",
          "[cancelgen][history_repair][regression]") {
    std::vector<Message> history;
    history.push_back(Message::user("! sleep 30"));

    auto tc = makeTc("sleep-call", "Bash");
    history.push_back(Message::assistant("Running sleep.", {tc}));

    // Simulate new turn's user input while old run's tool is still pending
    history.push_back(Message::user("! read CMakeLists.txt"));

    std::vector<ToolCall> expected = {tc};

    // Simulate the force-override: tool completed normally ("Slept for 30 seconds")
    // but executeLoop rewrote content and set isCancelled=true
    std::vector<ToolResponse> actual;
    ToolResponse forceOverridden;
    forceOverridden.callId = "sleep-call";
    forceOverridden.toolName = "Bash";
    forceOverridden.content = "Interrupted: tool execution was cancelled";
    forceOverridden.isCancelled = true;
    // isError stays false per the force-override logic (only isCancelled is set)
    forceOverridden.isError = false;
    actual.push_back(forceOverridden);

    insertToolResultsIntoHistory(history, expected, actual, "Interrupted");

    // After repair:
    // [0] User "! sleep 30"
    // [1] Assistant ← sleep tool_use
    // [2] ToolResult ← force-overridden Interrupted result (BEFORE next user)
    // [3] User "! read CMakeLists.txt"
    REQUIRE(history.size() == 4);

    CHECK(history[0].role == MessageRole::User);
    CHECK(history[1].role == MessageRole::Assistant);
    CHECK(history[2].role == MessageRole::ToolResult);
    CHECK(history[3].role == MessageRole::User);

    // Verify inserted result
    REQUIRE(history[2].toolResults.size() == 1);
    CHECK(history[2].toolResults[0].callId == "sleep-call");
    CHECK(history[2].toolResults[0].isCancelled == true);
    CHECK(history[2].toolResults[0].isError == false);
    CHECK(history[2].toolResults[0].content == "Interrupted: tool execution was cancelled");

    // Verify "Slept for 30 seconds" is NOT present
    CHECK(history[2].toolResults[0].content.find("Slept for 30 seconds") == String::npos);

    // History validation must pass
    REQUIRE(validateHistoryAfterRepair(history));
}

// Test: LateCancelledToolResultDoesNotLockInput
//
// Multiple consecutive ESC+resubmit cycles produce late tool results that
// arrive after resetCancel() in a new turn. If force-override is absent,
// stale "success" results reach the UI and lock the input.
//
// This test verifies insertToolResultsIntoHistory correctly handles:
//   - Multiple cancelled runs with different tool_use IDs
//   - Deduplication: late results from earlier runs don't duplicate
//   - Force-overridden results placed between correct assistant and user
//   - No stale success content leaks into history
TEST_CASE("cancelGen: multiple late cancelled results — no stale success, no UI lock",
          "[cancelgen][history_repair][regression]") {
    std::vector<Message> history;

    // Turn 1: user "sleep 30" → tool_use call-1
    history.push_back(Message::user("! sleep 30"));
    auto tc1 = makeTc("call-1", "Bash");
    history.push_back(Message::assistant("Sleeping 30.", {tc1}));

    // ESC + new prompt: user "sleep 10" (before call-1 finished)
    history.push_back(Message::user("! sleep 10"));

    // Turn 2: tool_use call-2
    auto tc2 = makeTc("call-2", "Bash");
    history.push_back(Message::assistant("Sleeping 10.", {tc2}));

    // ESC + new prompt: user "read file" (before call-2 finished)
    history.push_back(Message::user("! read CMakeLists.txt"));

    // Now both late results arrive simultaneously.
    // call-1 (sleep 30): finished normally, returns "Slept for 30 seconds"
    // call-2 (sleep 10): finished normally, returns "Slept for 10 seconds"
    // Both are force-overridden by executeLoop's cancelGeneration detection.

    // Repair call-1 (matches first assistant)
    {
        std::vector<ToolResponse> actual1;
        ToolResponse r1;
        r1.callId = "call-1";
        r1.toolName = "Bash";
        r1.content = "Interrupted: tool execution was cancelled";
        r1.isCancelled = true;
        actual1.push_back(r1);
        insertToolResultsIntoHistory(history, {tc1}, actual1, "Interrupted");
    }

    // Repair call-2 (matches second assistant)
    {
        std::vector<ToolResponse> actual2;
        ToolResponse r2;
        r2.callId = "call-2";
        r2.toolName = "Bash";
        r2.content = "Interrupted: tool execution was cancelled";
        r2.isCancelled = true;
        actual2.push_back(r2);
        insertToolResultsIntoHistory(history, {tc2}, actual2, "Interrupted");
    }

    // After both repairs, no consecutive user messages should exist.
    // Expected structure:
    // [0] User "! sleep 30"
    // [1] Assistant (call-1)
    // [2] ToolResult (call-1: Interrupted)
    // [3] User "! sleep 10"
    // [4] Assistant (call-2)
    // [5] ToolResult (call-2: Interrupted)
    // [6] User "! read CMakeLists.txt"

    // Verify no consecutive user messages
    String prevRole;
    for (size_t i = 0; i < history.size(); ++i) {
        String role = (history[i].role == MessageRole::User) ? "user" :
                      (history[i].role == MessageRole::Assistant) ? "assistant" :
                      (history[i].role == MessageRole::ToolResult) ? "tool_result" : "system";
        INFO("Message " << i << " role=" << role);
        if (!prevRole.empty() && role == "user" && prevRole == "user") {
            FAIL("Consecutive user messages at index " << i);
        }
        prevRole = role;
    }

    // Count tool_result messages: should be exactly 2
    int toolResultCount = 0;
    for (auto& msg : history) {
        if (msg.role == MessageRole::ToolResult) toolResultCount++;
    }
    CHECK(toolResultCount == 2);

    // Verify both results are marked cancelled with Interrupted content
    for (auto& msg : history) {
        if (msg.role != MessageRole::ToolResult) continue;
        for (auto& tr : msg.toolResults) {
            CHECK(tr.isCancelled == true);
            CHECK(tr.content.find("Interrupted") != String::npos);
            CHECK(tr.content.find("Slept for") == String::npos);
        }
    }

    // History validation must pass
    REQUIRE(validateHistoryAfterRepair(history));

    // Convert to ContentBlockParam and validate serialized JSON
    std::vector<ContentMessage> cm;
    for (auto& msg : history) cm.push_back(convertLegacyMessage(msg));

    // Verify via injectMissingToolResults: should be no-op (all results present)
    int injected = injectMissingToolResults(cm);
    CHECK(injected == 0);

    // All validators
    REQUIRE(validateToolResultOrdering(cm));
    REQUIRE(validateEmptyMessages(cm));
    auto request = buildAnthropicApiMessages(cm);
    Json fullReq;
    fullReq["model"] = "claude-sonnet-4-6";
    fullReq["max_tokens"] = 4096;
    fullReq["messages"] = request;
    REQUIRE(validateSerializedApiJson(fullReq));
}
