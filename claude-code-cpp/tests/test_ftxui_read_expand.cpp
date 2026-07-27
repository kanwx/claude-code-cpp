/**
 * FTXUI Read tool expanded content preview tests
 *
 * Verifies that expanded Read tool results show file paths and content
 * previews, while collapsed view remains compact.
 */
#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <claude/ui/ContentBlockRenderer.hpp>
#include <claude/stream/ContentBlock.hpp>
#include <string>
#include <vector>

using namespace claude;

// Helper: render an ftxui Element to a Screen and return all lines as text
static std::vector<std::string> renderToLines(ftxui::Element doc, int width = 60) {
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(width),
        ftxui::Dimension::Fixed(20)
    );
    ftxui::Render(screen, doc);

    std::vector<std::string> lines;
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string line;
        for (int x = 0; x < screen.dimx(); ++x) {
            line += screen.at(x, y);
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// Helper: join rendered lines into a single string for substring matching
static std::string renderedText(ftxui::Element doc) {
    auto lines = renderToLines(std::move(doc));
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += "\n";
        out += lines[i];
    }
    return out;
}

// Helper: create a Read ToolResult ContentBlock
static ContentBlock makeReadResult(const std::string& filePath, int lineCount,
                                    const std::string& contentPreview = "",
                                    bool truncated = false,
                                    int previewLines = 0, int totalLines = 0) {
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = "Read";
    cb.summary = ToolResultSummary::success(
        std::to_string(lineCount) + " lines",
        /*bold=*/true,
        " from " + filePath
    );
    cb.summary.contentPreview = contentPreview;
    cb.summary.contentPreviewTruncated = truncated;
    cb.summary.previewLinesShown = previewLines;
    cb.summary.totalLines = totalLines;
    return cb;
}

// ========== Test 1: Single Read expanded shows file path + content preview ==========

TEST_CASE("Read ToolResult expanded shows content preview", "[read-expand]") {
    ContentBlock block = makeReadResult(
        "/tmp/test.cpp", 5,
        "  1  int main() {\n  2    return 0;\n  3  }\n"
    );
    block.expanded = true;

    ftxui::Element el = renderFtxuiElement(block, {});

    std::string out = renderedText(std::move(el));
    CHECK(out.find("test.cpp") != std::string::npos);
    CHECK(out.find("5 lines") != std::string::npos);
    CHECK(out.find("int main()") != std::string::npos);
    CHECK(out.find("return 0") != std::string::npos);
}

// ========== Test 2: Collapsed Read does NOT show content preview ==========

TEST_CASE("Read ToolResult collapsed does not show content preview", "[read-expand]") {
    ContentBlock block = makeReadResult(
        "/tmp/test.cpp", 5,
        "  1  int main() {\n  2    return 0;\n"
    );
    block.expanded = false;

    ftxui::Element el = renderFtxuiElement(block, {});

    std::string out = renderedText(std::move(el));
    // Should show file path and line count but NOT the body content
    CHECK(out.find("test.cpp") != std::string::npos);
    CHECK(out.find("int main()") == std::string::npos);
}

// ========== Test 3: Multi-file expanded group shows each file path ==========

TEST_CASE("Expanded CollapsedGroup shows each child file path", "[read-expand]") {
    ContentBlock group;
    group.type = ContentBlock::CollapsedGroup;
    group.toolName = "Read";
    group.summary.primaryText = "Read 2 files";
    group.expanded = true;

    group.children.push_back(
        makeReadResult("/tmp/a.cpp", 10, "  a content\n")
    );
    group.children.push_back(
        makeReadResult("/tmp/b.cpp", 20, "  b content\n")
    );

    ftxui::Element el = renderFtxuiElement(group, {});

    std::string out = renderedText(std::move(el));
    CHECK(out.find("a.cpp") != std::string::npos);
    CHECK(out.find("b.cpp") != std::string::npos);
}

// ========== Test 4: Collapsed group remains compact ==========

