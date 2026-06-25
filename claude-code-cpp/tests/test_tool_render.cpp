#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <claude/core/Types.hpp>
#include <claude/tool/Tool.hpp>
#include <claude/core/ApiTypes.hpp>
#include <claude/tool/impl/FileReadTool.hpp>
#include <claude/ui/ToolResultFormatter.hpp>
#include <claude/stream/ContentBlock.hpp>

using namespace claude;

// Concrete test tool
class TestTool : public claude::Tool {
public:
    claude::String name() const override { return "TestTool"; }
    claude::String description() const override { return "test"; }
    claude::String inputSchema() const override { return "{}"; }
    claude::String execute(const claude::Json&, claude::ToolContext&) override { return ""; }
};

class CollapsibleTestTool : public claude::Tool {
public:
    claude::String name() const override { return "CollapsibleTestTool"; }
    claude::String description() const override { return "test"; }
    claude::String inputSchema() const override { return "{}"; }
    claude::String execute(const claude::Json&, claude::ToolContext&) override { return ""; }
    bool isCollapsible() const override { return true; }
    bool isReadTool() const override { return true; }
    claude::ToolResultSummary renderToolResult(const claude::String& result, bool isError,
                            bool isCancelled, bool isRejected) const override {
        if (isError) return claude::ToolResultSummary::error("Error: " + result);
        if (isCancelled) return claude::ToolResultSummary::dim("Cancelled");
        if (isRejected) return claude::ToolResultSummary::dim("Rejected");
        return claude::ToolResultSummary::success("OK: " + result);
    }
    claude::String userFacingName() const override { return "CollapsibleTest"; }
};

TEST_CASE("Tool base class has default render methods", "[tool_render]") {
    TestTool t;
    REQUIRE(t.renderToolUse("args", false) == "");
    REQUIRE(t.renderToolResult("result", false, false, false).empty());
    REQUIRE(t.renderGroupedToolUse({}) == "");
    REQUIRE(t.isCollapsible() == false);
    REQUIRE(t.isSearchTool() == false);
    REQUIRE(t.isReadTool() == false);
    REQUIRE(t.isListTool() == false);
    REQUIRE(t.isMemoryTool() == false);
}

TEST_CASE("Tool subclasses can override render methods", "[tool_render]") {
    CollapsibleTestTool t;
    REQUIRE(t.isCollapsible() == true);
    REQUIRE(t.isReadTool() == true);
    auto okResult = t.renderToolResult("hello", false, false, false);
    REQUIRE(okResult.primaryText == "OK: hello");
    REQUIRE_FALSE(okResult.isError);
    auto errResult = t.renderToolResult("oops", true, false, false);
    REQUIRE(errResult.isError);
    REQUIRE(errResult.errorText == "Error: oops");
    auto cancelResult = t.renderToolResult("", false, true, false);
    REQUIRE(cancelResult.isDim);
    REQUIRE(cancelResult.primaryText == "Cancelled");
    auto rejectResult = t.renderToolResult("", false, false, true);
    REQUIRE(rejectResult.isDim);
    REQUIRE(rejectResult.primaryText == "Rejected");
    REQUIRE(t.userFacingName() == "CollapsibleTest");
}

TEST_CASE("ToolResponse has isCancelled and isRejected fields", "[tool_render]") {
    claude::ToolResponse resp;
    REQUIRE(resp.isCancelled == false);
    REQUIRE(resp.isRejected == false);

    resp.isCancelled = true;
    REQUIRE(resp.isCancelled == true);

    resp.isRejected = true;
    REQUIRE(resp.isRejected == true);
}

TEST_CASE("ToolUseRenderData struct holds tool use data", "[tool_render]") {
    claude::ToolUseRenderData data;
    data.toolUseId = "tu_123";
    data.toolName = "Read";
    data.arguments = R"({"file_path":"/tmp/test.txt"})";
    data.result = "hello world";
    data.isError = false;
    data.isCancelled = false;
    data.isRejected = false;
    data.isInProgress = false;

    REQUIRE(data.toolUseId == "tu_123");
    REQUIRE(data.toolName == "Read");
    REQUIRE(data.isInProgress == false);
}

// ========== FileReadTool::renderToolResult file path tests ==========

