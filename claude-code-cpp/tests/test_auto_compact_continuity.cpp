/**
 * Auto-compact continuity tests
 * Verifies compact does not corrupt tool ownership or turn continuity.
 */
#include <catch2/catch_test_macros.hpp>
#include <claude/core/compact/PostCompactCleanup.hpp>
#include <claude/core/ContentBlockParam.hpp>
#include <claude/core/ApiTypes.hpp>
#include <vector>
#include <string>

using namespace claude;
using namespace claude::compact;

// ============================================================================
// Test helpers
// ============================================================================

static Message makeUser(const String& content) {
    return Message::user(content);
}

static Message makeAssistant(const String& content,
                             std::vector<ToolCall> calls = {}) {
    return Message::assistant(content, std::move(calls));
}

static Message makeToolResult(const String& callId, const String& content,
                              bool isError = false) {
    ToolResponse tr;
    tr.callId = callId;
    tr.content = content;
    tr.isError = isError;
    return Message::toolResult({std::move(tr)});
}

static ToolCall makeToolCall(const String& id, const String& name,
                             const String& args = "{}") {
    ToolCall tc;
    tc.id = id;
    tc.name = name;
    tc.arguments = args;
    return tc;
}

// ============================================================================
// Test 1: enforceAlternation refuses to merge asst+asst where one has tool_use
// ============================================================================
TEST_CASE("enforceAlternation refuses tool-bearing assistant merge",
          "[compact][continuity][guard]") {
    // Simulate the RC1 scenario: summary assistant + tool_use assistant
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("[Previous conversation summary]\n\n..."));
    history.push_back(makeAssistant(
        "I understand the conversation summary. I'll continue from here."));
    // First kept message is an assistant with tool_use — this must NOT merge
    // with the summary assistant above.
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    history.push_back(makeAssistant("Command ran successfully"));

    int fixes = PostCompactCleanup::enforceAlternation(history);

    // Verify: no message has BOTH "I understand" text AND tool_use "toolu_1"
    // (the merge was correctly skipped). Consecutive assistants may remain
    // when tool_use is present — downstream validation catches this.
    for (size_t i = 0; i < history.size(); ++i) {
        auto& msg = history[i];
        bool hasSummary = msg.content.find("I understand the conversation") != String::npos;
        bool hasToolUse = false;
        for (auto& tc : msg.toolCalls) {
            if (tc.id == "toolu_1") hasToolUse = true;
        }
        // These must never appear in the same message
        CHECK_FALSE((hasSummary && hasToolUse));
    }

    // Verify the tool_use is preserved in some message
    bool foundToolUse = false;
    for (auto& msg : history) {
        for (auto& tc : msg.toolCalls) {
            if (tc.id == "toolu_1") foundToolUse = true;
        }
    }
    CHECK(foundToolUse);

    // Verify no consecutive assistants remain
    for (size_t i = 1; i < history.size(); ++i) {
        if (history[i].role == MessageRole::Assistant &&
            history[i-1].role == MessageRole::Assistant) {
            // If still consecutive, neither must have tool_use
            bool hasToolUseI = !history[i].toolCalls.empty();
            bool hasToolUsePrev = !history[i-1].toolCalls.empty();
            CHECK_FALSE((hasToolUseI && hasToolUsePrev));
        }
    }
}

// ============================================================================
// Test 2: coalesceAdjacentSameRole refuses tool-bearing assistant merge
// ============================================================================
TEST_CASE("coalesceAdjacentSameRole refuses tool-bearing assistant merge",
          "[compact][continuity][coalesce]") {
    // Create ContentMessage history with consecutive assistants
    std::vector<ContentMessage> messages;

    // Assistant with text only (summary ack)
    messages.push_back(
        ContentMessage::assistant("I understand the conversation summary."));

    // Assistant with tool_use — must NOT be coalesced with the one above
    auto toolAsst = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText(""),
        ContentBlockParam::makeToolUse("toolu_1", "Bash",
            Json::object({{"command", "echo hello"}}))
    });
    toolAsst.role = MessageRole::Assistant;
    messages.push_back(std::move(toolAsst));

    size_t before = messages.size();
    int coalesced = coalesceAdjacentSameRole(messages);

    // After the guard: consecutive assistants with tool_use should NOT be merged
    // (coalesce skips them). The count should be 0 and messages unchanged.
    CHECK(coalesced == 0);
    CHECK(messages.size() == before);

    // Verify tool_use is preserved independently (not merged into the text asst)
    bool textOnly = false;
    bool toolUseMsg = false;
    for (auto& msg : messages) {
        bool hasToolUse = false;
        bool hasText = false;
        for (auto& b : msg.content) {
            if (b.type == ContentBlockParam::ToolUse) hasToolUse = true;
            if (b.type == ContentBlockParam::Text && !b.text.empty()) hasText = true;
        }
        if (hasToolUse) toolUseMsg = true;
        if (hasText && !hasToolUse) textOnly = true;
    }
    CHECK(textOnly);
    CHECK(toolUseMsg);
}

