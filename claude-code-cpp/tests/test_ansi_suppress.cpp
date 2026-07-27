#include <catch2/catch_test_macros.hpp>
#include <claude/console/AnsiSuppress.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/ui/ContentBlockRenderer.hpp>
#include <claude/stream/ContentBlock.hpp>
#include <cstdlib>
#include <string>

using namespace claude;

// ============================================================
// stripAnsi tests
// ============================================================

TEST_CASE("stripAnsi removes SGR color codes", "[ansi_suppress]") {
    String input = String(AnsiStyle::RED) + "Error" + AnsiStyle::RESET;
    String result = stripAnsi(input);
    REQUIRE(result == "Error");
}

TEST_CASE("stripAnsi removes DIM and BOLD", "[ansi_suppress]") {
    String input = String(AnsiStyle::DIM) + "dim text" + AnsiStyle::RESET;
    String result = stripAnsi(input);
    REQUIRE(result == "dim text");

    String input2 = String(AnsiStyle::BOLD) + "bold" + AnsiStyle::RESET;
    String result2 = stripAnsi(input2);
    REQUIRE(result2 == "bold");
}

TEST_CASE("stripAnsi removes cursor save/restore", "[ansi_suppress]") {
    String input = String("\033[s") + "text" + String("\033[u");
    String result = stripAnsi(input);
    REQUIRE(result == "text");
}

TEST_CASE("stripAnsi removes 24-bit color codes", "[ansi_suppress]") {
    String input = String("\033[38;2;255;0;0m") + "red" + String("\033[0m");
    String result = stripAnsi(input);
    REQUIRE(result == "red");
}

TEST_CASE("stripAnsi removes OSC sequences", "[ansi_suppress]") {
    String input = String("\033]8;;http://example.com\033\\link\033]8;;\033\\");
    String result = stripAnsi(input);
    REQUIRE(result == "link");
}

TEST_CASE("stripAnsi passes through plain text unchanged", "[ansi_suppress]") {
    String input = "plain text without any escapes";
    String result = stripAnsi(input);
    REQUIRE(result == input);
}

TEST_CASE("stripAnsi handles empty string", "[ansi_suppress]") {
    String result = stripAnsi("");
    REQUIRE(result.empty());
}

TEST_CASE("stripAnsi removes multiple ANSI sequences", "[ansi_suppress]") {
    String input = String(AnsiStyle::YELLOW) + "warn: " + AnsiStyle::RESET +
                   String(AnsiStyle::BOLD) + "important" + AnsiStyle::RESET;
    String result = stripAnsi(input);
    REQUIRE(result == "warn: important");
}

TEST_CASE("stripAnsi removes complex semantic ANSI", "[ansi_suppress]") {
    // Simulate MessageResponse::format output
    String input = String(AnsiStyle::Semantic::TOOL_PREFIX) + "  ⎿ " + AnsiStyle::RESET + "content";
    String result = stripAnsi(input);
    REQUIRE(result == "  ⎿ content");
}

// ============================================================
// renderPlain tests (ContentBlock-based)
// ============================================================

TEST_CASE("renderPlain produces no ANSI codes for ToolResult", "[ansi_suppress]") {
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = "Read";
    cb.summary = ToolResultSummary::success("Read file.txt");
    cb.expanded = false;

    String plain = ContentBlockRenderer::renderPlain(cb);
    // Must contain the prefix and summary but no ANSI
    REQUIRE(plain.find("\033") == String::npos);
    REQUIRE(plain.find("Read file.txt") != String::npos);
}

TEST_CASE("renderPlain produces no ANSI codes for Error", "[ansi_suppress]") {
    ContentBlock cb;
    cb.type = ContentBlock::ErrorMessage;
    cb.text = "something went wrong";

    String plain = ContentBlockRenderer::renderPlain(cb);
    REQUIRE(plain.find("\033") == String::npos);
    REQUIRE(plain.find("something went wrong") != String::npos);
}

TEST_CASE("renderPlain produces no ANSI codes for ToolGroup", "[ansi_suppress]") {
    ContentBlock cb;
    cb.type = ContentBlock::ToolGroup;
    cb.summary = ToolResultSummary::success("Read 3 files");
    cb.expanded = false;

    String plain = ContentBlockRenderer::renderPlain(cb);
    REQUIRE(plain.find("\033") == String::npos);
    REQUIRE(plain.find("Read 3 files") != String::npos);
}

TEST_CASE("renderAnsi does contain ANSI codes", "[ansi_suppress]") {
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = "Read";
    cb.summary = ToolResultSummary::success("Read file.txt");
    cb.expanded = false;

    String ansi = ContentBlockRenderer::renderAnsi(cb);
    // renderAnsi should contain ANSI codes
    REQUIRE(ansi.find("\033") != String::npos);
}

TEST_CASE("renderPlain vs renderAnsi consistency", "[ansi_suppress]") {
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = "Read";
    cb.summary = ToolResultSummary::success("Read 3 files");
    cb.expanded = false;

    String ansi = ContentBlockRenderer::renderAnsi(cb);
    String plain = ContentBlockRenderer::renderPlain(cb);

    // renderPlain = stripAnsi(renderAnsi)
    REQUIRE(plain == stripAnsi(ansi));
}

// ============================================================
// NO_COLOR tests
// ============================================================

TEST_CASE("NO_COLOR env var affects supportsAnsiStdout", "[ansi_suppress]") {
    // This test can't verify the actual return value (depends on isatty),
    // but it verifies the NO_COLOR check path doesn't crash.
    // Save and restore NO_COLOR
    const char* saved = std::getenv("NO_COLOR");
    if (saved) saved = strdup(saved);  // NOLINT

    // Set NO_COLOR
    REQUIRE(setenv("NO_COLOR", "1", 1) == 0);
    // On a non-TTY (like in CI), supportsAnsiStdout should already be false.
    // With NO_COLOR set, it must be false regardless.
    REQUIRE(supportsAnsiStdout() == false);

    // Restore
    if (saved) {
        setenv("NO_COLOR", saved, 1);
        free((void*)saved);
    } else {
        unsetenv("NO_COLOR");
    }
}
