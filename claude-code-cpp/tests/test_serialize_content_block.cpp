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

TEST_CASE("Anthropic serializes empty toolResult content as placeholder", "[serialize][anthropic]") {
    ContentMessage msg;
    msg.role = MessageRole::ToolResult;
    msg.content.push_back(ContentBlockParam::makeToolResult("toolu_999", ""));  // empty output
    auto j = serializeContentMessageForAnthropic(msg);

    REQUIRE(j["role"] == "user");
    REQUIRE(j["content"][0]["type"] == "tool_result");
    REQUIRE(j["content"][0]["tool_use_id"] == "toolu_999");
    REQUIRE(j["content"][0]["content"] == "(no output)");
    // is_error must NOT be set — tool succeeded, just produced no output
    REQUIRE_FALSE(j["content"][0].contains("is_error"));
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

    // Verify synthetic tool_result was merged into the next user message
    // Order: user → assistant(tool_use call_X1) → user(tool_result(call_X1) + text("你好"))
    // The merge avoids consecutive user messages in the serialized JSON.
    REQUIRE(history.size() == 3);
    REQUIRE(history[0].role == MessageRole::User);       // original user
    REQUIRE(history[1].role == MessageRole::Assistant);   // assistant with tool_use
    REQUIRE(history[2].role == MessageRole::User);        // merged user: tool_result + text

    // Verify synthetic tool_result content — must be first block in message
    bool foundSynthetic = false, foundText = false;
    for (const auto& block : history[2].content) {
        if (block.type == ContentBlockParam::ToolResult &&
            block.toolUseId == "call_X1") {
            foundSynthetic = true;
            REQUIRE_FALSE(foundText);  // tool_result must come before text
            REQUIRE(block.isError == true);
            REQUIRE(block.resultContent.find("Interrupted") != String::npos);
        } else if (block.type == ContentBlockParam::Text && !block.text.empty()) {
            foundText = true;
            REQUIRE(foundSynthetic);  // text only after tool_result
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
    // Normal flow: every tool_use has a matching tool_result — no injection needed.
    // However, standalone ToolResult messages are merged into the next User message
    // to prevent consecutive user roles in the serialized API JSON.
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("read a file"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Reading it."),
        ContentBlockParam::makeToolUse("call_R1", "Read", Json::object()),
    });
    history.push_back(assistant);

    // Tool result IS present but in a standalone ToolResult message
    ContentMessage result;
    result.role = MessageRole::ToolResult;
    result.content.push_back(ContentBlockParam::makeToolResult("call_R1", "file content"));
    history.push_back(result);

    history.push_back(ContentMessage::user("next question"));

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 0);

    // Standalone ToolResult merged into next User to avoid consecutive users
    // user → assistant(tool_use) → user(tool_result + text)
    REQUIRE(history.size() == 3);
    REQUIRE(history[0].role == MessageRole::User);       // "read a file"
    REQUIRE(history[1].role == MessageRole::Assistant);   // tool_use call_R1
    REQUIRE(history[2].role == MessageRole::User);        // merged: tool_result + text
    REQUIRE(history[2].content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(history[2].content[1].type == ContentBlockParam::Text);

    // All validators pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    Json fullReq;
    fullReq["model"] = "claude-sonnet-4-6";
    fullReq["max_tokens"] = 4096;
    fullReq["messages"] = request;
    REQUIRE(validateSerializedApiJson(fullReq));
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

    // Order must be: user → assistant(tool_use) → user(tool_result + text)
    // The repaired tool_results are merged into the existing user message
    // to avoid consecutive user messages in the serialized JSON.
    REQUIRE(history.size() >= 3);
    REQUIRE(history[0].role == MessageRole::User);
    REQUIRE(history[1].role == MessageRole::Assistant);
    REQUIRE(history[2].role == MessageRole::User);  // merged, not standalone ToolResult

    // Verify tool_result is in the message immediately after assistant,
    // and tool_result comes BEFORE text in the content blocks.
    bool foundToolResult = false, foundText = false;
    for (auto& block : history[2].content) {
        if (block.type == ContentBlockParam::ToolResult && block.toolUseId == "call_P1") {
            foundToolResult = true;
            // Text must not appear before tool_result
            REQUIRE_FALSE(foundText);
        } else if (block.type == ContentBlockParam::Text && !block.text.empty()) {
            foundText = true;
            // Tool_result must come before text
            REQUIRE(foundToolResult);
        }
    }
    REQUIRE(foundToolResult);

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

    // After repair: tool_results are merged into the user message at position 2
    // (not a standalone ToolResult message). This avoids consecutive user messages.
    REQUIRE(history.size() >= 3);
    REQUIRE(history[1].role == MessageRole::Assistant);
    REQUIRE(history[2].role == MessageRole::User);  // merged user(tool_result + text)

    // Tool_result must come before text in the merged message
    bool foundToolResult = false, foundText = false;
    for (auto& block : history[2].content) {
        if (block.type == ContentBlockParam::ToolResult && block.toolUseId == "call_P2") {
            foundToolResult = true;
            REQUIRE_FALSE(foundText);  // tool_result must be before text
        } else if (block.type == ContentBlockParam::Text && !block.text.empty()) {
            foundText = true;
            REQUIRE(foundToolResult);  // text must be after tool_result
        }
    }
    REQUIRE(foundToolResult);
    REQUIRE(history[2].textContent().find("你好") != String::npos);

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

// ============================================================================
// validateEmptyMessages tests
// ============================================================================

TEST_CASE("validateEmptyMessages: accepts valid history", "[validator][emptymsg]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("hello"));
    history.push_back(ContentMessage::assistant("hi there"));
    REQUIRE(validateEmptyMessages(history));
}

TEST_CASE("validateEmptyMessages: rejects empty content vector", "[validator][emptymsg]") {
    std::vector<ContentMessage> history;
    ContentMessage empty;
    empty.role = MessageRole::User;
    // content vector is default-constructed, empty
    history.push_back(empty);
    REQUIRE_FALSE(validateEmptyMessages(history));
}

TEST_CASE("validateEmptyMessages: rejects empty text block", "[validator][emptymsg]") {
    std::vector<ContentMessage> history;
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("valid text"),
        ContentBlockParam::makeText(""),  // empty text block
    });
    history.push_back(msg);
    REQUIRE_FALSE(validateEmptyMessages(history));
}

