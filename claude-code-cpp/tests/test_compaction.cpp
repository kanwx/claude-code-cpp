#include <catch2/catch_test_macros.hpp>
#include <claude/core/compact/MicroCompact.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/Types.hpp>

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
    REQUIRE(history[1].content == "small"); // unchanged
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

    // Step 1: MicroCompact by tool name
    int compacted = MicroCompact::applyByToolName(history, {"Read", "Bash"}, 2);
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
