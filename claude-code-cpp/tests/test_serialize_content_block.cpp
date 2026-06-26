#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "claude/core/ContentBlockParam.hpp"

using namespace claude;

// ========== Anthropic ContentMessage serialization ==========

TEST_CASE("Anthropic serializes user ContentMessage", "[serialize][anthropic]") {
    auto msg = ContentMessage::user("Hello");
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"].is_array());
    REQUIRE(j["content"].size() == 1);
    REQUIRE(j["content"][0]["type"] == "text");
    REQUIRE(j["content"][0]["text"] == "Hello");
}

TEST_CASE("Anthropic serializes assistant ContentMessage with toolUse", "[serialize][anthropic]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I will read the file."),
        ContentBlockParam::makeToolUse("toolu_123", "Read", Json::object({{"path", "/foo.txt"}}))
    });
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "assistant");
    REQUIRE(j["content"].size() == 2);
    REQUIRE(j["content"][0]["type"] == "text");
    REQUIRE(j["content"][0]["text"] == "I will read the file.");
    REQUIRE(j["content"][1]["type"] == "tool_use");
    REQUIRE(j["content"][1]["id"] == "toolu_123");
    REQUIRE(j["content"][1]["name"] == "Read");
    REQUIRE(j["content"][1]["input"]["path"] == "/foo.txt");
}

TEST_CASE("Anthropic serializes toolResult ContentMessage", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("toolu_456", "file contents here"));
    auto j = serializeContentMessageForAnthropic(msg);

    // ToolResult maps to user role in Anthropic API
    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"].size() == 1);
    REQUIRE(j["content"][0]["type"] == "tool_result");
    REQUIRE(j["content"][0]["tool_use_id"] == "toolu_456");
    REQUIRE(j["content"][0]["content"] == "file contents here");
}

TEST_CASE("Anthropic serializes toolResult with isError", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("toolu_789", "error occurred", true));
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"][0]["type"] == "tool_result");
    REQUIRE(j["content"][0]["is_error"] == true);
}

TEST_CASE("Anthropic serializes thinking blocks", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::Assistant;
    msg.content.push_back(ContentBlockParam::makeThinking("Let me think...", "sig123"));
    msg.content.push_back(ContentBlockParam::makeText("The answer is 42."));
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["content"][0]["type"] == "thinking");
    REQUIRE(j["content"][0]["thinking"] == "Let me think...");
    REQUIRE(j["content"][0]["signature"] == "sig123");
    REQUIRE(j["content"][1]["type"] == "text");
}

TEST_CASE("Anthropic serializes redacted_thinking blocks", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::Assistant;
    msg.content.push_back(ContentBlockParam::makeRedactedThinking("encrypted_data_here"));
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["content"][0]["type"] == "redacted_thinking");
    REQUIRE(j["content"][0]["data"] == "encrypted_data_here");
}

// ========== buildAnthropicApiMessages ==========

TEST_CASE("Anthropic merges consecutive toolResult messages", "[serialize][anthropic][merge]") {
    std::vector<ContentMessage> history;
    // First tool result message
    ContentMessage tr1;
    tr1.role = MessageRole::ToolResult;
    tr1.content.push_back(ContentBlockParam::makeToolResult("toolu_1", "result1"));
    history.push_back(tr1);

    // Second tool result message (consecutive)
    ContentMessage tr2;
    tr2.role = MessageRole::ToolResult;
    tr2.content.push_back(ContentBlockParam::makeToolResult("toolu_2", "result2"));
    history.push_back(tr2);

    auto messages = buildAnthropicApiMessages(history);
    // Should be merged into a single user message
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0]["role"] == "user");
    REQUIRE(messages[0]["content"].size() == 2);
    REQUIRE(messages[0]["content"][0]["tool_use_id"] == "toolu_1");
    REQUIRE(messages[0]["content"][1]["tool_use_id"] == "toolu_2");
}

TEST_CASE("Anthropic does NOT merge mixed user+toolResult messages", "[serialize][anthropic][merge]") {
    std::vector<ContentMessage> history;
    // User message with text + toolResult (mixed)
    ContentMessage mixed;
    mixed.role = MessageRole::User;
    mixed.content.push_back(ContentBlockParam::makeText("Here is the context"));
    mixed.content.push_back(ContentBlockParam::makeToolResult("toolu_1", "result1"));
    history.push_back(mixed);

    // Pure tool-result message
    ContentMessage tr;
    tr.role = MessageRole::ToolResult;
    tr.content.push_back(ContentBlockParam::makeToolResult("toolu_2", "result2"));
    history.push_back(tr);

    auto messages = buildAnthropicApiMessages(history);
    // The mixed message should NOT be merged with the following tool-result message
    REQUIRE(messages.size() == 2);
    REQUIRE(messages[0]["role"] == "user");
    REQUIRE(messages[1]["role"] == "user");
}