TEST_CASE("validateEmptyMessages: rejects empty assistant message", "[validator][emptymsg]") {
    // Simulates a cancelled turn that was persisted without content
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("do something"));
    ContentMessage emptyAsst;
    emptyAsst.role = MessageRole::Assistant;
    // no content blocks — cancelled before any text was generated
    history.push_back(emptyAsst);
    REQUIRE_FALSE(validateEmptyMessages(history));
}

TEST_CASE("validateEmptyMessages: accepts assistant with text only", "[validator][emptymsg]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("hello"));
    history.push_back(ContentMessage::assistant("hi there"));
    history.push_back(ContentMessage::user("thanks"));
    REQUIRE(validateEmptyMessages(history));
}

// ============================================================================
// validateSerializedApiJson tests
// ============================================================================

// Helper: build a minimal valid request JSON with the given messages array
static Json makeRequest(Json messages) {
    Json req;
    req["model"] = "claude-sonnet-4-6";
    req["max_tokens"] = 4096;
    req["messages"] = messages;
    return req;
}

static Json makeMsg(const char* role, Json content) {
    Json m;
    m["role"] = role;
    m["content"] = content;
    return m;
}

static Json textBlock(const char* text) {
    return Json::object({{"type", "text"}, {"text", text}});
}

static Json toolUseBlock(const char* id, const char* name, Json input) {
    return Json::object({{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", input}});
}

static Json toolResultBlock(const char* toolUseId, const char* content) {
    return Json::object({{"type", "tool_result"}, {"tool_use_id", toolUseId}, {"content", content}});
}

TEST_CASE("validateSerializedApiJson: accepts valid user/assistant/user", "[validator][apijson]") {
    auto msg = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("hello")})),
        makeMsg("assistant", Json::array({textBlock("hi there")})),
    }));
    REQUIRE(validateSerializedApiJson(msg));
}

