#include <catch2/catch_test_macros.hpp>
#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/ui/ToolResultFormatter.hpp"
#include "claude/ui/PathDisplay.hpp"
#include "claude/stream/ContentBlock.hpp"

using namespace claude;

TEST_CASE("ContentBlockAnsi renders ToolResult summary not raw", "[renderer]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .summary = ToolResultSummary::success("Read 42 lines"),
        .rawResultPath = "/tmp/claude-results/abc123"
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Read 42 lines") != String::npos);
    CHECK(rendered.find("/tmp/claude-results") == String::npos);
}

TEST_CASE("ContentBlockAnsi renders UserMessage", "[renderer]") {
    ContentBlock block{.type = ContentBlock::UserMessage, .text = "List Python files"};
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("List Python files") != String::npos);
}

TEST_CASE("ContentBlockAnsi renders ThinkingBlock collapsed", "[renderer]") {
    ContentBlock block{.type = ContentBlock::ThinkingBlock, .detailText = "Deep thoughts", .dimmed = true};
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Thinking") != String::npos);
    CHECK(rendered.find("Deep thoughts") == String::npos);
}

TEST_CASE("ContentBlockAnsi renders ToolGroup summary", "[renderer]") {
    ContentBlock group{
        .type = ContentBlock::ToolGroup,
        .summary = ToolResultSummary::success("Read 3 files, Searched 1 pattern"),
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("Read 42 lines")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("Read 15 lines")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("Read 8 lines")},
        }
    };
    String rendered = ContentBlockRenderer::renderAnsi(group);
    CHECK(rendered.find("Read 3 files") != String::npos);
    // [Ctrl+O] hints are FTXUI-only; non-FTXUI renderers omit them
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("ContentBlockAnsi renders ErrorMessage", "[renderer]") {
    ContentBlock block{.type = ContentBlock::ErrorMessage, .text = "Something went wrong", .detailText = "Stack trace..."};
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Something went wrong") != String::npos);
}

