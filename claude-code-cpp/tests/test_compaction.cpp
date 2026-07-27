#include <catch2/catch_test_macros.hpp>
#include <claude/core/compact/MicroCompact.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/Types.hpp>
#include <unordered_set>

using namespace claude;
using namespace claude::compact;

// Helper: build a ToolResult message with a specific toolName in toolResults
static Message makeToolResult(const String& toolName, const String& content) {
    ToolResponse tr;
    tr.callId = "call-1";
    tr.toolName = toolName;
    tr.content = content;
    return Message::toolResult({tr});
}

// Helper: build a ToolResult message using metadata (for cache_breakpoint tests)
static Message makeToolResultWithMeta(const String& toolName, const String& content,
                                       const std::map<String, String>& meta) {
    Message msg = makeToolResult(toolName, content);
    for (const auto& [k, v] : meta) {
        msg.metadata[k] = v;
    }
    return msg;
}

// ============================================================================
// MicroCompact::applyByToolName
// ============================================================================

TEST_CASE("MicroCompact applyByToolName compacts large matching results", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("hello"));
    history.push_back(Message::assistant("hi"));

    // Large tool result from Read (> 500 chars, should be compacted)
    history.push_back(makeToolResult("Read", String(6000, 'x')));

    // Keep last 5 messages intact
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByToolName(history, {"Read"});
    REQUIRE(compacted == 1);
    // The ToolResult message content should be replaced with a placeholder
    REQUIRE(history[3].content.find("tool result content cleared") != String::npos);
}

TEST_CASE("MicroCompact applyByToolName skips small results", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));

    // Small tool result (< 500 chars, should NOT be compacted)
    history.push_back(makeToolResult("Read", "small"));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByToolName(history, {"Read"});
    REQUIRE(compacted == 0);
    REQUIRE(history[1].toolResults[0].content == "small"); // unchanged
}

TEST_CASE("MicroCompact applyByToolName skips non-matching tool names", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));

    // Large result from Bash, but we're filtering for Read only
    history.push_back(makeToolResult("Bash", String(6000, 'x')));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByToolName(history, {"Read"});
    REQUIRE(compacted == 0);
}

TEST_CASE("MicroCompact applyByToolName with empty filter compacts all tools", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(makeToolResult("Read", String(6000, 'x')));
    history.push_back(makeToolResult("Bash", String(6000, 'y')));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    // Empty toolNames = compact all tool results
    int compacted = MicroCompact::applyByToolName(history, {});
    REQUIRE(compacted == 2);
}

TEST_CASE("MicroCompact applyByToolName skips already-compacted messages", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));

    // Already compacted message
    Message alreadyCompacted = makeToolResult("Read", "[Old tool result content cleared - original size: 6000 chars]");
    history.push_back(alreadyCompacted);
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByToolName(history, {"Read"});
    REQUIRE(compacted == 0); // already compacted, skip
}

// ============================================================================
// MicroCompact::applyByPressure
// ============================================================================

TEST_CASE("MicroCompact applyByPressure respects cache breakpoints", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));

    // Large result with cache_breakpoint metadata — should NOT be compacted
    history.push_back(makeToolResultWithMeta("Read", String(6000, 'x'),
        {{"cache_breakpoint", "true"}}));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByPressure(history, 0.90);
    REQUIRE(compacted == 0);
}

TEST_CASE("MicroCompact applyByPressure at 0.85 compacts results > 3000 chars", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("hello"));

    // Result > 3000 chars but < 10000 chars — compacted at 0.85 pressure
    history.push_back(makeToolResult("Read", String(5000, 'x')));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByPressure(history, 0.85);
    REQUIRE(compacted == 1);
}

TEST_CASE("MicroCompact applyByPressure at 0.70 skips results < 10000 chars", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("hello"));

    // Result > 3000 but < 10000 — NOT compacted at 0.70 pressure
    history.push_back(makeToolResult("Read", String(5000, 'x')));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByPressure(history, 0.70);
    REQUIRE(compacted == 0);
}