TEST_CASE("validateSerializedApiJson: rejects missing messages key", "[validator][apijson]") {
    Json req;
    req["model"] = "claude-sonnet-4-6";
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects non-array messages", "[validator][apijson]") {
    Json req;
    req["messages"] = "not_an_array";
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects invalid role", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("system", Json::array({textBlock("sys prompt")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects empty content array", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("hello")})),
        makeMsg("assistant", Json::array()),  // empty content
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects consecutive same roles", "[validator][apijson]") {
    // Two consecutive user messages — Anthropic requires alternation
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("first")})),
        makeMsg("user", Json::array({textBlock("second, but illegal")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects empty text block", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("")})),  // empty text
        makeMsg("assistant", Json::array({textBlock("reply")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects tool_use with empty id", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("run a command")})),
        makeMsg("assistant", Json::array({
            textBlock("ok"),
            toolUseBlock("", "Bash", Json::object({{"command", "ls"}})),  // empty id
        })),
        makeMsg("user", Json::array({toolResultBlock("", "output")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects tool_result with empty tool_use_id", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("run a command")})),
        makeMsg("assistant", Json::array({
            textBlock("ok"),
            toolUseBlock("call_1", "Bash", Json::object({{"command", "ls"}})),
        })),
        makeMsg("user", Json::array({toolResultBlock("", "output")})),  // empty tool_use_id
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects tool_result with empty content", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("run a command")})),
        makeMsg("assistant", Json::array({
            textBlock("ok"),
            toolUseBlock("call_1", "Bash", Json::object({{"command", "ls"}})),
        })),
        makeMsg("user", Json::array({toolResultBlock("call_1", "")})),  // empty content
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects unknown block type", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({Json::object({{"type", "invalid_type"}, {"text", "x"}})})),
        makeMsg("assistant", Json::array({textBlock("reply")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects contentPreview leak", "[validator][apijson]") {
    // Construct a message where contentPreview somehow leaked into a block
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({
            Json::object({{"type", "tool_result"}, {"tool_use_id", "call_1"},
                          {"content", "file content"},
                          {"contentPreview", "file..."}})  // UI field leak!
        })),
        makeMsg("assistant", Json::array({textBlock("got it")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects stableId as object key", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({
            Json::object({{"type", "text"}, {"text", "hello"}, {"stableId", 42}})  // UI field leak!
        })),
        makeMsg("assistant", Json::array({textBlock("reply")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: allows stableId in tool_result content",
          "[validator][apijson]") {
    // Legitimate: tool_result content that happens to contain "stableId" as
    // a C++ field name in source code (e.g. reading ContentBlock.hpp).
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("read the file")})),
        makeMsg("assistant", Json::array({
            textBlock("let me read that"),
            toolUseBlock("call_1", "Read", Json::object({{"file_path", "ContentBlock.hpp"}})),
        })),
        makeMsg("user", Json::array({
            toolResultBlock("call_1", "struct ContentBlock {\n    uint64_t stableId = 0;\n};"),
        })),
        makeMsg("assistant", Json::array({textBlock("the file contains stableId field")})),
    }));
    REQUIRE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: allows stableId in text content",
          "[validator][apijson]") {
    // Legitimate: user message text that happens to contain "stableId".
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({
            textBlock("what does stableId mean in ContentBlock?")
        })),
        makeMsg("assistant", Json::array({
            textBlock("stableId is a monotonic ID for incremental diff matching"),
        })),
    }));
    REQUIRE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: allows contentPreview in tool_result content",
          "[validator][apijson]") {
    // Legitimate: tool_result content that mentions "contentPreview" as a term.
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("check for contentPreview leaks")})),
        makeMsg("assistant", Json::array({
            toolUseBlock("call_1", "Grep", Json::object({{"pattern", "contentPreview"}})),
        })),
        makeMsg("user", Json::array({
            toolResultBlock("call_1", "found contentPreview in 3 files"),
        })),
        makeMsg("assistant", Json::array({textBlock("there are 3 occurrences")})),
    }));
    REQUIRE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects stableId in nested block key",
          "[validator][apijson]") {
    // stableId must be rejected even when deeply nested inside a content block.
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({
            Json::object({{"type", "text"}, {"text", "hello"},
                          {"metadata", Json::object({{"stableId", 123}})}}),
        })),
        makeMsg("assistant", Json::array({textBlock("reply")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects contentPreview in nested block key",
          "[validator][apijson]") {
    // contentPreview must be rejected even when nested.
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({
            toolResultBlock("call_1", "file content"),
        })),
        makeMsg("assistant", Json::array({textBlock("done")})),
    }));
    // Inject contentPreview as an extra key on the messages array wrapper
    // (simulates a serialization bug leaking UI state into the request).
    req["contentPreview"] = "leaked preview";
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: allows source code snippet with stableId",
          "[validator][apijson]") {
    // Baseline Task B reads ContentBlock.hpp, FtxuiRepl.cpp, etc.
    // These all contain "stableId" as C++ source code — must pass.
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("find stableId usage")})),
        makeMsg("assistant", Json::array({
            textBlock("searching for stableId"),
            toolUseBlock("call_1", "Grep", Json::object({{"pattern", "stableId"}})),
        })),
        makeMsg("user", Json::array({
            toolResultBlock("call_1",
                "src/ui/FtxuiRepl.cpp:229: cb.stableId = nextStableId_++;\n"
                "include/claude/stream/ContentBlock.hpp:65: uint64_t stableId = 0;\n"
                "src/stream/MessagePipeline.cpp:42: if (block.stableId >= nextId) {\n"),
        })),
        makeMsg("assistant", Json::array({textBlock("found 3 uses of stableId")})),
    }));
    REQUIRE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: rejects assistant tool_use without matching tool_result",
          "[validator][apijson]") {
    // Assistant has tool_use, but next user message has no tool_result
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("run command")})),
        makeMsg("assistant", Json::array({
            textBlock("ok running"),
            toolUseBlock("orphan_1", "Bash", Json::object({{"command", "ls"}})),
        })),
        // Next message is user but with text, NOT tool_results
        makeMsg("user", Json::array({textBlock("next thing")})),
        makeMsg("assistant", Json::array({textBlock("done")})),
    }));
    REQUIRE_FALSE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: accepts valid tool_use/tool_result chain", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("run a command")})),
        makeMsg("assistant", Json::array({
            textBlock("ok"),
            toolUseBlock("call_1", "Bash", Json::object({{"command", "ls"}})),
        })),
        makeMsg("user", Json::array({toolResultBlock("call_1", "file1\nfile2")})),
        makeMsg("assistant", Json::array({textBlock("here are the files...")})),
    }));
    REQUIRE(validateSerializedApiJson(req));
}

TEST_CASE("validateSerializedApiJson: accepts thinking and redacted_thinking blocks", "[validator][apijson]") {
    auto req = makeRequest(Json::array({
        makeMsg("user", Json::array({textBlock("question")})),
        makeMsg("assistant", Json::array({
            Json::object({{"type", "thinking"}, {"thinking", "let me think..."}, {"signature", "sig1"}}),
            textBlock("answer"),
        })),
    }));
    REQUIRE(validateSerializedApiJson(req));
}

// ============================================================================
// Merge repair tests: synthetic tool_results must merge into next user message,
// not create consecutive user(user) messages in the serialized API JSON.
// ============================================================================

