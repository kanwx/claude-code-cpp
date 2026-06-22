#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <claude/tool/impl/GrepTool.hpp>
#include <claude/tool/ToolContext.hpp>
#include <fstream>
#include <chrono>
#include <filesystem>

using namespace claude;

// Helper: create a file in a temp dir
struct TestFixture {
    std::filesystem::path tmpDir;
    ToolContext ctx;

    TestFixture() {
        tmpDir = std::filesystem::temp_directory_path() / ("grep_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(tmpDir);
        ctx = ToolContext::create(tmpDir);
    }

    ~TestFixture() {
        std::filesystem::remove_all(tmpDir);
    }

    void writeFile(const std::string& name, const std::string& content) {
        std::ofstream f(tmpDir / name);
        f << content;
    }
};

// ---- Pattern safety checks ----

TEST_CASE("isLiteralPattern") {
    REQUIRE(GrepTool::isLiteralPattern("hello"));
    REQUIRE(GrepTool::isLiteralPattern("simple text"));
    REQUIRE(GrepTool::isLiteralPattern("numbers 123"));
    REQUIRE_FALSE(GrepTool::isLiteralPattern("hello.*world"));
    REQUIRE_FALSE(GrepTool::isLiteralPattern("a+b"));
    REQUIRE_FALSE(GrepTool::isLiteralPattern("file[0-9]"));
    REQUIRE_FALSE(GrepTool::isLiteralPattern("^start"));
    REQUIRE_FALSE(GrepTool::isLiteralPattern("end$"));
}

TEST_CASE("isRegexPatternSafe") {
    // Safe patterns
    REQUIRE(GrepTool::isRegexPatternSafe("hello"));
    REQUIRE(GrepTool::isRegexPatternSafe("hello.*world"));
    REQUIRE(GrepTool::isRegexPatternSafe("foo|bar"));
    REQUIRE(GrepTool::isRegexPatternSafe("a(b|c)d"));

    // Dangerous: nested quantifiers
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe("(a+)+"));
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe("(.*)+"));
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe("(.)+b"));
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe("([^x]+)+"));

    // Dangerous: nested quantifier with whitespace
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe("(a+) +"));

    // Dangerous: repeated wildcards
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe(".*.*"));
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe(".+.+"));

    // Dangerous: too many alternations
    std::string manyPipes;
    for (int i = 0; i < 51; i++) manyPipes += "x|";
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe(manyPipes));

    // Dangerous: too long
    std::string longPattern(501, 'a');
    REQUIRE_FALSE(GrepTool::isRegexPatternSafe(longPattern));
}

// ---- Scenario 1: Dangerous pattern + long 'a' line → fast failure ----

TEST_CASE("GrepTool fallback: dangerous pattern rejects quickly", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;

    // Create a file with a long line of 'a's that would cause catastrophic backtracking
    std::string longA;
    for (int i = 0; i < 2000; i++) longA += 'a';
    longA += "!";  // No match, forces full backtrack
    fx.writeFile("test.txt", longA + "\n");

    Json input;
    input["pattern"] = "(a+)+$";
    input["path"] = fx.tmpDir.string();

    auto start = std::chrono::steady_clock::now();
    String result = tool.execute(input, fx.ctx);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Must return quickly (< 1000ms) with an error about complex pattern
    REQUIRE(elapsed < 1000);
    REQUIRE(result.find("ripgrep") != String::npos);

    GrepTool::forceUseFallbackForTest = false;
}

// ---- Scenario 2: Literal pattern → uses string::find fast path ----

TEST_CASE("GrepTool fallback: literal pattern finds matches", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;
    fx.writeFile("file1.txt", "hello world\nfoo bar\nhello again\n");

    Json input;
    input["pattern"] = "hello";
    input["path"] = fx.tmpDir.string();

    String result = tool.execute(input, fx.ctx);

    REQUIRE(result.find("file1.txt:1: hello world") != String::npos);
    REQUIRE(result.find("file1.txt:3: hello again") != String::npos);

    GrepTool::forceUseFallbackForTest = false;
}

// ---- Scenario 3: Safe regex → uses std::regex and returns correctly ----