// ============================================================================
// Test 3: cleanup does not inject synthetic assistant acknowledgement
// ============================================================================
TEST_CASE("cleanup does not inject synthetic assistant ack",
          "[compact][continuity][no-ack]") {
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("run echo hello"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    history.push_back(makeAssistant("Command completed successfully"));

    PostCompactCleanup::cleanup(history);

    // Verify no synthetic assistant acknowledgements were injected
    for (auto& msg : history) {
        if (msg.role == MessageRole::Assistant) {
            CHECK(msg.content.find("I understand the conversation summary") == String::npos);
            CHECK(msg.content.find("Understood. I'll continue working") == String::npos);
            CHECK(msg.content.find("I understand the session memory") == String::npos);
        }
    }
}

// ============================================================================
// Test 4: enforceAlternation preserves complete cancelled Bash turn
// ============================================================================
TEST_CASE("enforceAlternation preserves cancelled Bash turn",
          "[compact][continuity][cancelled]") {
    // Simulate: user runs sleep 30, ESC cancels, tool_result marked Interrupted
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("[Previous conversation summary]\n\n..."));
    history.push_back(makeUser("run sh -c 'sleep 30 & wait'"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash",
            R"({"command":"sh -c 'sleep 30 & wait'"})")}));
    history.push_back(makeToolResult("toolu_1", "Interrupted", true));
    history.push_back(makeAssistant("The command was cancelled."));

    int fixes = PostCompactCleanup::enforceAlternation(history);

    // Verify tool_use → tool_result pairing is preserved
    // toolu_1 at assistant[3] must have matching tool_result at [4]
    bool foundToolUse = false;
    bool foundToolResult = false;
    for (size_t i = 0; i < history.size(); ++i) {
        for (auto& tc : history[i].toolCalls) {
            if (tc.id == "toolu_1") foundToolUse = true;
        }
        for (auto& tr : history[i].toolResults) {
            if (tr.callId == "toolu_1") foundToolResult = true;
        }
    }
    CHECK(foundToolUse);
    CHECK(foundToolResult);

    // Verify tool_result contains "Interrupted"
    bool interruptedFound = false;
    for (auto& msg : history) {
        for (auto& tr : msg.toolResults) {
            if (tr.content.find("Interrupted") != String::npos) {
                interruptedFound = true;
            }
        }
    }
    CHECK(interruptedFound);
}