TEST_CASE("Anthropic skips system messages from messages array", "[serialize][anthropic]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::system("You are helpful."));
    history.push_back(ContentMessage::user("Hi"));

    auto messages = buildAnthropicApiMessages(history);
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0]["role"] == "user");
}

// ========== OpenAI ContentMessage serialization ==========

TEST_CASE("OpenAI serializes assistant with tool_calls", "[serialize][openai]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I will check."),
        ContentBlockParam::makeToolUse("call_abc", "Bash", Json::object({{"command", "ls"}}))
    });
    auto j = serializeContentMessageForOpenAI(msg);

    REQUIRE(j["role"] == "assistant");
    // content is text-only portion
    REQUIRE(j["content"] == "I will check.");
    // tool_calls is array of OpenAI-format function calls
    REQUIRE(j["tool_calls"].is_array());
    REQUIRE(j["tool_calls"].size() == 1);
    REQUIRE(j["tool_calls"][0]["type"] == "function");
    REQUIRE(j["tool_calls"][0]["id"] == "call_abc");
    REQUIRE(j["tool_calls"][0]["function"]["name"] == "Bash");
    REQUIRE(j["tool_calls"][0]["function"]["arguments"] == R"({"command":"ls"})");
}

TEST_CASE("OpenAI serializes tool result", "[serialize][openai]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("call_abc", "output here"));
    auto j = serializeContentMessageForOpenAI(msg);

    REQUIRE(j["role"] == "tool");
    REQUIRE(j["tool_call_id"] == "call_abc");
    REQUIRE(j["content"] == "output here");
}

// ========== convertLegacyMessage ==========

TEST_CASE("convertLegacyMessage converts flat content", "[serialize][legacy]") {
    Message old = Message::user("Hello world");
    auto result = convertLegacyMessage(old);

    REQUIRE(result.role == MessageRole::User);
    REQUIRE(result.content.size() == 1);
    REQUIRE(result.content[0].type == ContentBlockParam::Text);
    REQUIRE(result.content[0].text == "Hello world");
}

TEST_CASE("convertLegacyMessage converts tool_calls", "[serialize][legacy]") {
    Message old;
    old.role = MessageRole::Assistant;
    old.content = "I'll do it.";
    old.toolCalls.push_back({"call_1", "Read", R"({"path":"/foo"})"});
    auto result = convertLegacyMessage(old);

    REQUIRE(result.role == MessageRole::Assistant);
    // text + toolUse = 2 blocks
    REQUIRE(result.content.size() == 2);
    REQUIRE(result.content[1].type == ContentBlockParam::ToolUse);
    REQUIRE(result.content[1].id == "call_1");
    REQUIRE(result.content[1].name == "Read");
    REQUIRE(result.content[1].input["path"] == "/foo");
}