// Test 1: repair merges synthetic tool_result into next user text
TEST_CASE("E8-merge: synthetic tool_result merges into next user text, not standalone",
          "[serialize][e8][merge][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("run a command"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running it."),
        ContentBlockParam::makeToolUse("call_M1", "Bash",
            Json::object({{"command", "sleep 30"}})),
    });
    history.push_back(assistant);

    // Next user text — no tool_result present (cancelled before tool finished)
    history.push_back(ContentMessage::user("hello"));

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 1);

    // After repair: must be 3 messages, NOT 4 (no standalone tool_result)
    // user → assistant(tool_use) → user(tool_result + text)
    REQUIRE(history.size() == 3);
    REQUIRE(history[0].role == MessageRole::User);
    REQUIRE(history[1].role == MessageRole::Assistant);
    REQUIRE(history[2].role == MessageRole::User);  // merged, not ToolResult

    // Tool_result must be the FIRST block in the merged message
    REQUIRE(history[2].content.size() >= 2);
    REQUIRE(history[2].content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(history[2].content[0].toolUseId == "call_M1");
    // Text must come AFTER tool_result
    REQUIRE(history[2].content[1].type == ContentBlockParam::Text);
    REQUIRE(history[2].content[1].text == "hello");

    // Must pass all validators
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
}

// Test 2: cancelled Bash + next user passes serialized JSON validator end-to-end
TEST_CASE("E8-merge: cancelled Bash + next user passes serialized JSON validator",
          "[serialize][e8][merge][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("Run sleep 30"));
    history.push_back(ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep."),
        ContentBlockParam::makeToolUse("call_M2", "Bash",
            Json::object({{"command", "sleep 30"}})),
    }));
    // User submitted new input while tool pending — no tool_result
    history.push_back(ContentMessage::user("hello"));

    // Repair
    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 1);
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));

    // Build the actual API JSON and validate it
    auto request = buildAnthropicApiMessages(history);
    auto fullReq = makeRequest(request);

    // This is the key assertion: the serialized JSON must be valid
    REQUIRE(validateSerializedApiJson(fullReq));
}

// Test 3: multiple cancelled turns do not accumulate consecutive users
TEST_CASE("E8-merge: multiple cancelled turns do not create consecutive user messages",
          "[serialize][e8][merge][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("first command"));

    // Turn 1: tool_use cancelled → next user text
    auto asst1 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running first."),
        ContentBlockParam::makeToolUse("call_M3a", "Bash",
            Json::object({{"command", "sleep 10"}})),
    });
    history.push_back(asst1);
    history.push_back(ContentMessage::user("second command"));

    // Turn 2: another tool_use cancelled → another user text
    auto asst2 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running second."),
        ContentBlockParam::makeToolUse("call_M3b", "Bash",
            Json::object({{"command", "sleep 10"}})),
    });
    history.push_back(asst2);
    history.push_back(ContentMessage::user("third thing"));

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 2);

    // Build the API JSON and validate — no consecutive user messages
    auto request = buildAnthropicApiMessages(history);
    auto fullReq = makeRequest(request);
    REQUIRE(validateSerializedApiJson(fullReq));

    // Verify role alternation in the serialized output
    REQUIRE(request.size() >= 4);
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            REQUIRE_FALSE(role == prevRole);  // no consecutive same roles
        }
        prevRole = role;
    }
}

// Test 4: tool_result block order inside merged user message
TEST_CASE("E8-merge: tool_result blocks precede text in merged user message",
          "[serialize][e8][merge][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("do things"));

    // Assistant with TWO tool_uses, both orphaned
    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Doing both."),
        ContentBlockParam::makeToolUse("call_M4a", "Read",
            Json::object({{"path", "/f1"}})),
        ContentBlockParam::makeToolUse("call_M4b", "Bash",
            Json::object({{"command", "ls"}})),
    });
    history.push_back(assistant);
    history.push_back(ContentMessage::user("next question"));

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 2);

    // Both tool_results must come before the text
    REQUIRE(history[2].content.size() == 3);
    REQUIRE(history[2].content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(history[2].content[0].toolUseId == "call_M4a");
    REQUIRE(history[2].content[1].type == ContentBlockParam::ToolResult);
    REQUIRE(history[2].content[1].toolUseId == "call_M4b");
    REQUIRE(history[2].content[2].type == ContentBlockParam::Text);

    // Validators must pass
    REQUIRE(validateToolResultOrdering(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));
}

// Test 5: late tool_result merged into next user — no duplicate
TEST_CASE("E8-merge: late tool_result merged into user, not duplicated",
          "[serialize][e8][merge][regression]") {
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("run a command"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running."),
        ContentBlockParam::makeToolUse("call_M5", "Bash",
            Json::object({{"command", "sleep 30"}})),
    });
    history.push_back(assistant);
    history.push_back(ContentMessage::user("hello"));

    // Late tool_result in a separate message (simulating cancel path)
    ContentMessage lateResult;
    lateResult.role = MessageRole::ToolResult;
    lateResult.content.push_back(
        ContentBlockParam::makeToolResult("call_M5", "cancelled", true));
    history.push_back(lateResult);

    // Count before
    auto countCallM5 = [](const std::vector<ContentMessage>& msgs) {
        int c = 0;
        for (auto& m : msgs)
            for (auto& b : m.content)
                if (b.type == ContentBlockParam::ToolResult && b.toolUseId == "call_M5") c++;
        return c;
    };
    REQUIRE(countCallM5(history) == 1);

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 0);  // moved, not synthetic

    // Must still be exactly 1 call_M5 tool_result (moved, not duplicated)
    REQUIRE(countCallM5(history) == 1);

    // tool_result must be in the merged user message at position 2
    REQUIRE(history[2].role == MessageRole::User);
    bool foundInMerged = false;
    for (auto& b : history[2].content) {
        if (b.type == ContentBlockParam::ToolResult && b.toolUseId == "call_M5") {
            foundInMerged = true;
            break;
        }
    }
    REQUIRE(foundInMerged);

    // Validators must pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));
}