TEST_CASE("Collapsed CollapsedGroup does not dump child content", "[read-expand]") {
    ContentBlock group;
    group.type = ContentBlock::CollapsedGroup;
    group.toolName = "Read";
    group.summary.primaryText = "Read 2 files";
    group.expanded = false;

    group.children.push_back(
        makeReadResult("/tmp/a.cpp", 10, "  aaaaaaaaaaaaaaaaaaaaaa\n")
    );
    group.children.push_back(
        makeReadResult("/tmp/b.cpp", 20, "  bbbbbbbbbbbbbbbbbbbbbb\n")
    );

    ftxui::Element el = renderFtxuiElement(group, {});

    std::string out = renderedText(std::move(el));
    CHECK(out.find("Read 2 files") != std::string::npos);
    CHECK(out.find("aaaaa") == std::string::npos);
    CHECK(out.find("bbbbb") == std::string::npos);
}

// ========== Test 5: Long content is truncated with indicator ==========

TEST_CASE("Read content preview truncation indicator", "[read-expand]") {
    std::string longContent;
    for (int i = 1; i <= 30; ++i) {
        longContent += "  " + std::to_string(i) + "  line content here\n";
    }

    ContentBlock block = makeReadResult(
        "/tmp/large.cpp", 100,
        longContent,
        /*truncated=*/true,
        /*previewLines=*/20,
        /*totalLines=*/100
    );
    block.expanded = true;

    ftxui::Element el = renderFtxuiElement(block, {});

    std::string out = renderedText(std::move(el));
    CHECK(out.find("truncated") != std::string::npos);
    CHECK(out.find("100 lines") != std::string::npos);
}

// ========== Test 6: Read error shows error detail ==========

TEST_CASE("Read error expanded shows error detail", "[read-expand]") {
    ContentBlock block;
    block.type = ContentBlock::ToolResult;
    block.toolName = "Read";
    block.summary = ToolResultSummary::error("File not found: /tmp/missing.cpp");
    block.expanded = true;

    ftxui::Element el = renderFtxuiElement(block, {});

    std::string out = renderedText(std::move(el));
    CHECK(out.find("File not found") != std::string::npos);
}

// ========== Test 8: ToolGroup inside expanded CollapsedGroup shows children ==========
// Regression test for Bug 1: multi-file Read via ToolGroup (Pass 3) wrapped by
// CollapsedGroup (Pass 4) must expand the inner ToolGroup when isInExpandedGroup=true.

TEST_CASE("ToolGroup inside expanded CollapsedGroup shows children", "[read-expand]") {
    // Simulate what the pipeline produces for multi-file Read:
    // Pass 3: groupConsecutiveToolUses → ToolGroup
    // Pass 4: collapseReadSearchGroups → CollapsedGroup wrapping ToolGroup
    ContentBlock toolGroup;
    toolGroup.type = ContentBlock::ToolGroup;
    toolGroup.toolName = "Read";
    toolGroup.summary.primaryText = "Read 3 files";
    toolGroup.expanded = false;  // default: not independently expanded

    toolGroup.children.push_back(
        makeReadResult("/tmp/a.cpp", 10, "  a content\n")
    );
    toolGroup.children.push_back(
        makeReadResult("/tmp/b.cpp", 20, "  b content\n")
    );
    toolGroup.children.push_back(
        makeReadResult("/tmp/c.cpp", 30, "  c content\n")
    );

    ContentBlock collGroup;
    collGroup.type = ContentBlock::CollapsedGroup;
    collGroup.toolName = "Read";
    collGroup.summary.primaryText = "Read 3 files";
    collGroup.expanded = true;
    collGroup.children.push_back(std::move(toolGroup));

    ftxui::Element el = renderFtxuiElement(collGroup, {});

    std::string out = renderedText(std::move(el));
    // The CollapsedGroup itself is expanded, so the ToolGroup child
    // receives isInExpandedGroup=true and should show its own children.
    CHECK(out.find("a.cpp") != std::string::npos);
    CHECK(out.find("b.cpp") != std::string::npos);
    CHECK(out.find("c.cpp") != std::string::npos);
}

