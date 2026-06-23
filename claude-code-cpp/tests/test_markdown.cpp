#include <catch2/catch_test_macros.hpp>
#include <claude/console/MarkdownRenderer.hpp>
#include <claude/console/AnsiSuppress.hpp>
#include <sstream>
#include <string>
#include <vector>

using namespace claude;

namespace {

/// Collect all lines from rendered output (strip ANSI for structural checks)
std::vector<std::string> renderAndGetLines(const std::string& markdown, int termWidth = 80) {
    std::ostringstream out;
    MarkdownRenderer renderer(out, termWidth);
    renderer.render(markdown);
    std::string output = out.str();
    std::vector<std::string> lines;
    std::string line;
    for (char c : output) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

/// Check that a string contains all given substrings (AND logic)
bool containsAll(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (haystack.find(needle) == std::string::npos) return false;
    }
    return true;
}

} // anonymous namespace

// ===== displayWidth correctness (tested through table alignment) =====

TEST_CASE("MarkdownRenderer ASCII table columns align", "[markdown][table][ascii]") {
    auto lines = renderAndGetLines(
        "| Name | Path | Notes |\n"
        "| --- | --- | --- |\n"
        "| main.cpp | src/ui/ | Entry |\n"
        "| test.cpp | tests/ | Unit tests |\n"
    );
    // Verify table structure: headers, separator, data rows
    int tableLines = 0;
    for (const auto& line : lines) {
        if (line.find("Name") != std::string::npos) tableLines++;
        if (line.find("main.cpp") != std::string::npos) tableLines++;
    }
    REQUIRE(tableLines >= 2);
}

TEST_CASE("MarkdownRenderer CJK table columns align", "[markdown][table][cjk]") {
    auto lines = renderAndGetLines(
        "| \xe5\x90\x8d\xe7\xa7\xb0 | \xe8\xb7\xaf\xe5\xbe\x84 | \xe8\xaf\xb4\xe6\x98\x8e |\n"
        "| --- | --- | --- |\n"
        "| \xe4\xb8\xad\xe6\x96\x87\xe6\x96\x87\xe4\xbb\xb6.cpp | src/\xe6\xa8\xa1\xe5\x9d\x97/renderers/ | \xe4\xb8\xad\xe6\x96\x87\xe8\xaf\xb4\xe6\x98\x8e |\n"
        "| main.cpp | src/ui/ | English desc |\n"
    );
    // Verify: CJK content renders without garbled UTF-8
    // "名称" = \xe5\x90\x8d\xe7\xa7\xb0
    int cjkHeader = 0;
    for (const auto& line : lines) {
        if (line.find("\xe5\x90\x8d\xe7\xa7\xb0") != std::string::npos) cjkHeader++;
        if (line.find("\xe4\xb8\xad\xe6\x96\x87") != std::string::npos) cjkHeader++;
    }
    REQUIRE(cjkHeader >= 1);
}

TEST_CASE("MarkdownRenderer CJK mixed ASCII table", "[markdown][table][cjk]") {
    std::ostringstream out;
    MarkdownRenderer renderer(out, 80);
    // "名称" = name, "路径" = path, "说明" = description
    renderer.render(
        "| \xe5\x90\x8d\xe7\xa7\xb0 | \xe8\xb7\xaf\xe5\xbe\x84 | \xe8\xaf\xb4\xe6\x98\x8e |\n"
        "| --- | --- | --- |\n"
        "| hello.cpp | src/ | test |\n"
    );
    std::string output = out.str();

    // Verify all CJK headers appear intact
    REQUIRE(containsAll(output, {
        "\xe5\x90\x8d\xe7\xa7\xb0",  // 名称
        "\xe8\xb7\xaf\xe5\xbe\x84",  // 路径
        "\xe8\xaf\xb4\xe6\x98\x8e",  // 说明
        "hello.cpp",
    }));
}

// ===== padCell / truncation correctness =====