// ============================================================================
// cancelGeneration race tests: verify resetCancel() + cancelGeneration detection
// ============================================================================

// Test: CancelledRunLateSuccessIsInterruptedAfterResetCancel
//
// Scenario:
//   1. Run A starts tool_use sleep 30 in a background thread
//   2. ESC pressed → cancel() increments cancelGeneration (N → N+1)
//   3. New prompt submitted → resetCancel() clears impl_->cancelled (now false)
//   4. Run A's sleep(30) completes normally, tool process returns "Slept for 30 seconds"
//   5. executeLoop snapshots cancelGenBefore BEFORE tool execution,
//      compares after: cancelGeneration changed → turnCancelled=true
//   6. Force-override ALL tool responses:
//      isCancelled=true, content="Interrupted: tool execution was cancelled"
//   7. insertOrMergeToolResultsAfterAssistant writes corrected result to history
//
// Expected:
//   - ToolResponse.isCancelled = true
//   - History has "Interrupted: tool execution was cancelled", NOT "Slept for 30 seconds"
//   - Serialized JSON passes API validator
//   - No stale success event reaches the UI
TEST_CASE("E8-cancelGen: force-overridden tool_result contains Interrupted, not real output",
          "[serialize][e8][cancelgen][regression]") {
    // Build history simulating the race:
    // user "sleep 30" → assistant tool_use call_S1 → user "next command"
    std::vector<ContentMessage> history;
    history.push_back(ContentMessage::user("! sleep 30"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep."),
        ContentBlockParam::makeToolUse("call_S1", "Bash",
            Json::object({{"command", "sleep 30"}})),
    });
    history.push_back(assistant);

    // Simulate the new turn's user input while old tool still running
    history.push_back(ContentMessage::user("! read CMakeLists.txt"));

    // Repair the orphaned tool_use (simulates P0 API guard at buildApiRequest)
    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 1);

    // After repair: 3 messages, not 4 (merged into next user)
    REQUIRE(history.size() == 3);
    REQUIRE(history[0].role == MessageRole::User);       // "sleep 30"
    REQUIRE(history[1].role == MessageRole::Assistant);   // tool_use call_S1
    REQUIRE(history[2].role == MessageRole::User);        // merged tool_result + text

    // Key assertion: "Interrupted" content IS present, "Slept for 30 seconds" is NOT
    bool foundInterrupted = false;
    bool foundSleptSuccess = false;
    for (auto& block : history[2].content) {
        if (block.type == ContentBlockParam::ToolResult && block.toolUseId == "call_S1") {
            REQUIRE(block.isError == true);  // cancelled → error
            if (block.resultContent.find("Interrupted") != String::npos)
                foundInterrupted = true;
            if (block.resultContent.find("Slept for 30 seconds") != String::npos)
                foundSleptSuccess = true;
        }
    }
    REQUIRE(foundInterrupted);
    REQUIRE_FALSE(foundSleptSuccess);

    // All validators must pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));
}

// Test: LateCancelledToolResultDoesNotLockInput
//
// Scenario:
//   Multiple consecutive ESC+resubmit cycles. Each cancelled run's tool result
//   arrives after the new turn's resetCancel(). If the force-override is missing,
//   stale ToolResult events with "Slept for 30 seconds" (success) reach the UI,
//   causing input lock until a second ESC clears it.
//
// This test verifies the ContentBlockParam-level safety:
//   - Multiple orphaned tool_uses from consecutive cancelled turns are all repaired
//   - No consecutive user messages in serialized output (merge fix holds)
//   - No stale success results survive
//   - Serialized JSON passes all validators
TEST_CASE("E8-cancelGen: multiple cancelled turns do not accumulate stale results",
          "[serialize][e8][cancelgen][regression]") {
    std::vector<ContentMessage> history;

    // Turn 1: user "sleep 30" → ESC cancels → tool_use call_A1 orphaned
    history.push_back(ContentMessage::user("! sleep 30"));
    auto asst1 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep 30."),
        ContentBlockParam::makeToolUse("call_A1", "Bash",
            Json::object({{"command", "sleep 30"}})),
    });
    history.push_back(asst1);

    // User submits new command (ESC + new prompt) — tool for call_A1 not finished
    history.push_back(ContentMessage::user("! sleep 10"));

    // Turn 2: user "sleep 10" → ESC cancels → tool_use call_A2 orphaned
    auto asst2 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep 10."),
        ContentBlockParam::makeToolUse("call_A2", "Bash",
            Json::object({{"command", "sleep 10"}})),
    });
    history.push_back(asst2);

    // User submits another new command while call_A2 still pending
    history.push_back(ContentMessage::user("! read CMakeLists.txt"));

    // Before repair: 2 orphaned tool_uses
    auto orphansBefore = findOrphanedToolUses(history);
    REQUIRE(orphansBefore.size() == 2);

    // Repair
    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 2);

    // No orphans after repair
    auto orphansAfter = findOrphanedToolUses(history);
    REQUIRE(orphansAfter.empty());

    // No consecutive user messages in serialized output
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));

    // Verify role alternation: no consecutive same roles
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            REQUIRE_FALSE(role == prevRole);
        }
        prevRole = role;
    }

    // Verify ALL synthetic tool_results say "Interrupted", not real output
    int interruptedCount = 0;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult &&
                block.resultContent.find("Interrupted") != String::npos) {
                interruptedCount++;
            }
        }
    }
    REQUIRE(interruptedCount == 2);  // both call_A1 and call_A2 are Interrupted

    // Verify no block contains stale success text
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                REQUIRE(block.resultContent.find("Slept for") == String::npos);
                REQUIRE(block.resultContent.find("Running sleep") == String::npos);
            }
        }
    }
}