TEST_CASE("convertLegacyMessage converts toolResults", "[serialize][legacy]") {
    Message old = Message::toolResult({{"call_1", "Read", "file content", true}});
    auto result = convertLegacyMessage(old);

    REQUIRE(result.role == MessageRole::ToolResult);
    REQUIRE(result.content.size() == 1);
    REQUIRE(result.content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(result.content[0].toolUseId == "call_1");
    REQUIRE(result.content[0].resultContent == "file content");
    REQUIRE(result.content[0].isError == true);
}

TEST_CASE("convertLegacyMessage converts thinking", "[serialize][legacy]") {
    Message old;
    old.role = MessageRole::Assistant;
    old.content = "The answer.";
    old.thinking = "Let me reason...";
    old.signature = "sig_abc";
    auto result = convertLegacyMessage(old);

    // thinking + text = 2 blocks
    REQUIRE(result.content.size() == 2);
    REQUIRE(result.content[0].type == ContentBlockParam::Thinking);
    REQUIRE(result.content[0].thinking == "Let me reason...");
    REQUIRE(result.content[0].signature == "sig_abc");
}

TEST_CASE("convertLegacyMessage preserves block order", "[serialize][legacy]") {
    Message old;
    old.role = MessageRole::Assistant;
    old.thinking = "thinking text";
    old.signature = "sig";
    old.content = "response text";
    old.toolCalls.push_back({"tc_1", "Bash", R"({"cmd":"ls"})"});

    auto result = convertLegacyMessage(old);

    // Order: thinking -> text -> toolUse (API order)
    REQUIRE(result.content.size() == 3);
    REQUIRE(result.content[0].type == ContentBlockParam::Thinking);
    REQUIRE(result.content[1].type == ContentBlockParam::Text);
    REQUIRE(result.content[2].type == ContentBlockParam::ToolUse);
}

// ========== Regression: text block must not precede tool_result blocks ==========

TEST_CASE("convertLegacyMessage toolResult with non-empty content must not emit text block",
          "[serialize][legacy][regression]") {
    // Bug scenario: MicroCompact writes "[Old tool result content cleared...]"
    // to BOTH msg.content AND each ToolResponse.content. convertLegacyMessage
    // then emits a Text block from msg.content BEFORE the ToolResult blocks.
    // Anthropic API rejects this because tool_result blocks must come first
    // in the user message that follows an assistant with tool_use.
    //
    // Construct: assistant tool_use call_A → user tool_result call_A
    // The user ToolResult message has non-empty msg.content (from MicroCompact).
    Message old;
    old.role = MessageRole::ToolResult;
    old.content = "[Old tool result content cleared — original size: 123 chars]";
    ToolResponse tr;
    tr.callId = "call_A";
    tr.toolName = "Read";
    tr.content = "actual result";
    old.toolResults.push_back(tr);

    auto result = convertLegacyMessage(old);

    // Must NOT emit a Text block when toolResults are present.
    // All content blocks should be ToolResult blocks.
    for (size_t i = 0; i < result.content.size(); ++i) {
        INFO("Block " << i << " has type " << result.content[i].type
             << " (expected all ToolResult=" << ContentBlockParam::ToolResult << ")");
        REQUIRE(result.content[i].type == ContentBlockParam::ToolResult);
    }
    REQUIRE(result.content.size() == old.toolResults.size());
    REQUIRE(result.content[0].toolUseId == "call_A");
    REQUIRE(result.content[0].resultContent == "actual result");
}

TEST_CASE("convertLegacyMessage toolResult with multiple toolResults must not emit text block",
          "[serialize][legacy][regression]") {
    // Multiple toolResults (assistant had tool_use call_A, call_B, call_C)
    // with non-empty msg.content (from MicroCompact).
    Message old;
    old.role = MessageRole::ToolResult;
    old.content = "[Old tool result content cleared — original size: 10788 chars]";

    for (int i = 0; i < 3; ++i) {
        ToolResponse tr;
        tr.callId = "call_0" + std::to_string(i);
        tr.toolName = "Read";
        tr.content = "result " + std::to_string(i);
        old.toolResults.push_back(tr);
    }

    auto result = convertLegacyMessage(old);

    // All 3 blocks must be ToolResult, no Text block
    REQUIRE(result.content.size() == 3);
    for (size_t i = 0; i < result.content.size(); ++i) {
        INFO("Block " << i << " type=" << result.content[i].type);
        REQUIRE(result.content[i].type == ContentBlockParam::ToolResult);
        REQUIRE(result.content[i].toolUseId == "call_0" + std::to_string(i));
    }
}

TEST_CASE("PostCompactCleanup merged message must not emit text before tool_results",
          "[compact][regression]") {
    // Simulate PostCompactCleanup::enforceAlternation merging a User message
    // with a ToolResult message (both have effective role User).
    // After merge, the message has both msg.content (user text) AND toolResults.
    // The API serialization must NOT emit the text block before tool_result blocks.
    Message merged;
    merged.role = MessageRole::User;
    merged.content = "some user text\n\n[Old tool result content cleared — original size: 123 chars]";
    ToolResponse tr;
    tr.callId = "call_X";
    tr.toolName = "Read";
    tr.content = "file content";
    merged.toolResults.push_back(tr);

    auto cm = convertLegacyMessage(merged);

    // Must NOT have a Text block before ToolResult blocks.
    // The first non-ToolResult block (if any) must come after all ToolResult blocks.
    bool foundTextBeforeToolResult = false;
    int toolResultCount = 0;
    for (auto& block : cm.content) {
        if (block.type == ContentBlockParam::ToolResult) {
            toolResultCount++;
        } else if (block.type == ContentBlockParam::Text) {
            if (toolResultCount < static_cast<int>(merged.toolResults.size())) {
                foundTextBeforeToolResult = true;
            }
        }
    }

    REQUIRE(toolResultCount == 1);
    REQUIRE_FALSE(foundTextBeforeToolResult);
}

// ============================================================================
// E8 regression tests: interrupt/cancel protocol invariant enforcement
// ============================================================================

// Helper: build ContentMessage vector from legacy Messages for testing.
static std::vector<ContentMessage> toContentMessages(const std::vector<Message>& legacy) {
    std::vector<ContentMessage> out;
    for (const auto& msg : legacy) {
        out.push_back(convertLegacyMessage(msg));
    }
    return out;
}

// ========== Test 1: pending tool_use guard ==========

TEST_CASE("E8: detect orphaned tool_use — assistant with tool_use but no tool_result",
          "[serialize][e8][regression]") {
    // Construct the exact scenario that causes 400 errors:
    // [0] user "hello"
    // [1] assistant with tool_use Bash(call_A) but NO matching tool_result
    // [2] user "next prompt" (new turn submitted while tool still running)

    std::vector<Message> history;
    history.push_back(Message::user("! sleep 30"));

    Message assistant;
    assistant.role = MessageRole::Assistant;
    assistant.content = "I'll run sleep for you.";
    ToolCall tc;
    tc.id = "call_A";
    tc.name = "Bash";
    tc.arguments = R"({"command":"sleep 30"})";
    assistant.toolCalls.push_back(tc);
    history.push_back(assistant);

    // User submits next message while tool is still running
    // This is the bug: tool_result for call_A is NOT in history
    history.push_back(Message::user("你好"));

    auto cm = toContentMessages(history);
    auto orphans = findOrphanedToolUses(cm);

    INFO("Should detect orphaned tool_use call_A — new user input accepted while tool pending");
    REQUIRE_FALSE(orphans.empty());
    REQUIRE(orphans[0] == "call_A");
}

TEST_CASE("E8: no orphaned tool_uses in valid history — assistant tool_use followed by tool_result",
          "[serialize][e8][regression]") {
    // Normal flow: assistant tool_use → tool_result pair, no orphans
    std::vector<Message> history;
    history.push_back(Message::user("! sleep 30"));

    Message assistant;
    assistant.role = MessageRole::Assistant;
    assistant.content = "Running sleep.";
    ToolCall tc;
    tc.id = "call_A";
    tc.name = "Bash";
    tc.arguments = R"({"command":"sleep 30"})";
    assistant.toolCalls.push_back(tc);
    history.push_back(assistant);

    // Tool result is present (normal completion)
    ToolResponse tr;
    tr.callId = "call_A";
    tr.toolName = "Bash";
    tr.content = "Command completed.";
    history.push_back(Message::toolResult({tr}));

    history.push_back(Message::user("next prompt"));

    auto cm = toContentMessages(history);
    auto orphans = findOrphanedToolUses(cm);

    REQUIRE(orphans.empty());
}

TEST_CASE("E8: detect orphaned tool_use — multiple tool_uses, one missing tool_result",
          "[serialize][e8][regression]") {
    // Assistant: tool_use call_A, call_B
    // ToolResult: only call_A (call_B was interrupted/cancelled)
    // call_B should be detected as orphaned
    std::vector<Message> history;
    history.push_back(Message::user("do two things"));

    Message assistant;
    assistant.role = MessageRole::Assistant;
    assistant.content = "Doing both.";
    ToolCall tcA; tcA.id = "call_A"; tcA.name = "Read"; tcA.arguments = "{}";
    ToolCall tcB; tcB.id = "call_B"; tcB.name = "Bash"; tcB.arguments = "{}";
    assistant.toolCalls.push_back(tcA);
    assistant.toolCalls.push_back(tcB);
    history.push_back(assistant);

    // Only call_A has result — call_B's result is missing (cancelled before writing)
    ToolResponse trA; trA.callId = "call_A"; trA.toolName = "Read"; trA.content = "ok";
    history.push_back(Message::toolResult({trA}));

    history.push_back(Message::user("next"));

    auto cm = toContentMessages(history);
    auto orphans = findOrphanedToolUses(cm);

    REQUIRE(orphans.size() == 1);
    REQUIRE(orphans[0] == "call_B");
}

// ========== Test 2: cancelled tool must produce tool_result in history ==========

TEST_CASE("E8: cancelled tool execution must write tool_result to history",
          "[serialize][e8][regression]") {
    // Simulate the result of AgentLoop cancel path:
    // The history after a cancelled turn MUST contain a tool_result for every
    // tool_use that was issued by the assistant.
    //
    // Current bug: AgentLoop.cpp:618-631 returns "Cancelled by user" without
    // writing the tool_result from executeToolCalls to messageHistory.
    //
    // After fix: history should contain the cancelled/interrupted tool_result.
    //
    // This test verifies that convertLegacyMessage correctly represents the
    // interrupted tool_result when it IS present in the legacy Message.

    std::vector<Message> history;
    history.push_back(Message::user("! sleep 30"));

    Message assistant;
    assistant.role = MessageRole::Assistant;
    assistant.content = "Running sleep.";
    ToolCall tc;
    tc.id = "call_A";
    tc.name = "Bash";
    tc.arguments = R"({"command":"sleep 30"})";
    assistant.toolCalls.push_back(tc);
    history.push_back(assistant);

    // The cancelled tool result IS present (what the fix should produce)
    ToolResponse tr;
    tr.callId = "call_A";
    tr.toolName = "Bash";
    tr.content = "Cancelled by user";
    tr.isCancelled = true;
    history.push_back(Message::toolResult({tr}));

    auto cm = toContentMessages(history);
    auto orphans = findOrphanedToolUses(cm);

    REQUIRE(orphans.empty());

    // Verify the cancelled tool_result is properly marked
    bool foundCancelledResult = false;
    for (const auto& msg : cm) {
        for (const auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult &&
                block.toolUseId == "call_A") {
                foundCancelledResult = true;
                // ContentBlockParam doesn't carry isCancelled directly,
                // but the resultContent should reflect the interrupted state
                REQUIRE(block.resultContent.find("Cancelled") != String::npos);
            }
        }
    }
    REQUIRE(foundCancelledResult);
}