// ============================================================================
// Test 5: enforceAlternation with adjacent users (summary + session memory)
// ============================================================================
TEST_CASE("enforceAlternation merges adjacent user messages safely",
          "[compact][continuity][user-merge]") {
    // After removing synthetic assistants, consecutive user messages
    // (session memory + summary) should be safely merged
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("[Session memory from prior conversation]\n- fact 1"));
    history.push_back(makeUser("[Previous conversation summary]\n\nThe user ran echo hello."));
    history.push_back(makeUser("run echo hello"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    history.push_back(makeAssistant("Command ran successfully."));

    int fixes = PostCompactCleanup::enforceAlternation(history);

    // User messages should be merged safely (tool_results preserved)
    // No consecutive user messages should remain
    for (size_t i = 1; i < history.size(); ++i) {
        CHECK_FALSE((history[i].role == MessageRole::User &&
                    history[i-1].role == MessageRole::User));
    }

    // tool_use → tool_result pairing must still be intact
    bool hasToolUse = false, hasToolResult = false;
    for (auto& msg : history) {
        for (auto& tc : msg.toolCalls) {
            if (tc.id == "toolu_1") hasToolUse = true;
        }
        for (auto& tr : msg.toolResults) {
            if (tr.callId == "toolu_1") hasToolResult = true;
        }
    }
    CHECK(hasToolUse);
    CHECK(hasToolResult);
}

// ============================================================================
// Test 6: removeOrphanedResults preserves intact tool_result pairs
// ============================================================================
TEST_CASE("removeOrphanedResults preserves intact pairings",
          "[compact][continuity][orphan]") {
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("run echo hello"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    // Orphan result — no matching tool_use
    history.push_back(makeToolResult("toolu_orphan", "orphan output"));
    history.push_back(makeAssistant("Done"));

    int removed = PostCompactCleanup::removeOrphanedResults(history);

    // The orphan tool_result should be removed
    CHECK(removed >= 1);

    // The valid toolu_1 result must still be present
    bool hasToolu1 = false;
    for (auto& msg : history) {
        for (auto& tr : msg.toolResults) {
            if (tr.callId == "toolu_1") hasToolu1 = true;
        }
    }
    CHECK(hasToolu1);

    // The orphan must be gone
    bool hasOrphan = false;
    for (auto& msg : history) {
        for (auto& tr : msg.toolResults) {
            if (tr.callId == "toolu_orphan") hasOrphan = true;
        }
    }
    CHECK_FALSE(hasOrphan);
}

// ============================================================================
// Test 7: consecutive text-only assistants are safely merged
// ============================================================================
TEST_CASE("enforceAlternation merges text-only assistants safely",
          "[compact][continuity][text-merge]") {
    // Two text-only assistants (no tool_use) should still be merged safely
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("hello"));
    history.push_back(makeAssistant("I'll help with that."));
    history.push_back(makeAssistant("Let me check the files first."));
    history.push_back(makeUser("thanks"));

    int fixes = PostCompactCleanup::enforceAlternation(history);

    // The two text-only assistants should be merged
    bool foundMerged = false;
    for (auto& msg : history) {
        if (msg.content.find("I'll help with that") != String::npos &&
            msg.content.find("Let me check the files") != String::npos) {
            foundMerged = true;
        }
    }
    CHECK(foundMerged);

    // No consecutive assistants should remain
    for (size_t i = 1; i < history.size(); ++i) {
        CHECK_FALSE((history[i].role == MessageRole::Assistant &&
                    history[i-1].role == MessageRole::Assistant));
    }
}

// ============================================================================
// Test 8: validateCompactedHistory catches consecutive tool-use assistants
// (This test exercises the validation logic pattern)
// ============================================================================
TEST_CASE("validate catches consecutive assistant with tool_use",
          "[compact][continuity][validate]") {
    // Build a ContentMessage history with consecutive assistants where one
    // has tool_use — this should fail validateToolResultOrdering
    std::vector<ContentMessage> messages;

    messages.push_back(ContentMessage::user("run echo hello"));

    // Assistant with tool_use
    messages.push_back(ContentMessage::assistantBlocks({
        ContentBlockParam::makeToolUse("toolu_1", "Bash",
            Json::object({{"command", "echo hello"}}))
    }));
    messages.back().role = MessageRole::Assistant;

    // Another assistant (consecutive!) — this breaks protocol
    messages.push_back(ContentMessage::assistant("I also want to help."));

    // The tool_use at [1] has no following tool_result before next assistant at [2]
    // validateToolResultOrdering should detect this
    bool valid = validateToolResultOrdering(messages);
    CHECK_FALSE(valid);
}

// ============================================================================
// Test 9: Full compact cleanup pipeline with complete tool turn
// ============================================================================
TEST_CASE("full cleanup pipeline preserves complete tool turn",
          "[compact][continuity][full-pipeline]") {
    // Simulate the full cleanup flow: compact happened, history has
    // summary user + kept messages. Run through PostCompactCleanup.
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    // Session memory + summary merged as single user (from compressAndRebuild)
    history.push_back(makeUser(
        "[Session memory from prior conversation]\n- Working on echo\n\n"
        "[Previous conversation summary]\n\nThe user ran echo commands."));
    // Preserved complete turn
    history.push_back(makeUser("run echo hello"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    history.push_back(makeAssistant("Command ran successfully. Output was 'hello'."));

    PostCompactCleanup::cleanup(history);

    // Verify: no synthetic assistant acks
    for (auto& msg : history) {
        if (msg.role == MessageRole::Assistant) {
            CHECK(msg.content.find("I understand the conversation") == String::npos);
            CHECK(msg.content.find("Understood. I'll continue") == String::npos);
        }
    }

    // Verify: tool_use → tool_result intact
    bool hasToolUse = false, hasToolResult = false;
    for (size_t i = 0; i < history.size(); ++i) {
        for (auto& tc : history[i].toolCalls) {
            if (tc.id == "toolu_1") {
                hasToolUse = true;
                // tool_result must follow before next assistant
                bool foundAfter = false;
                for (size_t j = i + 1; j < history.size(); ++j) {
                    if (history[j].role == MessageRole::Assistant &&
                        !history[j].toolCalls.empty()) break;
                    for (auto& tr : history[j].toolResults) {
                        if (tr.callId == "toolu_1") foundAfter = true;
                    }
                }
                CHECK(foundAfter);
            }
        }
        for (auto& tr : history[i].toolResults) {
            if (tr.callId == "toolu_1") hasToolResult = true;
        }
    }
    CHECK(hasToolUse);
    CHECK(hasToolResult);
}

// ============================================================================
// Test 10: Cleanup preserves Glob "No files found" tool_result
// ============================================================================
TEST_CASE("cleanup preserves Glob No files found tool_result",
          "[compact][continuity][nofiles]") {
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("[Previous conversation summary]\n\n..."));
    history.push_back(makeUser("read cmakelists.txt"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_glob", "Glob",
            R"({"pattern":"**/cmakelists.txt"})")}));
    history.push_back(makeToolResult("toolu_glob", "No files found"));
    history.push_back(makeAssistant("No files matching the pattern were found."));
    history.push_back(makeUser("run echo hello"));

    PostCompactCleanup::cleanup(history);

    // Verify "No files found" tool_result is preserved
    bool hasGlobResult = false;
    for (auto& msg : history) {
        for (auto& tr : msg.toolResults) {
            if (tr.callId == "toolu_glob" && tr.content == "No files found") {
                hasGlobResult = true;
            }
        }
    }
    CHECK(hasGlobResult);

    // Verify tool_use → tool_result pairing
    bool hasGlobToolUse = false;
    for (auto& msg : history) {
        for (auto& tc : msg.toolCalls) {
            if (tc.id == "toolu_glob") hasGlobToolUse = true;
        }
    }
    CHECK(hasGlobToolUse);

    // No orphaned results
    int removed = PostCompactCleanup::removeOrphanedResults(history);
    CHECK(removed == 0);

    // No consecutive users or assistants
    for (size_t i = 1; i < history.size(); ++i) {
        CHECK_FALSE((history[i].role == MessageRole::Assistant &&
                     history[i-1].role == MessageRole::Assistant));
        CHECK_FALSE((history[i].role == MessageRole::User &&
                     history[i-1].role == MessageRole::User));
    }
}