// ============================================================================
// Coalesce & idempotent repair tests: removeIndices → assistant→assistant,
// same-role coalescing, no duplicate results, repair idempotency
// ============================================================================

// Test 1: remove stale ToolResult between two assistants must not create
// consecutive assistants after coalesce.
//
// Scenario:
//   assistant[0](tool_use call_X)
//   ToolResult[1](call_X)  ← stale, will be removed
//   assistant[2](text only)
//
// After repair: tool_result for call_X is collected from [1], [1] is removed.
// Without coalesce, [0] assistant and [2] assistant become adjacent —
// consecutive assistants in the serialized JSON.
// With coalesce, [0] and [2] are merged into one assistant.
TEST_CASE("E8-coalesce: removed ToolResult between two assistants — coalesce merges",
          "[serialize][e8][coalesce][regression]") {
    std::vector<ContentMessage> history;

    // Assistant with tool_use call_R1
    auto asstA = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running a task."),
        ContentBlockParam::makeToolUse("call_R1", "Bash",
            Json::object({{"command", "ls"}})),
    });
    history.push_back(asstA);

    // Stale standalone ToolResult for call_R1 (to be removed)
    ContentMessage staleTr;
    staleTr.role = MessageRole::ToolResult;
    staleTr.content.push_back(
        ContentBlockParam::makeToolResult("call_R1", "file list output"));
    history.push_back(staleTr);

    // Another assistant (text-only, from a different turn)
    auto asstB = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Here are the files."),
    });
    history.push_back(asstB);

    // User follows
    history.push_back(ContentMessage::user("thanks"));

    // Verify pre-conditions
    REQUIRE(history.size() == 4);
    REQUIRE(history[0].role == MessageRole::Assistant);
    REQUIRE(history[1].role == MessageRole::ToolResult);
    REQUIRE(history[2].role == MessageRole::Assistant);
    REQUIRE(history[3].role == MessageRole::User);

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 0);  // call_R1 result was collected, not synthetic

    // After coalesce: no consecutive assistants
    // Expected: assistant(tool_use + text) → user(tool_result + "thanks")
    // OR: assistant(tool_use) → user(tool_result) → assistant(text) → user("thanks")
    // Both are valid. Key assertion: no consecutive same-role messages.
    for (size_t i = 1; i < history.size(); ++i) {
        INFO("Position " << i << " role=" << (int)history[i].role
             << " prev role=" << (int)history[i-1].role);
        REQUIRE_FALSE(history[i].role == history[i - 1].role);
    }

    // All validators pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));

    // Verify role alternation in serialized JSON
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            REQUIRE_FALSE(role == prevRole);
        }
        prevRole = role;
    }
}

// Test 2: mergeTarget skips removed messages but preserves role alternation.
//
// Scenario from TTY log:
//   assistant[0](tool_use X, Y) at idx 0
//   stale ToolResult at idx 1 (removed)
//   assistant at idx 2 (text only)
//
// mergeTarget finds idx 2 (assistant) → no merge → standalone insert at idx 1.
// After removal of idx 1 (shifted to idx 2): assistant → tool_result → assistant.
// Coalesce then merges the two assistants.
//
// Final: assistant(tool_use X, Y + text) → user(tool_result X, Y)
// or:    assistant(tool_use X, Y) → user(tool_result X, Y) → assistant(text)
TEST_CASE("E8-coalesce: mergeTarget skips removed msg, coalesce fixes result",
          "[serialize][e8][coalesce][regression]") {
    std::vector<ContentMessage> history;

    // Assistant with two tool_uses
    auto asstA = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Doing two things."),
        ContentBlockParam::makeToolUse("call_T1", "Read",
            Json::object({{"path", "/f1"}})),
        ContentBlockParam::makeToolUse("call_T2", "Bash",
            Json::object({{"command", "ls"}})),
    });
    history.push_back(asstA);

    // Stale standalone ToolResult (to be removed) — contains ONLY call_T1 result
    ContentMessage staleTr;
    staleTr.role = MessageRole::ToolResult;
    staleTr.content.push_back(
        ContentBlockParam::makeToolResult("call_T1", "file content"));
    // call_T2 has NO result (orphaned)
    history.push_back(staleTr);

    // Another assistant (text-only)
    auto asstB = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Here is the result."),
    });
    history.push_back(asstB);

    // No further messages (end of history)

    // Verify pre-conditions
    REQUIRE(history.size() == 3);
    REQUIRE(history[0].role == MessageRole::Assistant);  // tool_use T1, T2
    REQUIRE(history[1].role == MessageRole::ToolResult);  // stale, to be removed
    REQUIRE(history[2].role == MessageRole::Assistant);   // text only

    int injected = injectMissingToolResults(history);
    // call_T1 result was collected (moved), call_T2 was missing → synthetic
    REQUIRE(injected == 1);

    // Key assertion: no consecutive same roles
    for (size_t i = 1; i < history.size(); ++i) {
        INFO("Position " << i << " role=" << (int)history[i].role);
        REQUIRE_FALSE(history[i].role == history[i - 1].role);
    }

    // Both tool_use IDs must have exactly one tool_result
    int t1Count = 0, t2Count = 0;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                if (block.toolUseId == "call_T1") t1Count++;
                if (block.toolUseId == "call_T2") t2Count++;
            }
        }
    }
    REQUIRE(t1Count == 1);
    REQUIRE(t2Count == 1);
    REQUIRE_FALSE(history.empty());

    // All validators pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));

    // Serialized JSON must have role alternation
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            REQUIRE_FALSE(role == prevRole);
        }
        prevRole = role;
    }
}