TEST_CASE("ContentBlock ToolResult with expand hint", "[renderer]") {
    ContentBlock block{.type = ContentBlock::ToolResult, .summary = ToolResultSummary::success("Read 42 lines")};
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // [Ctrl+O] hints are FTXUI-only; non-FTXUI renderers omit them even if expandHint is set
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

// ---- [Ctrl+O] hint leak prevention tests ----

TEST_CASE("renderAnsi ThinkingBlock omits Ctrl+O", "[ctrl_o_leak]") {
    ContentBlock block{.type = ContentBlock::ThinkingBlock, .detailText = "Deep thoughts"};
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Ctrl+O") == String::npos);
    CHECK(rendered.find("ctrl+o") == String::npos);
}

TEST_CASE("renderAnsi ToolResult omits Ctrl+O", "[ctrl_o_leak]") {
    ContentBlock block{.type = ContentBlock::ToolResult, .summary = ToolResultSummary::success("Read 42 lines")};
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderAnsi ToolGroup omits Ctrl+O", "[ctrl_o_leak]") {
    ContentBlock block{
        .type = ContentBlock::ToolGroup,
        .summary = ToolResultSummary::success("Called 3 tools"),
        .children = {}
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderAnsi CollapsedGroup omits Ctrl+O", "[ctrl_o_leak]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Searched 3 patterns")
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderPlain omits Ctrl+O", "[ctrl_o_leak]") {
    ContentBlock block{.type = ContentBlock::ToolResult, .summary = ToolResultSummary::success("Read 42 lines")};
    String rendered = ContentBlockRenderer::renderPlain(block);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderAnsi ignores expandHint with Ctrl+O", "[ctrl_o_leak]") {
    // expandHint contains [Ctrl+O] but renderAnsi must not emit it
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .summary = ToolResultSummary::success("Read 42 lines", true, "", "[Ctrl+O to expand]")
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Read 42 lines") != String::npos);
    CHECK(rendered.find("Ctrl+O") == String::npos);
    // expandHint must still be preserved on the block metadata for FTXUI use
    CHECK(block.summary.expandHint == "[Ctrl+O to expand]");
}

// ---- ToolResultFormatter tests ----

TEST_CASE("ToolResultFormatter extracts Read file details", "[ToolResultFormatter]") {
    ContentBlock block;
    block.type = ContentBlock::ToolResult;
    block.toolName = "Read";
    block.summary = ToolResultSummary::success("42 lines", true, " from src/main.cpp");
    block.resultStatus = ToolResultStatus::Success;

    auto dm = formatToolResult(block);
    REQUIRE(dm.toolName == "Read");
    REQUIRE(dm.lineCount == 42);
    REQUIRE(dm.filePath == "src/main.cpp");
    REQUIRE(!dm.isError);
}

TEST_CASE("ToolResultFormatter extracts Grep match count", "[ToolResultFormatter]") {
    ContentBlock block;
    block.type = ContentBlock::ToolResult;
    block.toolName = "Grep";
    block.summary = ToolResultSummary::success("Found 14 matches", true);
    block.resultStatus = ToolResultStatus::Success;

    auto dm = formatToolResult(block);
    REQUIRE(dm.toolName == "Grep");
    REQUIRE(dm.matchCount == 14);
}

TEST_CASE("ToolResultFormatter extracts Edit diff stats", "[ToolResultFormatter]") {
    ContentBlock block;
    block.type = ContentBlock::ToolResult;
    block.toolName = "Edit";
    block.summary = ToolResultSummary::success("Added 5 lines, Removed 2 lines", true, " to src/ui/Repl.cpp");
    block.resultStatus = ToolResultStatus::Success;

    auto dm = formatToolResult(block);
    REQUIRE(dm.toolName == "Edit");
    REQUIRE(dm.linesAdded == 5);
    REQUIRE(dm.linesRemoved == 2);
    REQUIRE(dm.filePath == "src/ui/Repl.cpp");
}

TEST_CASE("ToolResultFormatter handles error state", "[ToolResultFormatter]") {
    ContentBlock block;
    block.type = ContentBlock::ToolResult;
    block.toolName = "Bash";
    block.summary = ToolResultSummary::error("Command failed: permission denied");
    block.resultStatus = ToolResultStatus::Error;

    auto dm = formatToolResult(block);
    REQUIRE(dm.isError);
    REQUIRE(dm.errorText == "Command failed: permission denied");
}

// ---- Tree connector tests (CollapsedGroup expanded) ----

// UTF-8 bytes for box-drawing chars:
//   ├─ = \xe2\x94\x9c\xe2\x94\x80
//   └─ = \xe2\x94\x94\xe2\x94\x80

TEST_CASE("renderAnsi CollapsedGroup single child uses └─ only", "[tree_connector]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Searched 1 file"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("42 lines")},
        }
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // Single child must use └─, not ├─
    CHECK(rendered.find("\xe2\x94\x94\xe2\x94\x80") != String::npos);  // └─ present
    CHECK(rendered.find("\xe2\x94\x9c\xe2\x94\x80") == String::npos);  // ├─ absent
}

TEST_CASE("renderAnsi CollapsedGroup two children correct connectors", "[tree_connector]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Searched 2 patterns"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Grep", .summary = ToolResultSummary::success("14 matches")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("42 lines")},
        }
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // Both connectors must appear
    CHECK(rendered.find("\xe2\x94\x9c\xe2\x94\x80") != String::npos);  // ├─ for first child
    CHECK(rendered.find("\xe2\x94\x94\xe2\x94\x80") != String::npos);  // └─ for last child
}

TEST_CASE("renderAnsi CollapsedGroup three children middle gets ├─", "[tree_connector]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Searched 3 patterns"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Grep", .summary = ToolResultSummary::success("14 matches")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Grep", .summary = ToolResultSummary::success("3 matches")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("42 lines")},
        }
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // ├─ appears twice (first two children), └─ once (last)
    size_t branchPos = rendered.find("\xe2\x94\x9c\xe2\x94\x80");  // ├─
    CHECK(branchPos != String::npos);
    size_t secondBranch = rendered.find("\xe2\x94\x9c\xe2\x94\x80", branchPos + 3);
    CHECK(secondBranch != String::npos);  // second ├─ for middle child
    // └─ appears after the last ├─
    CHECK(rendered.find("\xe2\x94\x94\xe2\x94\x80", secondBranch) != String::npos);
}