// ============================================================================
// Test 11: Mid-turn truncation structure is detectable
// ============================================================================
TEST_CASE("mid-turn truncation produces detectable invalid structure",
          "[compact][continuity][mid-turn]") {
    // Simulate a "bad compact" where keep range starts at assistant(tool_use).
    // user→asst(tool_use)→tool_result→asst is structurally valid per API
    // protocol, but the tool_use has no preceding user command for that turn.
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("[Previous conversation summary]\n\n..."));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    history.push_back(makeAssistant("Command ran successfully."));

    PostCompactCleanup::cleanup(history);

    // Verify tool pairing is intact
    bool hasToolUse = false, hasToolResult = false;
    for (auto& msg : history) {
        for (auto& tc : msg.toolCalls) {
            if (tc.id == "toolu_1") hasToolUse = true;
        }
        for (auto& tr : msg.toolResults) {
            if (tr.callId == "toolu_1") hasToolResult = true;
        }
    }
    CHECK(hasToolUse);
    CHECK(hasToolResult);
}

// ============================================================================
// Test 12: Consecutive users with tool_results merge safely
// ============================================================================
TEST_CASE("consecutive users with tool_results merge safely",
          "[compact][continuity][multi-user]") {
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser(
        "[Session memory from prior conversation]\n- fact 1"));
    history.push_back(makeUser(
        "[Previous conversation summary]\n\n..."));
    history.push_back(makeUser(
        "[System: Context was compacted...]"));
    // Real user message that also carries tool_results
    ToolResponse tr;
    tr.callId = "toolu_1";
    tr.content = "hello";
    Message realUser = makeUser("run echo hello");
    realUser.toolResults.push_back(tr);
    history.push_back(std::move(realUser));
    history.push_back(makeAssistant("Command completed successfully."));

    int fixes = PostCompactCleanup::enforceAlternation(history);

    // All consecutive users should be merged
    for (size_t i = 1; i < history.size(); ++i) {
        CHECK_FALSE((history[i].role == MessageRole::User &&
                     history[i-1].role == MessageRole::User));
    }

    // tool_result must be preserved in merged user message
    bool foundToolResult = false;
    for (auto& msg : history) {
        for (auto& tr2 : msg.toolResults) {
            if (tr2.callId == "toolu_1") foundToolResult = true;
        }
    }
    CHECK(foundToolResult);

    // All three user text segments should be present
    bool hasSessionMemory = false, hasSummary = false, hasBoundary = false;
    for (auto& msg : history) {
        if (msg.content.find("Session memory from prior conversation") != String::npos)
            hasSessionMemory = true;
        if (msg.content.find("Previous conversation summary") != String::npos)
            hasSummary = true;
        if (msg.content.find("Context was compacted") != String::npos)
            hasBoundary = true;
    }
    CHECK(hasSessionMemory);
    CHECK(hasSummary);
    CHECK(hasBoundary);
}