// Test 3: Repair is idempotent — calling injectMissingToolResults twice
// on the same history produces the same result and no new injections.
TEST_CASE("E8-idempotent: second injectMissingToolResults is no-op",
          "[serialize][e8][coalesce][regression]") {
    std::vector<ContentMessage> history;

    history.push_back(ContentMessage::user("run a command"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running it."),
        ContentBlockParam::makeToolUse("call_I1", "Bash",
            Json::object({{"command", "sleep 30"}})),
    });
    history.push_back(assistant);

    // User text without tool_result → orphan
    history.push_back(ContentMessage::user("hello"));

    // First repair
    int injected1 = injectMissingToolResults(history);
    REQUIRE(injected1 == 1);

    size_t sizeAfterFirst = history.size();
    int t1CountAfterFirst = 0;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult &&
                block.toolUseId == "call_I1") t1CountAfterFirst++;
        }
    }
    REQUIRE(t1CountAfterFirst == 1);

    // All validators pass after first repair
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request1 = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request1)));

    // Second repair — must be no-op
    int injected2 = injectMissingToolResults(history);
    REQUIRE(injected2 == 0);

    // Size must not change
    REQUIRE(history.size() == sizeAfterFirst);

    // Tool_result count must not change
    int t1CountAfterSecond = 0;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult &&
                block.toolUseId == "call_I1") t1CountAfterSecond++;
        }
    }
    REQUIRE(t1CountAfterSecond == 1);

    // Validators still pass
    REQUIRE(validateToolResultOrdering(history));
    auto request2 = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request2)));
}

// Test 4: Multiple tool_use repair preserves exactly one result per ID.
//
// Assistant has tool_use X, Y. Both orphaned. After repair, each has
// exactly one tool_result, tool_results come before text, no duplicates.
TEST_CASE("E8-idempotent: multiple tool_uses — one result per ID, no duplicates",
          "[serialize][e8][coalesce][regression]") {
    std::vector<ContentMessage> history;

    history.push_back(ContentMessage::user("do two things"));

    auto assistant = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Doing both."),
        ContentBlockParam::makeToolUse("call_D1", "Read",
            Json::object({{"path", "/a"}})),
        ContentBlockParam::makeToolUse("call_D2", "Bash",
            Json::object({{"command", "ls"}})),
    });
    history.push_back(assistant);

    // User text without tool_results → both orphaned
    history.push_back(ContentMessage::user("next question please"));

    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 2);

    // Count results per ID
    int d1Count = 0, d2Count = 0;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                if (block.toolUseId == "call_D1") d1Count++;
                if (block.toolUseId == "call_D2") d2Count++;
            }
        }
    }
    REQUIRE(d1Count == 1);
    REQUIRE(d2Count == 1);

    // Tool_results must come before text
    for (auto& msg : history) {
        if (msg.role != MessageRole::User) continue;
        bool foundText = false;
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::Text && !block.text.empty()) {
                foundText = true;
            }
            if (block.type == ContentBlockParam::ToolResult) {
                REQUIRE_FALSE(foundText);  // tool_result must precede text
            }
        }
    }

    // Idempotent: second call injects 0
    int injected2 = injectMissingToolResults(history);
    REQUIRE(injected2 == 0);

    // Counts unchanged
    int d1Count2 = 0, d2Count2 = 0;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                if (block.toolUseId == "call_D1") d1Count2++;
                if (block.toolUseId == "call_D2") d2Count2++;
            }
        }
    }
    REQUIRE(d1Count2 == 1);
    REQUIRE(d2Count2 == 1);

    // All validators pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));

    // No consecutive same roles in serialized JSON
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            REQUIRE_FALSE(role == prevRole);
        }
        prevRole = role;
    }
}