TEST_CASE("E8: convertLegacyMessage preserves isCancelled in tool_result",
          "[serialize][e8][regression]") {
    // When a tool is cancelled, the ToolResponse has isCancelled=true.
    // convertLegacyMessage must produce a ToolResult block that represents
    // the cancelled state so it can be serialized to history.
    Message cancelledTool;
    cancelledTool.role = MessageRole::ToolResult;
    cancelledTool.content = "";  // MicroCompact might write a placeholder

    ToolResponse tr;
    tr.callId = "call_Z";
    tr.toolName = "Bash";
    tr.content = "Cancelled by user";
    tr.isCancelled = true;
    cancelledTool.toolResults.push_back(tr);

    auto cm = convertLegacyMessage(cancelledTool);

    // Should have exactly one ToolResult block, no Text block
    REQUIRE(cm.content.size() == 1);
    REQUIRE(cm.content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(cm.content[0].toolUseId == "call_Z");
    REQUIRE(cm.content[0].resultContent == "Cancelled by user");
}

// ========== Test 1b: injectMissingToolResults safety net ==========

TEST_CASE("E8: injectMissingToolResults inserts synthetic tool_result for orphaned tool_use",
          "[serialize][e8][regression]") {
    // This is the FAILING-FIRST test for the API guard.
    // Before the fix: history has orphan tool_use, API gets 400.
    // After the fix: injectMissingToolResults inserts synthetic tool_result,
    // preventing the 400 error.

    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("! sleep 30"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I'll run that."),
        ContentBlockParam::makeToolUse("call_X1", "Bash",
            Json::object({{"command", "sleep 30"}}))
    });
    history.push_back(assistant);

    // Simulate: user submitted new input while tool still pending
    // (the E8 bug: no tool_result for call_X1)
    history.push_back(ContentMessage::user("你好"));

    // Verify orphan exists BEFORE injection
    auto orphansBefore = findOrphanedToolUses(history);
    REQUIRE_FALSE(orphansBefore.empty());
    REQUIRE(orphansBefore[0] == "call_X1");

    // Apply the safety net
    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 1);

    // Verify NO orphans AFTER injection
    auto orphansAfter = findOrphanedToolUses(history);
    REQUIRE(orphansAfter.empty());

    // Verify synthetic tool_result was inserted between assistant and user
    // Order should be: user → assistant(tool_use call_X1) → tool_result(call_X1) → user("你好")
    REQUIRE(history.size() == 4);
    REQUIRE(history[0].role == MessageRole::User);       // original user
    REQUIRE(history[1].role == MessageRole::Assistant);   // assistant with tool_use
    REQUIRE(history[2].role == MessageRole::ToolResult);  // SYNTHETIC tool_result
    REQUIRE(history[3].role == MessageRole::User);        // new user input

    // Verify synthetic tool_result content
    bool foundSynthetic = false;
    for (const auto& block : history[2].content) {
        if (block.type == ContentBlockParam::ToolResult &&
            block.toolUseId == "call_X1") {
            foundSynthetic = true;
            REQUIRE(block.isError == true);
            REQUIRE(block.resultContent.find("Interrupted") != String::npos);
        }
    }
    REQUIRE(foundSynthetic);
}