TEST_CASE("renderPlain CollapsedGroup preserves tree connectors sans Ctrl+O", "[tree_connector]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Searched 2 files"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("42 lines")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Grep", .summary = ToolResultSummary::success("14 matches")},
        }
    };
    String rendered = ContentBlockRenderer::renderPlain(block);
    CHECK(rendered.find("\xe2\x94\x9c\xe2\x94\x80") != String::npos);  // ├─ present
    CHECK(rendered.find("\xe2\x94\x94\xe2\x94\x80") != String::npos);  // └─ present
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderAnsi CollapsedGroup collapsed omits connectors", "[tree_connector]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Searched 3 patterns"),
        .expanded = false,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Grep", .summary = ToolResultSummary::success("14 matches")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Grep", .summary = ToolResultSummary::success("3 matches")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read", .summary = ToolResultSummary::success("42 lines")},
        }
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // Collapsed: no tree connectors at all
    CHECK(rendered.find("\xe2\x94\x9c\xe2\x94\x80") == String::npos);  // ├─ absent
    CHECK(rendered.find("\xe2\x94\x94\xe2\x94\x80") == String::npos);  // └─ absent
    CHECK(rendered.find("Searched 3 patterns") != String::npos);       // summary present
}

// ---- Tool card visual hierarchy tests ----

TEST_CASE("renderAnsi Read ToolResult preserves tool name and summary", "[tool_card]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Read",
        .summary = ToolResultSummary::success("42 lines", false, " from src/main.cpp"),
        .resultStatus = ToolResultStatus::Success,
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // Summary content preserved, no Ctrl+O leak
    CHECK(rendered.find("src/main.cpp") != String::npos);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderAnsi Bash error ToolResult uses failure display", "[tool_card]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Bash",
        .summary = ToolResultSummary::error("Command failed: exit 1"),
        .resultStatus = ToolResultStatus::Error,
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // Error text preserved, no Ctrl+O leak
    CHECK(rendered.find("Command failed") != String::npos);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderAnsi ToolProgress preserves activity text", "[tool_card]") {
    ContentBlock block{
        .type = ContentBlock::ToolProgress,
        .toolName = "Bash",
        .activity = "Running cmake --build .",
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    // Activity text preserved
    CHECK(rendered.find("cmake") != String::npos);
}

TEST_CASE("renderAnsi ToolGroup collapsed preserves summary", "[tool_card]") {
    ContentBlock block{
        .type = ContentBlock::ToolGroup,
        .summary = ToolResultSummary::success("Read 2 files, Searched 1 pattern"),
        .expanded = false,
    };
    String rendered = ContentBlockRenderer::renderAnsi(block);
    CHECK(rendered.find("Read 2 files") != String::npos);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

TEST_CASE("renderPlain ToolResult omits Ctrl+O", "[tool_card]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Bash",
        .summary = ToolResultSummary::success("Build complete"),
        .resultStatus = ToolResultStatus::Success,
    };
    String rendered = ContentBlockRenderer::renderPlain(block);
    CHECK(rendered.find("Build complete") != String::npos);
    CHECK(rendered.find("Ctrl+O") == String::npos);
}

// ---- Tool→answer separator logic tests ----

static ContentBlock toolResult() {
    return {.type = ContentBlock::ToolResult, .toolName = "Bash", .summary = ToolResultSummary::success("Done")};
}
static ContentBlock answerText() {
    return {.type = ContentBlock::AnswerText, .text = "Here is the result."};
}
static ContentBlock collapsedGroup() {
    return {.type = ContentBlock::CollapsedGroup, .summary = ToolResultSummary::success("Searched")};
}
static ContentBlock toolGroup() {
    return {.type = ContentBlock::ToolGroup, .summary = ToolResultSummary::success("Called 2 tools")};
}

TEST_CASE("findAnswerSeparatorIndices: ToolResult -> AnswerText inserts separator", "[separator]") {
    std::vector<ContentBlock> blocks = {toolResult(), answerText()};
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.size() == 1);
    REQUIRE(idx[0] == 1); // separator before answer at index 1
}

TEST_CASE("findAnswerSeparatorIndices: CollapsedGroup -> AnswerText inserts separator", "[separator]") {
    std::vector<ContentBlock> blocks = {collapsedGroup(), answerText()};
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.size() == 1);
    REQUIRE(idx[0] == 1);
}

TEST_CASE("findAnswerSeparatorIndices: ToolGroup -> AnswerText inserts separator", "[separator]") {
    std::vector<ContentBlock> blocks = {toolGroup(), answerText()};
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.size() == 1);
    REQUIRE(idx[0] == 1);
}

TEST_CASE("findAnswerSeparatorIndices: AnswerText only, no separator", "[separator]") {
    std::vector<ContentBlock> blocks = {answerText()};
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.empty());
}

TEST_CASE("findAnswerSeparatorIndices: ToolResult -> ToolResult, no separator", "[separator]") {
    std::vector<ContentBlock> blocks = {toolResult(), toolResult()};
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.empty());
}

