#include <catch2/catch_test_macros.hpp>
#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/ui/ToolResultFormatter.hpp"
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