TEST_CASE("MicroCompact applyByPressure below 0.70 does nothing", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(makeToolResult("Read", String(6000, 'x')));
    for (int i = 0; i < 5; i++) {
        history.push_back(Message::user("msg" + std::to_string(i)));
    }

    int compacted = MicroCompact::applyByPressure(history, 0.50);
    REQUIRE(compacted == 0);
}

// ============================================================================
// PostCompactCleanup
// ============================================================================

TEST_CASE("PostCompactCleanup removes empty messages", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user(""));       // empty
    history.push_back(Message::assistant("hi"));
    history.push_back(Message::user(""));       // empty

    int removed = PostCompactCleanup::removeEmptyMessages(history);
    REQUIRE(removed == 2);
    REQUIRE(history.size() == 2); // system + assistant
}

TEST_CASE("PostCompactCleanup deduplicates system messages", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system1"));
    history.push_back(Message::system("system2")); // duplicate
    history.push_back(Message::user("hello"));

    int removed = PostCompactCleanup::deduplicateSystemMessages(history);
    REQUIRE(removed == 1);
    REQUIRE(history.size() == 2);
}

TEST_CASE("PostCompactCleanup removes orphaned tool results", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));

    // Tool result with callId "orphan" — no matching ToolCall exists
    Message orphan(MessageRole::ToolResult);
    ToolResponse tr;
    tr.callId = "orphan-call";
    tr.toolName = "Read";
    tr.content = "orphaned content";
    orphan.toolResults.push_back(tr);
    history.push_back(orphan);

    history.push_back(Message::user("hello"));

    int removed = PostCompactCleanup::removeOrphanedResults(history);
    REQUIRE(removed == 1);
    REQUIRE(history.size() == 2); // system + user
}

TEST_CASE("PostCompactCleanup enforces alternation", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("msg1"));
    history.push_back(Message::user("msg2")); // consecutive user messages

    int fixes = PostCompactCleanup::enforceAlternation(history);
    REQUIRE(fixes >= 1);
    // After enforcement, no two consecutive messages should have the same effective role
    for (size_t i = 2; i < history.size(); ++i) {
        auto effectiveRole = [](const Message& msg) -> MessageRole {
            if (msg.role == MessageRole::ToolResult) return MessageRole::User;
            return msg.role;
        };
        // Allow system messages to be followed by anything
        if (history[i - 1].role == MessageRole::System) continue;
        REQUIRE(effectiveRole(history[i]) != effectiveRole(history[i - 1]));
    }
}

TEST_CASE("Full compaction pipeline: MicroCompact + PostCompactCleanup", "[compact][integration]") {
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("hello"));
    history.push_back(Message::assistant("hi"));

    // Several large tool results
    history.push_back(makeToolResult("Read", String(8000, 'a')));
    history.push_back(makeToolResult("Bash", String(8000, 'b')));
    history.push_back(Message::user("follow-up"));

    // Step 1: MicroCompact by tool name (keepLast=1: only protect the final user message)
    int compacted = MicroCompact::applyByToolName(history, {"Read", "Bash"}, 1);
    REQUIRE(compacted == 2);

    // Step 2: PostCompactCleanup
    PostCompactCleanup::cleanup(history);

    // After cleanup, history should still be valid (no empty messages, proper alternation)
    for (const auto& msg : history) {
        bool isEmpty = msg.content.empty() && msg.toolCalls.empty() && msg.toolResults.empty();
        // Compacted messages have placeholder content so not empty; system/user/assistant are non-empty
        REQUIRE_FALSE(isEmpty);
    }
}

// ============================================================================
// enforceAlternation toolResults preservation (RCA for Path 6 400 error)
// ============================================================================