// ========== Test 7: Expanded and collapsed render differ ==========

TEST_CASE("Expanded vs collapsed output differ", "[read-expand]") {
    std::string preview = "  1  hello world\n  2  more content\n";

    ContentBlock expandedBlock = makeReadResult(
        "/tmp/e.cpp", 2, preview
    );
    expandedBlock.expanded = true;

    ContentBlock collapsedBlock = makeReadResult(
        "/tmp/e.cpp", 2, preview
    );
    collapsedBlock.expanded = false;

    std::string expOut = renderedText(renderFtxuiElement(expandedBlock, {}));
    std::string colOut = renderedText(renderFtxuiElement(collapsedBlock, {}));

    // Expanded should contain the preview text; collapsed should not
    CHECK(expOut.find("hello world") != std::string::npos);
    CHECK(colOut.find("hello world") == std::string::npos);
    // But both should show the basic info
    CHECK(expOut.find("e.cpp") != std::string::npos);
    CHECK(colOut.find("e.cpp") != std::string::npos);
}

// ============================================================================
// P6-P1c: Unified ⎿ margin prefix snapshot tests
// ============================================================================
// UTF-8 bytes:
//   ⎿ = \xe2\x8e\xbf  (U+23BF, "shouldered open box")
//   ⏺ = \xe2\x8f\xba  (U+23FA, "black circle for record")
//   ● = \xe2\x97\x8f  (U+25CF, "black circle")
//   › = \xe2\x80\xba  (U+203A, "single right-pointing angle quotation mark")

// Helper: render to Screen for column-level inspection
static ftxui::Screen renderToScreen(ftxui::Element doc, int width = 60) {
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(width),
        ftxui::Dimension::Fixed(20)
    );
    ftxui::Render(screen, doc);
    return screen;
}

// Helper: return first non-empty line as string (trailing spaces stripped)
static std::string firstLine(ftxui::Element doc, int width = 60) {
    auto lines = renderToLines(std::move(doc), width);
    for (auto& l : lines) {
        if (!l.empty()) return l;
    }
    return "";
}

static const std::string U_OPENBOX = "\xe2\x8e\xbf";  // ⎿
static const std::string U_CIRCLE  = "\xe2\x8f\xba";  // ⏺
static const std::string U_BULLET  = "\xe2\x97\x8f";  // ●
static const std::string U_RSQUO   = "\xe2\x80\xba";  // ›

// ===== Test P1c.1: ToolProgress renders with ⎿ marker =====

TEST_CASE("P1c: ToolProgress renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolProgress,
        .toolName = "Bash",
        .activity = "Running cmake --build .",
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);
    CHECK(out.find("Bash") != std::string::npos);
    CHECK(out.find("cmake") != std::string::npos);
}

// ===== Test P1c.2: ToolResult collapsed renders with ⎿ marker =====

TEST_CASE("P1c: ToolResult collapsed renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Read",
        .summary = ToolResultSummary::success("42 lines", false, " from src/main.cpp"),
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find("Read") != std::string::npos);
    CHECK(out.find("src/main.cpp") != std::string::npos);
}

// ===== Test P1c.3: ToolResult expanded header renders with ⎿ marker =====

TEST_CASE("P1c: ToolResult expanded header renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Write",
        .expanded = true,
    };
    block.summary = ToolResultSummary::success("Added 3 lines", false, " to src/main.cpp");
    block.summary.contentPreview = "  line 1\n  line 2\n";
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ on header line
    CHECK(out.find("Write") != std::string::npos);
}

// ===== Test P1c.4: ToolResult error renders with ⎿ marker =====

TEST_CASE("P1c: ToolResult error renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Bash",
        .summary = ToolResultSummary::error("Command failed: exit 1"),
        .resultStatus = ToolResultStatus::Error,
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find("Bash") != std::string::npos);
    CHECK(out.find("Command failed") != std::string::npos);
}

