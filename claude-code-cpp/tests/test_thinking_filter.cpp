#include <catch2/catch_test_macros.hpp>
#include "claude/ui/ThinkingFilter.hpp"

using namespace claude;

// Helper: build a ToolUseBlock for test convenience
static ToolUseBlock makeToolUse(const String& id, const String& name) {
    return {id, name, "{}"};
}

// ========== Test 1: Removes redacted thinking ==========
TEST_CASE("ThinkingFilter removes redacted thinking", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantThinking("thinking..."));
    msgs.push_back(DisplayMessage::assistantRedactedThinking());
    msgs.push_back(DisplayMessage::assistantText("Hello"));

    auto result = ThinkingFilter::removeRedactedThinking(msgs);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].type == DisplayMessage::Type::AssistantThinking);
    REQUIRE(result[1].type == DisplayMessage::Type::AssistantText);
}

// ========== Test 2: removeRedactedThinking passes through when no redacted blocks ==========
TEST_CASE("ThinkingFilter removeRedactedThinking no-op when no redacted", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantText("Hello"));
    msgs.push_back(DisplayMessage::assistantThinking("thinking"));

    auto result = ThinkingFilter::removeRedactedThinking(msgs);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].type == DisplayMessage::Type::AssistantText);
    REQUIRE(result[1].type == DisplayMessage::Type::AssistantThinking);
}

// ========== Test 3: Strips trailing thinking ==========
TEST_CASE("ThinkingFilter strips trailing thinking", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantText("Hello"));
    msgs.push_back(DisplayMessage::assistantThinking("trailing 1"));
    msgs.push_back(DisplayMessage::assistantThinking("trailing 2"));

    auto result = ThinkingFilter::stripTrailingThinking(msgs);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].type == DisplayMessage::Type::AssistantText);
}

// ========== Test 4: Does NOT strip mid-turn thinking ==========
TEST_CASE("ThinkingFilter preserves mid-turn thinking between substance", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantToolUse(makeToolUse("t1", "Bash")));
    msgs.push_back(DisplayMessage::assistantThinking("thinking between tools"));
    msgs.push_back(DisplayMessage::assistantText("Final answer"));

    auto result = ThinkingFilter::stripTrailingThinking(msgs);
    REQUIRE(result.size() == 3);

    auto orphanResult = ThinkingFilter::removeOrphanedThinking(msgs);
    REQUIRE(orphanResult.size() == 3);
}

// ========== Test 5: Removes orphaned thinking-only sequences ==========
TEST_CASE("ThinkingFilter removes orphaned thinking-only sequences", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::systemInfo("System note"));
    msgs.push_back(DisplayMessage::assistantThinking("orphan 1"));
    msgs.push_back(DisplayMessage::assistantThinking("orphan 2"));
    msgs.push_back(DisplayMessage::systemInfo("Another note"));

    auto result = ThinkingFilter::removeOrphanedThinking(msgs);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].type == DisplayMessage::Type::SystemInfo);
    REQUIRE(result[1].type == DisplayMessage::Type::SystemInfo);
}

// ========== Test 6: Keeps thinking adjacent to tool_use ==========
TEST_CASE("ThinkingFilter keeps thinking adjacent to tool_use", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantThinking("before tool"));
    msgs.push_back(DisplayMessage::assistantToolUse(makeToolUse("t1", "Read")));
    msgs.push_back(DisplayMessage::assistantThinking("after tool"));
    msgs.push_back(DisplayMessage::assistantText("result"));

    auto result = ThinkingFilter::removeOrphanedThinking(msgs);
    REQUIRE(result.size() == 4);
}

// ========== Test 7: apply() chains redacted + trailing ==========
TEST_CASE("ThinkingFilter apply removes redacted and trailing", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantRedactedThinking());   // tier 1: removed
    msgs.push_back(DisplayMessage::assistantThinking("trailing")); // tier 2: trailing (after redacted removed)

    auto result = ThinkingFilter::apply(msgs);
    REQUIRE(result.empty());
}

// ========== Test 8: apply() full chain ==========
TEST_CASE("ThinkingFilter apply full chain redacted+orphaned+trailing", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantRedactedThinking());         // tier 1: removed
    msgs.push_back(DisplayMessage::assistantThinking("orphan"));         // tier 3: orphaned (redacted removed, adjacent before is nothing, adjacent after is systemInfo)
    msgs.push_back(DisplayMessage::systemInfo("System note"));           // not substance — orphan stays orphaned
    msgs.push_back(DisplayMessage::assistantText("Hello"));
    msgs.push_back(DisplayMessage::assistantThinking("trailing"));       // tier 2: trailing

    auto result = ThinkingFilter::apply(msgs);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].type == DisplayMessage::Type::SystemInfo);
    REQUIRE(result[1].type == DisplayMessage::Type::AssistantText);
}

// ========== Test 9: Handles empty input ==========
TEST_CASE("ThinkingFilter handles empty input", "[thinking-filter]") {
    std::vector<DisplayMessage> empty;

    REQUIRE(ThinkingFilter::removeRedactedThinking(empty).empty());
    REQUIRE(ThinkingFilter::stripTrailingThinking(empty).empty());
    REQUIRE(ThinkingFilter::removeOrphanedThinking(empty).empty());
    REQUIRE(ThinkingFilter::apply(empty).empty());
}

// ========== Test 10: Handles all-thinking input (all removed) ==========
TEST_CASE("ThinkingFilter removes all-thinking input", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantThinking("think 1"));
    msgs.push_back(DisplayMessage::assistantRedactedThinking());
    msgs.push_back(DisplayMessage::assistantThinking("think 2"));

    auto result = ThinkingFilter::apply(msgs);
    REQUIRE(result.empty());
}

// ========== Test 11: All-substance input (no thinking at all) ==========
TEST_CASE("ThinkingFilter passes through all-substance input unchanged", "[thinking-filter]") {
    std::vector<DisplayMessage> msgs;
    msgs.push_back(DisplayMessage::assistantText("Hello"));
    msgs.push_back(DisplayMessage::assistantToolUse(makeToolUse("t1", "Read")));
    msgs.push_back(DisplayMessage::assistantText("World"));

    auto result = ThinkingFilter::apply(msgs);
    REQUIRE(result.size() == 3);
    REQUIRE(result[0].type == DisplayMessage::Type::AssistantText);
    REQUIRE(result[1].type == DisplayMessage::Type::AssistantToolUse);
    REQUIRE(result[2].type == DisplayMessage::Type::AssistantText);
}