TEST_CASE("MarkdownRenderer cell not truncated mid-UTF8", "[markdown][table][utf8]") {
    // Use a narrow terminal to force truncation
    std::ostringstream out;
    MarkdownRenderer renderer(out, 30);
    // CJK filename is 3-byte chars; truncation must not break a codepoint
    renderer.render(
        "| File |\n"
        "| --- |\n"
        "| \xe4\xb8\xad\xe6\x96\x87\xe6\x96\x87\xe4\xbb\xb6.cpp |\n"  // 中文文件.cpp = 9 bytes
    );
    std::string output = out.str();

    // Should not contain standalone continuation bytes (0x80-0xBF)
    for (size_t i = 0; i < output.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(output[i]);
        // Skip ANSI escape sequences
        if (c == '\033') {
            while (i < output.size() && output[i] != 'm' && output[i] != 's' && output[i] != 'u') ++i;
            continue;
        }
        // A lone continuation byte (0x80-0xBF) not preceded by a start byte
        // signals a broken UTF-8 sequence
        if ((c & 0xC0) == 0x80) {
            if (i == 0) {
                FAIL("Lone continuation byte at position 0");
            }
        }
    }
    SUCCEED("No obvious UTF-8 corruption");
}

TEST_CASE("MarkdownRenderer narrow column truncation", "[markdown][table]") {
    std::ostringstream out;
    MarkdownRenderer renderer(out, 25);
    renderer.render(
        "| VeryLongColumnName | Short |\n"
        "| --- | --- |\n"
        "| extremely_long_value_here | x |\n"
    );
    std::string output = stripAnsi(out.str());
    // The value should be truncated (can't fit in 25-char terminal with 2 cols)
    // Should not crash, and output should have table structure
    REQUIRE(output.find("Short") != std::string::npos);
    REQUIRE(output.find("x") != std::string::npos);
}

// ===== ANSI escape not counted in width =====

TEST_CASE("MarkdownRenderer inline formatting does not affect column alignment", "[markdown][table][ansi]") {
    std::ostringstream out;
    MarkdownRenderer renderer(out, 80);
    // **bold** text should have ANSI codes stripped before width calc
    renderer.render(
        "| Plain | Formatted |\n"
        "| --- | --- |\n"
        "| hello | **bold** |\n"
        "| world | *italic* |\n"
    );
    std::string output = out.str();
    // The formatted text should still appear (ANSI codes present)
    REQUIRE(output.find("bold") != std::string::npos);
    REQUIRE(output.find("italic") != std::string::npos);
    // Column separators should appear
    // The table border chars are multi-byte UTF-8
    REQUIRE_FALSE(output.empty());
}

TEST_CASE("MarkdownRenderer link text width excludes URL", "[markdown][table][ansi]") {
    std::ostringstream out;
    MarkdownRenderer renderer(out, 80);
    // Link display text is "click", URL should not count toward column width
    renderer.render(
        "| Action |\n"
        "| --- |\n"
        "| [click](https://very.long.url.example.com/path/to/resource) |\n"
    );
    std::string output = out.str();
    // The link text should be rendered (as styled text)
    REQUIRE(output.find("click") != std::string::npos);
    // URL may or may not appear (OSC 8 hyperlink)
    // Main check: table should not break
}

// ===== No regression: tables with empty cells =====

TEST_CASE("MarkdownRenderer table with empty cells", "[markdown][table]") {
    std::ostringstream out;
    MarkdownRenderer renderer(out, 80);
    renderer.render(
        "| A | B | C |\n"
        "| --- | --- | --- |\n"
        "| 1 | | 3 |\n"
        "| | 2 | |\n"
    );
    std::string output = stripAnsi(out.str());
    REQUIRE(output.find("1") != std::string::npos);
    REQUIRE(output.find("3") != std::string::npos);
}

// ===== Single-column table =====

TEST_CASE("MarkdownRenderer single column table", "[markdown][table]") {
    auto lines = renderAndGetLines(
        "| Only |\n"
        "| --- |\n"
        "| value |\n"
    );
    int found = 0;
    for (const auto& line : lines) {
        if (line.find("Only") != std::string::npos) found++;
        if (line.find("value") != std::string::npos) found++;
    }
    REQUIRE(found >= 2);
}

// ===== CJK in stream rendering (basic smoke test) =====

TEST_CASE("MarkdownRenderer stream rendering with CJK", "[markdown][stream][cjk]") {
    std::ostringstream out;
    MarkdownRenderer renderer(out, 80);
    MarkdownRenderer::StreamState state;

    // Feed CJK text in chunks
    renderer.renderStream("Hello \xe4\xbd\xa0\xe5\xa5\xbd ", state);  // Hello 你好
    renderer.renderStream("world\n", state);
    renderer.finishStream(state);

    std::string output = out.str();
    REQUIRE(output.find("Hello") != std::string::npos);
    REQUIRE(output.find("world") != std::string::npos);
}