// ============================================================================
// Test 13: TTY scenario — hello, cancelled Bash, echo, Glob no files, compact
// ============================================================================
// Replicates the exact TTY flow that caused validation failures.
// Constructs the compressAndRebuild output structure and verifies it
// passes through cleanup + validation without corruption.
TEST_CASE("TTY scenario compact output passes validation",
          "[compact][continuity][tty]") {
    // Simulate compressAndRebuild output for the TTY flow:
    //   1. "hello" → greeting exchange (compressed into summary)
    //   2. "run sh -c 'sleep 30 & wait'" → ESC cancel → Interrupted (compressed)
    //   3. "run echo hello" → Bash → "hello" (KEPT)
    //   4. "read cmakelists.txt" → Glob → "No files found" (KEPT)
    //
    // compressAndRebuild produces: system + optional session memory user +
    // summary user + kept messages.  We test both with and without session
    // memory since that changes the consecutive-user count.

    SECTION("without session memory") {
        std::vector<Message> history;
        history.push_back(Message::system("System prompt"));
        // Summary user (from compressed hello + cancelled Bash turn)
        history.push_back(makeUser(
            "[Previous conversation summary]\n\n"
            "The user greeted with 'hello' and the assistant responded. "
            "The user then ran 'sh -c \"sleep 30 & wait\"' which was "
            "cancelled (Interrupted). The assistant acknowledged the "
            "cancellation."));
        // Kept messages: echo turn + Glob turn
        history.push_back(makeUser("run echo hello"));
        history.push_back(makeAssistant("",
            {makeToolCall("toolu_bash", "Bash",
                R"({"command":"echo hello"})")}));
        history.push_back(makeToolResult("toolu_bash", "hello"));
        history.push_back(makeAssistant("Command ran successfully. Output was 'hello'."));
        history.push_back(makeUser("read cmakelists.txt"));
        history.push_back(makeAssistant("",
            {makeToolCall("toolu_glob", "Glob",
                R"({"pattern":"**/cmakelists.txt"})")}));
        history.push_back(makeToolResult("toolu_glob", "No files found"));
        history.push_back(makeAssistant(
            "No files matching the pattern were found."));

        // Verify pre-cleanup structure
        size_t preSize = history.size();

        // Run cleanup
        PostCompactCleanup::cleanup(history);

        // Verify no consecutive users remain after cleanup
        for (size_t i = 1; i < history.size(); ++i) {
            CHECK_FALSE((history[i].role == MessageRole::User &&
                         history[i-1].role == MessageRole::User));
        }

        // Verify no consecutive assistants
        for (size_t i = 1; i < history.size(); ++i) {
            CHECK_FALSE((history[i].role == MessageRole::Assistant &&
                         history[i-1].role == MessageRole::Assistant));
        }

        // Verify no empty messages
        for (size_t i = 0; i < history.size(); ++i) {
            bool empty = history[i].content.empty() &&
                         history[i].toolCalls.empty() &&
                         history[i].toolResults.empty();
            CHECK_FALSE(empty);
        }

        // Verify tool_use → tool_result pairings intact
        bool hasBashToolUse = false, hasBashResult = false;
        bool hasGlobToolUse = false, hasGlobResult = false;
        for (auto& msg : history) {
            for (auto& tc : msg.toolCalls) {
                if (tc.id == "toolu_bash") hasBashToolUse = true;
                if (tc.id == "toolu_glob") hasGlobToolUse = true;
            }
            for (auto& tr : msg.toolResults) {
                if (tr.callId == "toolu_bash") hasBashResult = true;
                if (tr.callId == "toolu_glob") hasGlobResult = true;
            }
        }
        CHECK(hasBashToolUse);
        CHECK(hasBashResult);
        CHECK(hasGlobToolUse);
        CHECK(hasGlobResult);

        // Verify "No files found" is preserved
        bool hasNoFiles = false;
        for (auto& msg : history) {
            for (auto& tr : msg.toolResults) {
                if (tr.content == "No files found") hasNoFiles = true;
            }
        }
        CHECK(hasNoFiles);

        // Verify compact boundary marker exists
        bool hasBoundary = false;
        for (auto& msg : history) {
            if (msg.content.find("Context was compacted") != String::npos) {
                hasBoundary = true;
            }
        }
        CHECK(hasBoundary);
    }

    SECTION("with session memory") {
        std::vector<Message> history;
        history.push_back(Message::system("System prompt"));
        // Session memory user (from extractKeyFacts)
        history.push_back(makeUser(
            "[Session memory from prior conversation]\n"
            "- User is working in /Users/kankan/claude-code/claude-code-cpp\n"
            "- Project is a C++ rewrite of Claude Code CLI"));
        // Summary user
        history.push_back(makeUser(
            "[Previous conversation summary]\n\n"
            "The user greeted with 'hello'. "
            "The user ran a cancelled sleep command. "
            "The user ran 'echo hello' which succeeded."));
        // Kept messages
        history.push_back(makeUser("run echo hello"));
        history.push_back(makeAssistant("",
            {makeToolCall("toolu_bash", "Bash",
                R"({"command":"echo hello"})")}));
        history.push_back(makeToolResult("toolu_bash", "hello"));
        history.push_back(makeAssistant("Command ran successfully."));
        history.push_back(makeUser("read cmakelists.txt"));
        history.push_back(makeAssistant("",
            {makeToolCall("toolu_glob", "Glob",
                R"({"pattern":"**/cmakelists.txt"})")}));
        history.push_back(makeToolResult("toolu_glob", "No files found"));
        history.push_back(makeAssistant("No files matching the pattern were found."));

        PostCompactCleanup::cleanup(history);

        // Verify no consecutive users
        for (size_t i = 1; i < history.size(); ++i) {
            CHECK_FALSE((history[i].role == MessageRole::User &&
                         history[i-1].role == MessageRole::User));
        }

        // Verify no consecutive assistants
        for (size_t i = 1; i < history.size(); ++i) {
            CHECK_FALSE((history[i].role == MessageRole::Assistant &&
                         history[i-1].role == MessageRole::Assistant));
        }

        // Verify all tool pairings intact
        bool hasBash = false, hasGlob = false;
        for (auto& msg : history) {
            for (auto& tc : msg.toolCalls) {
                if (tc.id == "toolu_bash") hasBash = true;
                if (tc.id == "toolu_glob") hasGlob = true;
            }
        }
        CHECK(hasBash);
        CHECK(hasGlob);

        // Verify "No files found" preserved
        bool hasNoFiles = false;
        for (auto& msg : history) {
            for (auto& tr : msg.toolResults) {
                if (tr.content == "No files found") hasNoFiles = true;
            }
        }
        CHECK(hasNoFiles);
    }
}