// ===== Test P1c.5: ToolResult cancelled renders with ⎿ marker =====

TEST_CASE("P1c: ToolResult cancelled renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Bash",
        .summary = ToolResultSummary::success("Cancelled"),
        .resultStatus = ToolResultStatus::Cancelled,
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find("Interrupted") != std::string::npos);
}

// ===== Test P1c.6: ToolGroup collapsed renders with ⎿ marker =====

TEST_CASE("P1c: ToolGroup collapsed renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolGroup,
        .summary = ToolResultSummary::success("Read 2 files, Searched 1 pattern"),
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find("Read 2 files") != std::string::npos);
}

// ===== Test P1c.7: ToolGroup expanded renders with ⎿ marker =====

TEST_CASE("P1c: ToolGroup expanded renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::ToolGroup,
        .summary = ToolResultSummary::success("Read 2 files"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read",
                         .summary = ToolResultSummary::success("42 lines")},
        },
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ on header
    CHECK(out.find("Read 2 files") != std::string::npos);
}

// ===== Test P1c.8: AgentProgress renders with ⎿ marker =====

TEST_CASE("P1c: AgentProgress renders with ⎿ marker", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::AgentProgress,
        .text = "Analyzing code...",
        .toolName = "code-reviewer",
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find("code-reviewer") != std::string::npos);
}

// ===== Test P1c.9: CollapsedGroup collapsed uses ⎿, not ⏺ =====

TEST_CASE("P1c: CollapsedGroup collapsed uses ⎿ not ⏺", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Read 3 files"),
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find(U_CIRCLE) == std::string::npos);           // ⏺ absent
    CHECK(out.find("Read 3 files") != std::string::npos);
}

// ===== Test P1c.10: CollapsedGroup expanded uses ⎿, not ⏺ =====

TEST_CASE("P1c: CollapsedGroup expanded uses ⎿ not ⏺", "[p1c-prefix]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Read 3 files"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read",
                         .summary = ToolResultSummary::success("42 lines")},
        },
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_OPENBOX) != std::string::npos);          // ⎿ present
    CHECK(out.find(U_CIRCLE) == std::string::npos);           // ⏺ not on header
    CHECK(out.find("Read 3 files") != std::string::npos);
}

// ===== Test P1c.11: P1a dimmed narration still has no ●/⏺ =====

TEST_CASE("P1c: P1a dimmed narration has no marker", "[p1c-regression]") {
    ContentBlock block{
        .type = ContentBlock::AnswerText,
        .text = "Let me search for files...",
        .dimmed = true,
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    // Dimmed narration: no ● marker, no ⏺ marker, no ⎿ marker
    CHECK(out.find(U_BULLET) == std::string::npos);
    CHECK(out.find(U_CIRCLE) == std::string::npos);
    CHECK(out.find(U_OPENBOX) == std::string::npos);
    CHECK(out.find("Let me search") != std::string::npos);
}

// ===== Test P1c.12: P1b phase header / continuation markers unchanged =====

TEST_CASE("P1c: P1b phase header gets ● marker", "[p1c-regression]") {
    ContentBlock block{
        .type = ContentBlock::AnswerText,
        .text = "analysis",
        .dimmed = false,
        .isFirst = true,
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_BULLET) != std::string::npos);           // ● present
    CHECK(out.find("analysis") != std::string::npos);
}

TEST_CASE("P1c: P1b continuation gets ⏺ marker", "[p1c-regression]") {
    ContentBlock block{
        .type = ContentBlock::AnswerText,
        .text = "details",
        .dimmed = false,
        .isFirst = false,
    };
    std::string out = firstLine(renderFtxuiElement(block, {}));
    CHECK(out.find(U_CIRCLE) != std::string::npos);           // ⏺ present
    CHECK(out.find("details") != std::string::npos);
}

// ===== Test P1c.13: Column alignment — non-focusable tools ⎿ at column 1 =====