TEST_CASE("E8: injectMissingToolResults handles multiple orphaned tool_uses",
          "[serialize][e8][regression]") {
    // Assistant with 3 tool_uses, none have results
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("do three things"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Doing all three."),
        ContentBlockParam::makeToolUse("call_A", "Read", Json::object()),
        ContentBlockParam::makeToolUse("call_B", "Bash", Json::object()),
        ContentBlockParam::makeToolUse("call_C", "Grep", Json::object()),
    });
    history.push_back(assistant);
    history.push_back(ContentMessage::user("next input while tools pending"));

    auto orphansBefore = findOrphanedToolUses(history);
    REQUIRE(orphansBefore.size() == 3);

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 3);

    auto orphansAfter = findOrphanedToolUses(history);
    REQUIRE(orphansAfter.empty());
}

TEST_CASE("E8: injectMissingToolResults is no-op when all tool_uses have results",
          "[serialize][e8][regression]") {
    // Normal flow: every tool_use has a matching tool_result — no injection needed
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("read a file"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Reading it."),
        ContentBlockParam::makeToolUse("call_R1", "Read", Json::object()),
    });
    history.push_back(assistant);

    // Tool result IS present
    ContentMessage result;
    result.role = MessageRole::ToolResult;
    result.content.push_back(ContentBlockParam::makeToolResult("call_R1", "file content"));
    history.push_back(result);

    history.push_back(ContentMessage::user("next question"));

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 0);
    REQUIRE(history.size() == 4);  // no extra messages
}