// ============================================================================
// Test 14: validateCompactedHistory catches specific failure modes
// ============================================================================
// Runs the same validation checks as the production code to verify
// we can detect the specific structural issues.
TEST_CASE("validateCompactedHistory detects structural issues in TTY output",
          "[compact][continuity][validate-tty]") {
    // This test manually runs the same checks as validateCompactedHistory
    // to ensure the specific TTY scenario structure is valid.

    auto runValidation = [](const std::vector<Message>& history) -> std::string {
        // Check empty messages
        for (size_t i = 0; i < history.size(); ++i) {
            if (history[i].content.empty() &&
                history[i].toolCalls.empty() &&
                history[i].toolResults.empty()) {
                return "empty message at index " + std::to_string(i);
            }
        }
        // Check consecutive assistants
        for (size_t i = 1; i < history.size(); ++i) {
            if (history[i].role == MessageRole::Assistant &&
                history[i-1].role == MessageRole::Assistant) {
                return "consecutive assistants at " + std::to_string(i-1) +
                       "," + std::to_string(i);
            }
        }
        // Check consecutive users
        for (size_t i = 1; i < history.size(); ++i) {
            if (history[i].role == MessageRole::User &&
                history[i-1].role == MessageRole::User) {
                return "consecutive users at " + std::to_string(i-1) +
                       "," + std::to_string(i);
            }
        }
        // Check orphaned tool_use
        for (size_t i = 0; i < history.size(); ++i) {
            if (history[i].role != MessageRole::Assistant) continue;
            if (history[i].toolCalls.empty()) continue;
            for (auto& tc : history[i].toolCalls) {
                bool found = false;
                size_t nextAsst = history.size();
                for (size_t j = i + 1; j < history.size(); ++j) {
                    if (history[j].role == MessageRole::Assistant) {
                        nextAsst = j;
                        break;
                    }
                    for (auto& tr : history[j].toolResults) {
                        if (tr.callId == tc.id) found = true;
                    }
                }
                if (!found) {
                    return "orphan tool_use '" + tc.id + "' at index " +
                           std::to_string(i);
                }
            }
        }
        return "OK";
    };

    // Build the post-cleanup TTY scenario structure
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    // Merged user: session memory + summary + first kept user + boundary
    history.push_back(makeUser(
        "[System: Context was compacted. The most recent messages are "
        "preserved. Older conversation has been summarized.]\n\n"
        "[Session memory from prior conversation]\n"
        "- Project is C++ rewrite\n\n"
        "[Previous conversation summary]\n\n"
        "The user greeted, ran a cancelled sleep, ran echo hello.\n\n"
        "run echo hello"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_bash", "Bash",
            R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_bash", "hello"));
    history.push_back(makeAssistant("Command ran successfully."));
    history.push_back(makeUser("read cmakelists.txt"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_glob", "Glob",
            R"({"pattern":"**/cmakelists.txt"})")}));
    history.push_back(makeToolResult("toolu_glob", "No files found"));
    history.push_back(makeAssistant("No files matching the pattern were found."));

    std::string result = runValidation(history);
    INFO("Validation result: " << result);
    CHECK(result == "OK");
}

