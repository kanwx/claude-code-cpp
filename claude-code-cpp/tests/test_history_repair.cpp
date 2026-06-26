#include <catch2/catch_test_macros.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/core/ApiTypes.hpp>

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