TEST_CASE("P1c: ToolProgress ⎿ aligned at column 1", "[p1c-alignment]") {
    ContentBlock block{
        .type = ContentBlock::ToolProgress,
        .toolName = "Bash",
        .activity = "Running test",
    };
    auto screen = renderToScreen(renderFtxuiElement(block, {}));
    // Col 0 = space, Col 1 = ⎿, Col 2 = space, Col 3 = [
    CHECK(screen.at(0, 0) == " ");
    std::string col1 = screen.at(1, 0);
    CHECK(col1 == U_OPENBOX);
    CHECK(screen.at(2, 0) == " ");
}

// ===== Test P1c.14: Column alignment — AnswerText ● aligned at column 1 =====

TEST_CASE("P1c: AnswerText ● aligned at column 1", "[p1c-alignment]") {
    ContentBlock block{
        .type = ContentBlock::AnswerText,
        .text = "Here is the analysis:",
        .isFirst = true,
    };
    auto screen = renderToScreen(renderFtxuiElement(block, {}));
    // Col 0 = space, Col 1 = ●, Col 2 = space
    CHECK(screen.at(0, 0) == " ");
    std::string col1 = screen.at(1, 0);
    CHECK(col1 == U_BULLET);
    CHECK(screen.at(2, 0) == " ");
}

// ===== Test P1c.15: Column alignment — ● and ⎿ at same column =====

TEST_CASE("P1c: ● and ⎿ markers share same column", "[p1c-alignment]") {
    ContentBlock tp{
        .type = ContentBlock::ToolProgress,
        .toolName = "Bash",
        .activity = "test",
    };
    ContentBlock at{
        .type = ContentBlock::AnswerText,
        .text = "Analysis:",
        .isFirst = true,
    };

    auto screenT = renderToScreen(renderFtxuiElement(tp, {}));
    auto screenA = renderToScreen(renderFtxuiElement(at, {}));

    // Both markers at column 1
    CHECK(screenT.at(1, 0) == U_OPENBOX);
    CHECK(screenA.at(1, 0) == U_BULLET);
}

// ===== Test P1c.16: Focused ToolResult shows › left of ⎿ =====

TEST_CASE("P1c: Focused ToolResult shows › left of ⎿", "[p1c-focus]") {
    ContentBlock block{
        .type = ContentBlock::ToolResult,
        .toolName = "Read",
        .summary = ToolResultSummary::success("42 lines"),
    };
    BlockRenderOptions opts;
    opts.isFocusedCollapsible = true;

    auto screen = renderToScreen(renderFtxuiElement(block, opts));
    // Col 0 = ›, Col 1 = space, Col 2 = ⎿, Col 3 = space
    CHECK(screen.at(0, 0) == U_RSQUO);                       // ›
    CHECK(screen.at(1, 0) == " ");                            // space
    CHECK(screen.at(2, 0) == U_OPENBOX);                      // ⎿
    CHECK(screen.at(3, 0) == " ");                            // space
}

// ===== Test P1c.17: Expanded child indent unchanged =====

TEST_CASE("P1c: Expanded CollapsedGroup child tree connectors unchanged", "[p1c-align-preserve]") {
    ContentBlock block{
        .type = ContentBlock::CollapsedGroup,
        .summary = ToolResultSummary::success("Read 2 files"),
        .expanded = true,
        .children = {
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read",
                         .summary = ToolResultSummary::success("42 lines")},
            ContentBlock{.type = ContentBlock::ToolResult, .toolName = "Read",
                         .summary = ToolResultSummary::success("15 lines")},
        },
    };
    std::string out = renderedText(renderFtxuiElement(block, {}));
    // Tree connectors preserved
    CHECK(out.find("\xe2\x94\x9c\xe2\x94\x80") != std::string::npos);  // ├─
    CHECK(out.find("\xe2\x94\x94\xe2\x94\x80") != std::string::npos);  // └─
    CHECK(out.find("42 lines") != std::string::npos);
    CHECK(out.find("15 lines") != std::string::npos);
}