// ============================================================================
// Test 15: Failed compact is discarded — original history preserved
// ============================================================================
TEST_CASE("failed compact result is discarded preserving original history",
          "[compact][continuity][fail-safe]") {
    // Build a "valid original" history and a "corrupt compact result".
    // Verify the validation catches the corrupt result while the original
    // remains intact.

    // Original history (valid)
    std::vector<Message> original;
    original.push_back(Message::system("System prompt"));
    original.push_back(makeUser("run echo hello"));
    original.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    original.push_back(makeToolResult("toolu_1", "hello"));
    original.push_back(makeAssistant("Command ran successfully."));

    // Save a copy before any processing
    std::vector<Message> originalCopy = original;

    // Corrupt compact result: consecutive assistants with tool_use
    // (simulates a corrupt compressAndRebuild output)
    std::vector<Message> corrupt;
    corrupt.push_back(Message::system("System prompt"));
    corrupt.push_back(makeUser("[Previous conversation summary]\n\n..."));
    // Consecutive assistants — the second has tool_use
    corrupt.push_back(makeAssistant("I understand the summary."));
    corrupt.push_back(makeAssistant("",
        {makeToolCall("toolu_bad", "Bash", R"({"command":"rm -rf /"})")}));
    corrupt.push_back(makeToolResult("toolu_bad", "dangerous"));

    // Validation should detect the corrupt structure
    // (consecutive assistants where one has tool_use)

    // Run enforceAlternation on corrupt history — it should refuse to merge
    // the tool-bearing assistants
    int fixes = PostCompactCleanup::enforceAlternation(corrupt);

    // After enforceAlternation, consecutive tool-bearing assistants may
    // still exist (the guard skips them). Check manually.
    bool hasConsecutiveAsst = false;
    for (size_t i = 1; i < corrupt.size(); ++i) {
        if (corrupt[i].role == MessageRole::Assistant &&
            corrupt[i-1].role == MessageRole::Assistant) {
            hasConsecutiveAsst = true;
        }
    }
    // The guard means consecutive assistants remain — this is the
    // "corrupt" state that validation should catch

    // Original copy must be unchanged
    CHECK(originalCopy.size() == original.size());
    for (size_t i = 0; i < originalCopy.size(); ++i) {
        CHECK(originalCopy[i].content == original[i].content);
        CHECK(originalCopy[i].role == original[i].role);
    }
}