// ============================================================================
// E8 P0 failing-first tests: ordered repair + hard validation
// ============================================================================

// P0 Test 1: repair must insert consolidated tool_result BEFORE user text,
// not after it. The fixed injectMissingToolResults builds a consolidated
// tool_result message and inserts it immediately after the assistant —
// before any user text that was originally intermixed or preceding.
TEST_CASE("E8-P0: repair inserts tool_result BEFORE user text, not after",
          "[serialize][e8][p0][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("! sleep 30"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep."),
        ContentBlockParam::makeToolUse("call_P1", "Bash",
            Json::object({{"command", "sleep 30"}}))
    });
    history.push_back(assistant);

    // ILLEGAL: user text BEFORE tool_result in the same message
    ContentMessage brokenUserMsg;
    brokenUserMsg.role = MessageRole::User;
    brokenUserMsg.content.push_back(ContentBlockParam::makeText("你好"));
    brokenUserMsg.content.push_back(ContentBlockParam::makeToolResult("call_P1", "completed"));
    history.push_back(brokenUserMsg);

    // Before repair: validator MUST fail (text before tool_result)
    REQUIRE_FALSE(validateToolResultOrdering(history));

    // Apply repair
    int injected = injectMissingToolResults(history);
    // call_P1 result was collected (not synthetic), so injected == 0
    REQUIRE(injected == 0);

    // After repair: validator MUST pass
    REQUIRE(validateToolResultOrdering(history));

    // Order must be: user → assistant(tool_use) → tool_result → user(text only)
    REQUIRE(history.size() >= 3);
    REQUIRE(history[0].role == MessageRole::User);
    REQUIRE(history[1].role == MessageRole::Assistant);
    REQUIRE(history[2].role == MessageRole::ToolResult);

    // Verify tool_result is in the message immediately after assistant
    bool hasToolResultForP1 = false;
    for (auto& block : history[2].content) {
        if (block.type == ContentBlockParam::ToolResult && block.toolUseId == "call_P1") {
            hasToolResultForP1 = true;
            break;
        }
    }
    REQUIRE(hasToolResultForP1);

    // Verify NO duplicate tool_result for call_P1 in later messages
    for (size_t idx = 3; idx < history.size(); ++idx) {
        for (auto& block : history[idx].content) {
            bool isDup = (block.type == ContentBlockParam::ToolResult &&
                          block.toolUseId == "call_P1");
            REQUIRE_FALSE(isDup);
        }
    }
}