// Helper: build an assistant message with toolCalls
static Message makeAssistantWithToolCalls(const String& text,
                                           const std::vector<std::pair<String, String>>& tools) {
    Message msg = Message::assistant(text);
    for (const auto& [id, name] : tools) {
        ToolCall tc;
        tc.id = id;
        tc.name = name;
        tc.arguments = "{}";
        msg.toolCalls.push_back(std::move(tc));
    }
    return msg;
}

// Helper: build a ToolResult message with specific callId
static Message makeToolResultWithCallId(const String& callId,
                                         const String& toolName,
                                         const String& content) {
    ToolResponse tr;
    tr.callId = callId;
    tr.toolName = toolName;
    tr.content = content;
    return Message::toolResult({tr});
}

// Helper: verify all assistant toolCalls have matching toolResults in the
// immediately following message. Returns the list of unmatched tool_use IDs.
static std::vector<String> findOrphanedToolUses(const std::vector<Message>& history) {
    std::vector<String> orphans;
    for (size_t i = 0; i + 1 < history.size(); ++i) {
        const auto& current = history[i];
        const auto& next = history[i + 1];
        if (current.role != MessageRole::Assistant) continue;
        if (current.toolCalls.empty()) continue;

        // Collect all toolResult callIds from the next message
        std::unordered_set<String> resultIds;
        // Next message could be ToolResult or User (merged)
        if (next.role == MessageRole::ToolResult || next.role == MessageRole::User) {
            for (const auto& tr : next.toolResults) {
                resultIds.insert(tr.callId);
            }
        }

        for (const auto& tc : current.toolCalls) {
            if (resultIds.count(tc.id) == 0) {
                orphans.push_back(tc.id);
            }
        }
    }
    return orphans;
}

TEST_CASE("PostCompactCleanup enforceAlternation preserves toolResults when User merges ToolResult",
          "[compact][regression]") {
    // Construct the exact scenario that triggers the suspected bug:
    // [0] System
    // [1] User "some user text"        <- regular user message
    // [2] ToolResult(call_A)           <- ToolResult with effective role User
    //
    // After enforceAlternation, if [1] and [2] are merged by effective role,
    // the ToolResult's toolResults must NOT be lost.
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("some user text"));
    history.push_back(makeToolResultWithCallId("call_A", "Read", "file content here"));

    size_t sizeBefore = history.size();
    REQUIRE(sizeBefore == 3);

    int fixes = PostCompactCleanup::enforceAlternation(history);

    // After enforcement: total number of toolResult entries containing "call_A"
    // must be at least 1 — the merge must NOT discard toolResults.
    int foundToolResults = 0;
    for (const auto& msg : history) {
        for (const auto& tr : msg.toolResults) {
            if (tr.callId == "call_A") foundToolResults++;
        }
    }

    INFO("enforceAlternation merged " << fixes << " messages, history size went from "
         << sizeBefore << " to " << history.size());
    INFO("After enforceAlternation, toolResult call_A was found " << foundToolResults << " times");
    REQUIRE(foundToolResults >= 1);
}