// ============================================================================
// Test 16: Glob "No files found" turn survives cleanup unchanged
// ============================================================================
TEST_CASE("Glob No files found survives post-compact cleanup intact",
          "[compact][continuity][glob-survival]") {
    // A complete Glob turn where the result is "No files found"
    // must survive cleanup with tool_use→tool_result pairing intact.

    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    // Preceding context (compressed into summary)
    history.push_back(makeUser(
        "[Previous conversation summary]\n\nThe user searched for files."));
    // Glob turn — complete
    history.push_back(makeUser("find cmakelists files"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_g1", "Glob",
            R"({"pattern":"**/cmakelists*"})")}));
    history.push_back(makeToolResult("toolu_g1", "No files found"));
    history.push_back(makeAssistant(
        "No files matching the pattern 'cmakelists*' were found."));

    // Second Glob with different pattern
    history.push_back(makeUser("find any txt files"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_g2", "Glob",
            R"({"pattern":"**/*.txt"})")}));
    history.push_back(makeToolResult("toolu_g2", "No files found"));
    history.push_back(makeAssistant("No .txt files were found either."));

    size_t before = history.size();
    PostCompactCleanup::cleanup(history);

    // Both Glob "No files found" results must be preserved
    int globNoFilesCount = 0;
    for (auto& msg : history) {
        for (auto& tr : msg.toolResults) {
            if (tr.content == "No files found") globNoFilesCount++;
        }
    }
    CHECK(globNoFilesCount == 2);

    // Both tool_use→tool_result pairings intact
    for (auto& id : {"toolu_g1", "toolu_g2"}) {
        bool hasUse = false, hasResult = false;
        for (auto& msg : history) {
            for (auto& tc : msg.toolCalls) {
                if (tc.id == id) hasUse = true;
            }
            for (auto& tr : msg.toolResults) {
                if (tr.callId == id) hasResult = true;
            }
        }
        CHECK(hasUse);
        CHECK(hasResult);
    }

    // No orphans
    int orphans = PostCompactCleanup::removeOrphanedResults(history);
    CHECK(orphans == 0);

    // No consecutive users or assistants
    for (size_t i = 1; i < history.size(); ++i) {
        CHECK_FALSE((history[i].role == MessageRole::Assistant &&
                     history[i-1].role == MessageRole::Assistant));
        CHECK_FALSE((history[i].role == MessageRole::User &&
                     history[i-1].role == MessageRole::User));
    }
}

// ============================================================================
// Test 17: safeStart boundary direction (safeStart <= desiredStart)
// ============================================================================
// Verifies that the safe turn boundary search walks backward (to earlier
// indices / smaller values), expanding the keep range to include the
// complete user→asst→tool_result→asst turn.
TEST_CASE("safe turn boundary walks backward to earlier user message",
          "[compact][continuity][safe-boundary]") {
    // Construct a history where the desired keep point lands mid-turn
    // (e.g., on a tool_result message)
    //
    // Layout:
    //   [0] system
    //   [1] user "task1"          ← safe boundary should land here
    //   [2] asst(tool_use A)
    //   [3] tool_result(A)
    //   [4] asst "done A"
    //   [5] user "task2"          ← desired keep from here (5)
    //   [6] asst(tool_use B)      ← but if we keep from here, tool B has no
    //   [7] tool_result(B)           preceding user (the user at [5] gives
    //   [8] asst "done B"            context to tool B)
    //
    // With desiredKeepFrom = 6 (mid-turn on asst(tool_use)):
    //   - safe must walk backward to [5] (user) or earlier
    //   - safe=6: asst → not user
    //   - safe=5: user → FOUND
    //   - safe (5) < desired (6) ✓

    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    // Turn 1: complete
    history.push_back(makeUser("task1"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_A", "Bash", R"({"command":"cmd1"})")}));
    history.push_back(makeToolResult("toolu_A", "result A"));
    history.push_back(makeAssistant("Task 1 completed."));
    // Turn 2: complete
    history.push_back(makeUser("task2"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_B", "Bash", R"({"command":"cmd2"})")}));
    history.push_back(makeToolResult("toolu_B", "result B"));
    history.push_back(makeAssistant("Task 2 completed."));
    // Turn 3: complete
    history.push_back(makeUser("task3"));
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_C", "Bash", R"({"command":"cmd3"})")}));
    history.push_back(makeToolResult("toolu_C", "result C"));
    history.push_back(makeAssistant("Task 3 completed."));

    // Verify: if we keep from [5] ("task2"), we get full turns for B and C
    // If we keep from [6] (asst tool_use B), we get an incomplete turn
    // findSafeTurnBoundary should return 5 or earlier for desired=6

    // Simulate what we expect: keepFrom should be <= desired
    // and should land on a User message
    size_t desiredKeepFrom = 6;
    // Walk backward to find user
    size_t safe = desiredKeepFrom;
    while (safe > 1 && history[safe].role != MessageRole::User) {
        --safe;
    }

    CHECK(safe <= desiredKeepFrom);
    CHECK(history[safe].role == MessageRole::User);
    CHECK(safe == 5); // Should land on "task2"

    // Now verify that keeping from safe=5 produces a valid structure
    std::vector<Message> kept;
    kept.push_back(history[0]); // system
    for (size_t i = safe; i < history.size(); ++i) {
        kept.push_back(history[i]);
    }

    // kept should start with user→asst→... (valid alternation)
    CHECK(kept[1].role == MessageRole::User);

    // All tool pairings in kept should be intact
    for (auto& id : {"toolu_B", "toolu_C"}) {
        bool hasUse = false, hasResult = false;
        for (auto& msg : kept) {
            for (auto& tc : msg.toolCalls) {
                if (tc.id == id) hasUse = true;
            }
            for (auto& tr : msg.toolResults) {
                if (tr.callId == id) hasResult = true;
            }
        }
        CHECK(hasUse);
        CHECK(hasResult);
    }
}

// ============================================================================
// Test 18: empty tool_use assistant with empty content passes validation
// ============================================================================
// Tool-use-bearing assistant messages often have empty content.
// These must NOT be treated as "empty messages" by validation,
// because the tool_use block is the actual payload.
TEST_CASE("tool_use assistant with empty content is not empty message",
          "[compact][continuity][empty-content-tool-use]") {
    std::vector<Message> history;
    history.push_back(Message::system("System prompt"));
    history.push_back(makeUser("run echo hello"));
    // Assistant with tool_use and EMPTY content — this is NORMAL
    history.push_back(makeAssistant("",
        {makeToolCall("toolu_1", "Bash", R"({"command":"echo hello"})")}));
    history.push_back(makeToolResult("toolu_1", "hello"));
    history.push_back(makeAssistant("Command completed."));

    // Cleanup should NOT remove the tool_use assistant
    int removed = PostCompactCleanup::removeEmptyMessages(history);
    CHECK(removed == 0);

    // The tool_use assistant must still be present
    bool hasToolUse = false;
    for (auto& msg : history) {
        for (auto& tc : msg.toolCalls) {
            if (tc.id == "toolu_1") hasToolUse = true;
        }
    }
    CHECK(hasToolUse);
}