TEST_CASE("GrepTool fallback: safe regex finds matches", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;
    fx.writeFile("log.txt",
        "2024-01-15 INFO server started\n"
        "2024-01-15 ERROR connection failed\n"
        "2024-01-15 INFO shutdown complete\n");

    Json input;
    input["pattern"] = "\\d{4}-\\d{2}-\\d{2} ERROR";
    input["path"] = fx.tmpDir.string();

    String result = tool.execute(input, fx.ctx);

    REQUIRE(result.find("ERROR connection failed") != String::npos);
    REQUIRE(result.find("INFO server started") == String::npos);

    GrepTool::forceUseFallbackForTest = false;
}

// ---- Scenario 4: File with very long lines → skip without hang ----

TEST_CASE("GrepTool fallback: long lines are skipped", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;

    // Create a file where one line exceeds 16KB
    std::string longLine(16 * 1024 + 100, 'x');
    fx.writeFile("bigfile.txt", "hello world\n" + longLine + "\nhello again\n");

    Json input;
    input["pattern"] = "hello";
    input["path"] = fx.tmpDir.string();

    auto start = std::chrono::steady_clock::now();
    String result = tool.execute(input, fx.ctx);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Should complete quickly despite the long line
    REQUIRE(elapsed < 500);
    // Should still find the short-line matches
    REQUIRE(result.find("hello world") != String::npos);
    REQUIRE(result.find("hello again") != String::npos);
    // The long line should NOT cause the regex to be searched (it's skipped)

    GrepTool::forceUseFallbackForTest = false;
}

// ---- Scenario 5: rg present → still uses rg, not affected by fallback limits ----

TEST_CASE("GrepTool with rg: still uses rg despite fallback improvements", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = false;  // Clear fallback override

    // Check if rg is available on the system
    bool hasRg = GrepTool::hasRipgrep();

    if (!hasRg) {
        // Skip — rg not available in this environment
        SUCCEED("rg not installed — test skipped (not a failure)");
        return;
    }

    TestFixture fx;
    fx.writeFile("data.txt", "apple\nbanana\ncherry\ndate\n");

    Json input;
    input["pattern"] = "an";
    input["path"] = fx.tmpDir.string();

    String result = tool.execute(input, fx.ctx);

    // rg output includes line numbers with -n flag
    REQUIRE(result.find("banana") != String::npos);
    // rg should not show the "ripgrep installation" error message
    REQUIRE(result.find("install ripgrep") == String::npos);
}

// ---- Edge case: empty pattern ----

TEST_CASE("GrepTool fallback: no matches", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;
    fx.writeFile("file.txt", "foo\nbar\nbaz\n");

    Json input;
    input["pattern"] = "nonexistent";
    input["path"] = fx.tmpDir.string();

    String result = tool.execute(input, fx.ctx);

    REQUIRE(result.find("No matches found") != String::npos);

    GrepTool::forceUseFallbackForTest = false;
}

// ---- files_with_matches mode ----

TEST_CASE("GrepTool fallback: files_with_matches mode", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;
    fx.writeFile("a.txt", "hello\n");
    fx.writeFile("b.txt", "world\n");
    fx.writeFile("c.txt", "hello world\n");

    Json input;
    input["pattern"] = "hello";
    input["path"] = fx.tmpDir.string();
    input["output_mode"] = "files_with_matches";

    String result = tool.execute(input, fx.ctx);

    REQUIRE(result.find("a.txt") != String::npos);
    REQUIRE(result.find("c.txt") != String::npos);
    REQUIRE(result.find("b.txt") == String::npos);

    GrepTool::forceUseFallbackForTest = false;
}

// ---- count mode ----

TEST_CASE("GrepTool fallback: count mode", "[grep_safety]") {
    GrepTool tool;
    GrepTool::forceUseFallbackForTest = true;

    TestFixture fx;
    fx.writeFile("data.txt", "apple\napply\napricot\nbanana\n");

    Json input;
    input["pattern"] = "ap";
    input["path"] = fx.tmpDir.string();
    input["output_mode"] = "count";

    String result = tool.execute(input, fx.ctx);

    REQUIRE(result.find("Found 3 matches") != String::npos);

    GrepTool::forceUseFallbackForTest = false;
}