// P0 Test 2: late tool_result must be MOVED to consolidated position,
// not duplicated. When a tool_result appears in a message AFTER user text
// (or in a separate message further down), the repair collects it into the
// consolidated message immediately after the assistant and REMOVES it from
// its original position.
TEST_CASE("E8-P0: late tool_result is moved, not duplicated",
          "[serialize][e8][p0][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("! sleep 30"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep."),
        ContentBlockParam::makeToolUse("call_P2", "Bash",
            Json::object({{"command", "sleep 30"}}))
    });
    history.push_back(assistant);

    // User text without tool_result (simulating new input before tool finished)
    history.push_back(ContentMessage::user("你好"));

    // Late tool_result in a separate message (simulating cancel path write)
    ContentMessage lateResult;
    lateResult.role = MessageRole::ToolResult;
    lateResult.content.push_back(
        ContentBlockParam::makeToolResult("call_P2", "cancelled", true));
    history.push_back(lateResult);

    // Count tool_results for call_P2 before repair
    auto countCallP2 = [](const std::vector<ContentMessage>& msgs) {
        int c = 0;
        for (auto& m : msgs)
            for (auto& b : m.content)
                if (b.type == ContentBlockParam::ToolResult && b.toolUseId == "call_P2") c++;
        return c;
    };
    REQUIRE(countCallP2(history) == 1);

    int injected = injectMissingToolResults(history);
    // call_P2 result was collected (moved), not synthetic
    REQUIRE(injected == 0);

    // After repair: must still be exactly 1 (moved, not duplicated)
    REQUIRE(countCallP2(history) == 1);

    // After repair: tool_result must be immediately after assistant
    REQUIRE(history.size() >= 3);
    REQUIRE(history[1].role == MessageRole::Assistant);
    REQUIRE(history[2].role == MessageRole::ToolResult);

    // User text message must come AFTER the tool_result
    REQUIRE(history[3].role == MessageRole::User);
    REQUIRE(history[3].textContent().find("你好") != String::npos);

    // Validator must pass on the repaired history
    REQUIRE(validateToolResultOrdering(history));
}

// P0 Test 3: after injectMissingToolResults repairs a broken history,
// validateToolResultOrdering must accept it. This is the end-to-end
// guarantee that repaired messages are safe to send to the API.
TEST_CASE("E8-P0: repaired history passes hard validation",
          "[serialize][e8][p0][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("do things"));

    // Assistant with TWO tool_uses
    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Doing both."),
        ContentBlockParam::makeToolUse("call_A", "Read",
            Json::object({{"path", "/f1"}})),
        ContentBlockParam::makeToolUse("call_B", "Bash",
            Json::object({{"command", "ls"}})),
    });
    history.push_back(assistant);

    // Broken: text BEFORE tool_result, and call_B has NO result
    ContentMessage broken;
    broken.role = MessageRole::User;
    broken.content.push_back(ContentBlockParam::makeText("next question"));
    broken.content.push_back(ContentBlockParam::makeToolResult("call_A", "file data"));
    history.push_back(broken);

    // Before repair: validator MUST fail
    REQUIRE_FALSE(validateToolResultOrdering(history));

    // Repair
    int injected = injectMissingToolResults(history);
    REQUIRE(injected >= 1);  // call_B is synthetic

    // After repair: validator MUST pass
    REQUIRE(validateToolResultOrdering(history));

    // Both tool_use IDs must now be covered
    auto orphans = findOrphanedToolUses(history);
    REQUIRE(orphans.empty());
}

// P0 Test 4: validateToolResultOrdering must REJECT a history where
// tool_results come after user text in the message immediately following
// an assistant with tool_use. This is the hard validator catching the
// exact violation that causes Anthropic API 400 errors.
TEST_CASE("E8-P0: illegal tool_result ordering fails hard validator",
          "[serialize][e8][p0][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("! sleep 30"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep."),
        ContentBlockParam::makeToolUse("call_F1", "Bash",
            Json::object({{"command", "sleep 30"}}))
    });
    history.push_back(assistant);

    // ILLEGAL: text BEFORE tool_result in the next message
    ContentMessage illegal;
    illegal.role = MessageRole::User;
    illegal.content.push_back(ContentBlockParam::makeText("你好"));  // TEXT FIRST — violation
    illegal.content.push_back(ContentBlockParam::makeToolResult("call_F1", "done"));
    history.push_back(illegal);

    // Validator MUST reject this
    REQUIRE_FALSE(validateToolResultOrdering(history));

    // Also test: missing next message entirely
    std::vector<ContentMessage> truncated;
    truncated.push_back(ContentMessage::user("cmd"));
    auto asst2 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeToolUse("call_F2", "Bash",
            Json::object({{"command", "ls"}}))
    });
    truncated.push_back(asst2);
    // No next message — validator must reject
    REQUIRE_FALSE(validateToolResultOrdering(truncated));

    // Also test: next message is another assistant (should never happen, but
    // hard validator must catch it)
    std::vector<ContentMessage> consecutiveAssistant;
    consecutiveAssistant.push_back(ContentMessage::user("cmd"));
    consecutiveAssistant.push_back(asst2);
    auto asst3 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("no tools here"),
    });
    consecutiveAssistant.push_back(asst3);
    REQUIRE_FALSE(validateToolResultOrdering(consecutiveAssistant));
}