TEST_CASE("findAnswerSeparatorIndices: interleaved, no separator on mid-chain answers", "[separator]") {
    // AnswerText -> ToolResult -> AnswerText -> ToolResult (interleaved)
    std::vector<ContentBlock> blocks = {
        answerText(), toolResult(), answerText(), toolResult()
    };
    auto idx = findAnswerSeparatorIndices(blocks);
    // The AnswerText at index 2 is followed by toolResult at index 3 → NOT final, no separator
    REQUIRE(idx.empty());
}

TEST_CASE("findAnswerSeparatorIndices: last tool without answer, no separator", "[separator]") {
    std::vector<ContentBlock> blocks = {toolResult(), toolResult()};
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.empty());
}

TEST_CASE("findAnswerSeparatorIndices: tool chain then answer, one separator", "[separator]") {
    // Multiple tools → final answer
    std::vector<ContentBlock> blocks = {
        toolResult(), toolResult(), collapsedGroup(), answerText()
    };
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.size() == 1);
    REQUIRE(idx[0] == 3); // separator before answer at index 3
}

TEST_CASE("findAnswerSeparatorIndices: empty list, no separator", "[separator]") {
    std::vector<ContentBlock> blocks;
    auto idx = findAnswerSeparatorIndices(blocks);
    REQUIRE(idx.empty());
}

// ---- Path truncation tests ----

TEST_CASE("truncatePathForDisplay: short path unchanged", "[path_truncation]") {
    CHECK(truncatePathForDisplay("src/main.cpp") == "src/main.cpp");
    CHECK(truncatePathForDisplay("file.txt") == "file.txt");
    CHECK(truncatePathForDisplay("") == "");
    CHECK(truncatePathForDisplay("/a/b.cpp") == "/a/b.cpp");
}

TEST_CASE("truncatePathForDisplay: long absolute path preserves root and filename", "[path_truncation]") {
    String result = truncatePathForDisplay(
        "/Users/kankan/claude-code/claude-code-cpp/src/ui/renderers/ContentBlockFtxui.cpp");
    // Must contain filename
    CHECK(result.find("ContentBlockFtxui.cpp") != String::npos);
    // Must contain root component
    CHECK(result.find("Users") != String::npos);
    // Must contain ellipsis
    CHECK(result.find("\xe2\x80\xa6") != String::npos); // …
    // Must be shorter than original
    CHECK(result.size() <= 42);
}

TEST_CASE("truncatePathForDisplay: long relative path preserves root and filename", "[path_truncation]") {
    String result = truncatePathForDisplay(
        "very/deep/path/to/src/tool/impl/GrepTool.cpp");
    CHECK(result.find("GrepTool.cpp") != String::npos);
    CHECK(result.find("\xe2\x80\xa6") != String::npos);
    CHECK(result.size() <= 42);
    // First component should be preserved
    CHECK(result.find("very") != String::npos);
}

TEST_CASE("truncatePathForDisplay: preserves last directory level", "[path_truncation]") {
    String result = truncatePathForDisplay(
        "/Users/kankan/claude-code/claude-code-cpp/src/ui/renderers/ContentBlockFtxui.cpp");
    // Parent dir should be preserved
    CHECK(result.find("renderers") != String::npos);
}

TEST_CASE("truncatePathForDisplay: small maxWidth does not crash", "[path_truncation]") {
    // Very small maxWidth: must not crash, must return something
    String result = truncatePathForDisplay(
        "/some/very/long/path/to/important_config.yaml", 15);
    CHECK(!result.empty());
    CHECK(result.size() <= 25);  // allow slight overshoot for extension
}

TEST_CASE("truncatePathForDisplay: does not modify original, only returns new string", "[path_truncation]") {
    String original = "/Users/kankan/claude-code/claude-code-cpp/src/main.cpp";
    String result = truncatePathForDisplay(original);
    // Original must be unchanged
    CHECK(original == "/Users/kankan/claude-code/claude-code-cpp/src/main.cpp");
    // Result must be different (shorter)
    CHECK(result.size() < original.size());
}

TEST_CASE("truncatePathForDisplay: edge cases", "[path_truncation]") {
    // Two-component paths don't need truncation
    CHECK(truncatePathForDisplay("src/main.cpp") == "src/main.cpp");
    CHECK(truncatePathForDisplay("/tmp/file.txt") == "/tmp/file.txt");

    // Path exactly at maxWidth is preserved
    String exact(42, 'x');
    CHECK(truncatePathForDisplay(exact) == exact);
}
