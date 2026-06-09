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