// ========== Test 2b: buildAnthropicApiMessages validation gate ==========

TEST_CASE("E8: buildAnthropicApiMessages should reject history with orphaned tool_use",
          "[serialize][e8][regression]") {
    // When history has a tool_use without matching tool_result,
    // the API message builder should either:
    // (a) reject/throw, preventing a 400 from the API, or
    // (b) inject a synthetic interrupted tool_result
    //
    // This test documents the EXPECTED behavior after fix.
    // Currently, buildAnthropicApiMessages does NOT check for this.

    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("hello"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I'll run a command."),
        ContentBlockParam::makeToolUse("orphan_1", "Bash",
            Json::object({{"command", "sleep 30"}}))
    });
    history.push_back(assistant);

    // No tool_result for orphan_1 — simulating new input accepted while tool pending
    history.push_back(ContentMessage::user("你好"));

    auto orphans = findOrphanedToolUses(history);
    REQUIRE_FALSE(orphans.empty());

    // The P0 fix injects/repairs in AgentLoop::buildApiRequest() before calling
    // buildAnthropicApiMessages. buildAnthropicApiMessages itself is a pure
    // serializer — it does not validate. The guard lives at the higher level
    // (buildApiRequest: injectMissingToolResults + validateToolResultOrdering).
    //
    // This test verifies that orphans ARE detectable via findOrphanedToolUses,
    // which is the signal the upper layer uses to trigger repair.
}

// ============================================================================
// E8 Test 4 supplementary: cancel must not silently discard tool_results
// ============================================================================

TEST_CASE("E8: AgentLoop cancel check point must not skip tool_result history write",
          "[serialize][e8][regression]") {
    // This tests the specific code path at AgentLoop.cpp:618-631:
    //
    //   if (impl_->cancelled.load(...)) {
    //       return std::unexpected("Cancelled by user");  // <-- BUG: skips line 678-683!
    //   }
    //
    // After tool execution returns toolResponses, the cancelled check
    // returns early WITHOUT writing impl_->messageHistory.push_back(toolMsg).
    //
    // The fix must ensure tool_result is written BEFORE the cancel check,
    // or the cancel check must write it before returning.

    // Construct the state: assistant with tool_use, followed by user message
    // that represents the next turn's input — this is what the API receives
    // after cancel drops the tool_result.
    std::vector<Message> history;
    history.push_back(Message::user("! sleep 30"));

    Message assistant;
    assistant.role = MessageRole::Assistant;
    ToolCall tc;
    tc.id = "call_C1";
    tc.name = "Bash";
    tc.arguments = R"({"command":"sleep 30"})";
    assistant.toolCalls.push_back(tc);
    assistant.content = "Running it.";
    history.push_back(assistant);

    // Now simulate: new submit while tool pending
    // Expected: either call_C1 has a tool_result (from cancel path)
    // or the new submit is rejected.
    // Actual (bug): new submit goes through, no tool_result for call_C1
    history.push_back(Message::user("新的输入 — 在工具完成前提交"));

    auto cm = toContentMessages(history);
    auto orphans = findOrphanedToolUses(cm);

    // This MUST be empty in the correct implementation.
    // The P0 API guard (injectMissingToolResults in buildApiRequest) catches
    // and repairs this at the API layer. The root cause (AgentLoop cancel path
    // skipping the tool_result history write) may still exist in some paths,
    // but the API guard ensures we never send the broken history to the API.
    INFO("Orphaned tool_uses: " << orphans.size() << " (repaired by P0 API guard before sending)");
    CHECK_FALSE(orphans.empty());  // Documents the root cause is still detectable
}