TEST_CASE("PostCompactCleanup keeps toolResults immediately after assistant toolUse",
          "[compact][regression]") {
    // Simulate a realistic post-auto-compact history:
    // [0] System
    // [1] User: previous prompt
    // [2] Assistant: toolCalls call_A, call_B
    // [3] ToolResult: toolResults call_A, call_B
    // [4] User: next prompt (compact-preserved user content)
    //
    // After cleanup, every tool_use in [2] must have matching tool_result in [3].
    std::vector<Message> history;
    history.push_back(Message::system("system"));
    history.push_back(Message::user("Inspect the codebase"));
    history.push_back(makeAssistantWithToolCalls("Let me look at those files",
        {{"call_A", "Read"}, {"call_B", "Grep"}}));

    std::vector<ToolResponse> responses;
    ToolResponse rA; rA.callId = "call_A"; rA.toolName = "Read";
    rA.content = "file contents from Read";
    responses.push_back(rA);
    ToolResponse rB; rB.callId = "call_B"; rB.toolName = "Grep";
    rB.content = "grep match results";
    responses.push_back(rB);
    history.push_back(Message::toolResult(std::move(responses)));

    history.push_back(Message::user("next compact-preserved request"));

    // Snapshot toolCalls before cleanup
    std::vector<String> expectedToolUseIds;
    for (const auto& tc : history[2].toolCalls) {
        expectedToolUseIds.push_back(tc.id);
    }
    REQUIRE(expectedToolUseIds.size() == 2);

    PostCompactCleanup::cleanup(history);

    // Find the assistant with toolCalls (may have shifted index after cleanup)
    const Message* assistant = nullptr;
    const Message* next = nullptr;
    for (size_t i = 0; i + 1 < history.size(); ++i) {
        if (history[i].role == MessageRole::Assistant && !history[i].toolCalls.empty()) {
            assistant = &history[i];
            next = &history[i + 1];
            break;
        }
    }

    REQUIRE(assistant != nullptr);
    REQUIRE(next != nullptr);

    // Collect toolResult IDs from the next message
    std::unordered_set<String> resultIds;
    for (const auto& tr : next->toolResults) {
        resultIds.insert(tr.callId);
    }

    for (const auto& tc : assistant->toolCalls) {
        INFO("Assistant tool_use id=" << tc.id << " should have matching tool_result in next message");
        REQUIRE(resultIds.count(tc.id) == 1);
    }
}

TEST_CASE("PostCompactCleanup does not orphan assistant toolCalls after auto-compact simulation",
          "[compact][regression]") {
    // Simulate the state after auto-compact where early messages are summarized
    // and the last 5 messages are preserved. Specifically test that when a
    // regular User message sits immediately before a ToolResult in the preserved
    // window, the cleanup does not create orphaned tool_use blocks.
    //
    // Scenario (after auto-compact kept last 4):
    //   ... (summarized earlier messages including the assistant with toolCalls call_X)
    //   [boundary] Assistant: "I understand the summary..."
    //   [preserved] User: "some user followup"
    //   [preserved] Assistant: toolCalls call_A, call_B
    //   [preserved] ToolResult: toolResults call_A, call_B
    //   [preserved] User: "next prompt"
    //
    // After cleanup, call_A and call_B must NOT be orphaned.
    // Even if the User before the assistant has no toolCalls, the assistant→toolResult
    // pairing within the preserved window must remain intact.
    std::vector<Message> history;
    history.push_back(Message::system("system"));

    // Pre-compact boundary: summary of earlier conversation
    history.push_back(Message::user(
        "[Auto-compact: Summary of prior conversation]\n\nEarly discussion about code..."));
    history.push_back(Message::assistant(
        "I understand the conversation summary. I'll continue from here."));

    // Preserved messages from recent history
    history.push_back(Message::user("Now check the results"));
    history.push_back(makeAssistantWithToolCalls("Let me check those files",
        {{"call_C", "Read"}, {"call_D", "Grep"}, {"call_E", "Bash"}}));

    std::vector<ToolResponse> responses2;
    ToolResponse rC; rC.callId = "call_C"; rC.toolName = "Read";
    rC.content = "read result data";
    responses2.push_back(rC);
    ToolResponse rD; rD.callId = "call_D"; rD.toolName = "Grep";
    rD.content = "grep results data";
    responses2.push_back(rD);
    ToolResponse rE; rE.callId = "call_E"; rE.toolName = "Bash";
    rE.content = "bash output data";
    responses2.push_back(rE);
    history.push_back(Message::toolResult(std::move(responses2)));

    history.push_back(Message::user("next user turn"));

    REQUIRE(history[4].toolCalls.size() == 3);  // Assistant has 3 toolCalls

    PostCompactCleanup::cleanup(history);

    // Verify no orphaned tool_use IDs
    auto orphans = findOrphanedToolUses(history);

    INFO("Orphaned tool_use IDs after cleanup: " << orphans.size());
    for (const auto& id : orphans) {
        INFO("  orphaned: " << id);
    }
    REQUIRE(orphans.empty());
}