TEST_CASE("FileReadTool renderToolResult includes file path in secondaryText", "[tool_render][fileread]") {
    using Catch::Matchers::ContainsSubstring;

    // Simulate a successful text file read result.
    // The actual execute() output format: "Contents of <path>:\n\n<numbered lines>"
    String result = "Contents of /home/user/src/main.cpp:\n\n"
                    "     1\t#include <iostream>\n"
                    "     2\t\n"
                    "     3\tint main() {}\n";

    FileReadTool tool;
    auto summary = tool.renderToolResult(result, /*isError=*/false,
                                         /*isCancelled=*/false, /*isRejected=*/false);

    // Primary text has "N lines" format (number first, no "Read " prefix —
    // formatToolResult parses lineCount from the beginning).
    // Note: line count includes header lines from "Contents of <path>:\n\n" output.
    REQUIRE(summary.primaryText == "5 lines");

    // Secondary text uses " from <path>" format (cleanFilePath strips the prefix)
    REQUIRE(summary.secondaryText == " from /home/user/src/main.cpp");
    REQUIRE_FALSE(summary.isError);
    REQUIRE_FALSE(summary.isDim);
}

TEST_CASE("FileReadTool renderToolResult handles image result format", "[tool_render][fileread]") {
    // Image result format: "[IMAGE: <path> | format=... | ...]"
    String result = "[IMAGE: /home/user/photo.png | format=image/png"
                    " | size=12345 | width=800 | height=600"
                    " | tokens=500 | data=base64content]";

    FileReadTool tool;
    auto summary = tool.renderToolResult(result, /*isError=*/false,
                                         /*isCancelled=*/false, /*isRejected=*/false);

    REQUIRE(summary.secondaryText == " from /home/user/photo.png");
}

TEST_CASE("FileReadTool renderToolResult empty secondaryText for unknown format", "[tool_render][fileread]") {
    // Result without the standard "Contents of ..." prefix — no path extraction.
    String result = "Some custom error or unexpected output format\nline2\n";

    FileReadTool tool;
    auto summary = tool.renderToolResult(result, /*isError=*/false,
                                         /*isCancelled=*/false, /*isRejected=*/false);

    // secondaryText should be empty — we don't guess the path
    REQUIRE(summary.secondaryText.empty());
}

TEST_CASE("FileReadTool two consecutive reads produce correct paths without cross-contamination",
          "[tool_render][fileread]") {
    FileReadTool tool;

    // First read: main.cpp
    String result1 = "Contents of /app/src/main.cpp:\n\n"
                     "     1\t#include <iostream>\n"
                     "     2\tint main() {}\n";
    auto s1 = tool.renderToolResult(result1, false, false, false);
    REQUIRE(s1.secondaryText == " from /app/src/main.cpp");

    // Second read: utils.h — must NOT inherit main.cpp path
    String result2 = "Contents of /app/include/utils.h:\n\n"
                     "     1\t#pragma once\n"
                     "     2\tvoid util();\n";
    auto s2 = tool.renderToolResult(result2, false, false, false);
    REQUIRE(s2.secondaryText == " from /app/include/utils.h");

    // Third read back to main.cpp — verify no caching/staleness
    auto s3 = tool.renderToolResult(result1, false, false, false);
    REQUIRE(s3.secondaryText == " from /app/src/main.cpp");
}

// ========== ToolResultFormatter integration test ==========

TEST_CASE("ToolResultFormatter formats Read display text from renderToolResult output",
          "[tool_render][fileread][formatter]") {
    using Catch::Matchers::ContainsSubstring;

    FileReadTool tool;
    String result = "Contents of /project/src/FtxuiRepl.cpp:\n\n"
                    "     1\t#include <ftxui/component/component.hpp>\n"
                    "     2\t\n"
                    "     3\tusing namespace ftxui;\n";
    auto summary = tool.renderToolResult(result, false, false, false);

    // Verify summary format matches what formatToolResult expects
    // (line count includes header lines from the "Contents of <path>:\n\n" prefix)
    REQUIRE(summary.primaryText == "5 lines");
    REQUIRE(summary.secondaryText == " from /project/src/FtxuiRepl.cpp");

    // Build a ContentBlock and run through formatToolResult
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = "Read";
    cb.summary = summary;

    auto dm = formatToolResult(cb);
    REQUIRE(dm.toolName == "Read");
    REQUIRE(dm.lineCount == 5);
    REQUIRE(dm.filePath == "/project/src/FtxuiRepl.cpp");

    // toDisplayText() produces "path (N lines)" format
    String display = dm.toDisplayText();
    REQUIRE_THAT(display, ContainsSubstring("FtxuiRepl.cpp"));
    REQUIRE_THAT(display, ContainsSubstring("(5 lines)"));
}