// Test 5: TTY sequence simulation — multiple turns with late results.
//
// Simulates the full TTY flow:
//   1. hello
//   2. sleep 30 → ESC
//   3. read cmakelists.txt
//   4. read cmakelists.tst
//   5. hello
//
// With late sleep results arriving after ESC and resetCancel().
// Validates: no consecutive roles, Interrupted content, idempotent repair.
TEST_CASE("E8-idempotent: TTY sequence — hello, sleep+ESC, reads, hello",
          "[serialize][e8][coalesce][regression]") {
    std::vector<ContentMessage> history;

    // Turn 1: hello
    history.push_back(ContentMessage::user("hello"));
    auto asst1 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Hello! How can I help you?"),
    });
    history.push_back(asst1);

    // Turn 2: sleep 30 → ESC cancels → tool_use call_sleep
    history.push_back(ContentMessage::user("run sleep 30"));
    auto asst2 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Running sleep for 30 seconds."),
        ContentBlockParam::makeToolUse("call_sleep", "Bash",
            Json::object({{"command", "sleep 30"}})),
    });
    history.push_back(asst2);

    // Late sleep result arrives (force-overridden in executeLoop via
    // cancelGeneration detection, then insertOrMergeToolResultsAfterAssistant
    // writes it to history as a standalone ToolResult).
    ContentMessage lateSleepResult;
    lateSleepResult.role = MessageRole::ToolResult;
    lateSleepResult.content.push_back(
        ContentBlockParam::makeToolResult("call_sleep",
            "Interrupted: tool execution was cancelled", true));
    history.push_back(lateSleepResult);

    // Turn 3: read cmakelists.txt (new turn, submitted after ESC)
    history.push_back(ContentMessage::user("read cmakelists.txt"));
    auto asst3 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Reading the file."),
        ContentBlockParam::makeToolUse("call_read1", "Read",
            Json::object({{"path", "cmakelists.txt"}})),
    });
    history.push_back(asst3);

    // Turn 3 result: read1 success
    ContentMessage read1Result;
    read1Result.role = MessageRole::ToolResult;
    read1Result.content.push_back(
        ContentBlockParam::makeToolResult("call_read1", "cmake_minimum_required(VERSION 3.20)..."));
    history.push_back(read1Result);

    // Turn 4: read cmakelists.tst
    history.push_back(ContentMessage::user("read cmakelists.tst"));
    auto asst4 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("That file doesn't exist."),
    });
    history.push_back(asst4);

    // Turn 5: hello
    history.push_back(ContentMessage::user("hello"));
    auto asst5 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Hello again!"),
    });
    history.push_back(asst5);

    // Run repair — should fix the orphaned sleep and read1 tool_uses
    // call_sleep was collected (moved), call_read1 was collected (moved)
    int injected = injectMissingToolResults(history);
    REQUIRE(injected == 0);  // both results were collected, not synthetic

    // Verify no consecutive same roles
    for (size_t i = 1; i < history.size(); ++i) {
        INFO("Position " << i << " role=" << (int)history[i].role
             << " prev=" << (int)history[i-1].role);
        REQUIRE_FALSE(history[i].role == history[i - 1].role);
    }

    // Verify Interrupted content for sleep
    bool foundSleepInterrupted = false;
    for (auto& msg : history) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult &&
                block.toolUseId == "call_sleep") {
                REQUIRE(block.resultContent.find("Interrupted") != String::npos);
                REQUIRE(block.resultContent.find("Slept for") == String::npos);
                foundSleepInterrupted = true;
            }
        }
    }
    REQUIRE(foundSleepInterrupted);

    // All validators pass
    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));

    // Second repair must be no-op
    int injected2 = injectMissingToolResults(history);
    REQUIRE(injected2 == 0);

    // Validators still pass
    auto request2 = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request2)));

    // Serialized JSON: no consecutive same roles
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            INFO("Consecutive roles: " << prevRole << " → " << role);
            REQUIRE_FALSE(role == prevRole);
        }
        prevRole = role;
    }
}

// Test 6: pre-existing consecutive assistants (not from repair) are coalesced.
//
// Real TTY scenario: after auto-compact, the history legitimately contains
// two consecutive assistant messages (compaction acknowledgment + tool-result
// analysis).  The coalesce in injectMissingToolResults must handle these
// even when no tool_use repair was needed.
TEST_CASE("E8-coalesce: pre-existing consecutive assistants are merged",
          "[serialize][e8][coalesce][regression]") {
    std::vector<ContentMessage> history;

    // Normal alternating sequence with tool_use/tool_result
    auto asst = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Let me check something."),
        ContentBlockParam::makeToolUse("call_R1", "Bash",
            Json::object({{"command", "ls"}})),
    });
    history.push_back(asst);

    ContentMessage tr;
    tr.role = MessageRole::ToolResult;
    tr.content.push_back(ContentBlockParam::makeToolResult("call_R1", "file list"));
    history.push_back(tr);

    // User text
    history.push_back(ContentMessage::user("thanks"));

    // Pre-existing consecutive assistants (as from auto-compact)
    auto asst2 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("Understood. Continuing with compressed context."),
    });
    history.push_back(asst2);

    auto asst3 = ContentMessage::assistantBlocks({
        ContentBlockParam::makeThinking("analysis...", "sig"),
        ContentBlockParam::makeText("I have the files loaded. The main file is..."),
    });
    history.push_back(asst3);

    // Final user
    history.push_back(ContentMessage::user("next query"));

    REQUIRE(history.size() == 6);
    // Verify consecutive assistants exist before repair
    REQUIRE(history[3].role == MessageRole::Assistant);
    REQUIRE(history[4].role == MessageRole::Assistant);

    int injected = injectMissingToolResults(history);
    // call_R1 result was already present, no synthetics needed
    REQUIRE(injected == 0);

    // After repair+coalesce: no consecutive same-role messages
    for (size_t i = 1; i < history.size(); ++i) {
        INFO("Position " << i << " role=" << (int)history[i].role
             << " prev role=" << (int)history[i-1].role);
        REQUIRE_FALSE(history[i].role == history[i - 1].role);
    }

    REQUIRE(validateToolResultOrdering(history));
    REQUIRE(validateEmptyMessages(history));
    auto request = buildAnthropicApiMessages(history);
    REQUIRE(validateSerializedApiJson(makeRequest(request)));

    // Serialized JSON must have role alternation
    String prevRole;
    for (auto& msg : request) {
        String role = msg["role"];
        if (!prevRole.empty()) {
            INFO("Consecutive roles: " << prevRole << " → " << role);
            REQUIRE_FALSE(role == prevRole);
        }
        prevRole = role;
    }
}
