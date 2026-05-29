#include <catch2/catch_test_macros.hpp>
#include <claude/console/ActivityDescription.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using claude::getActivityDescription;

TEST_CASE("ActivityDescription basic tools", "[activity]") {
    SECTION("Read with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Read", input.dump()) == "Reading src/main.ts");
    }
    SECTION("Read with offset and limit") {
        json input = {{"file_path", "src/main.ts"}, {"offset", 10}, {"limit", 20}};
        REQUIRE(getActivityDescription("Read", input.dump()) == "Reading src/main.ts:10-30");
    }
    SECTION("Write with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Write", input.dump()) == "Writing src/main.ts");
    }
    SECTION("Edit with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Edit", input.dump()) == "Editing src/main.ts");
    }
    SECTION("Bash with command") {
        json input = {{"command", "npm test"}};
        REQUIRE(getActivityDescription("Bash", input.dump()) == "Running npm test");
    }
    SECTION("Grep with pattern only") {
        json input = {{"pattern", "TODO"}};
        REQUIRE(getActivityDescription("Grep", input.dump()) == "Searching for \"TODO\"");
    }
    SECTION("Grep with pattern and path") {
        json input = {{"pattern", "TODO"}, {"path", "src/"}};
        REQUIRE(getActivityDescription("Grep", input.dump()) == "Searching for \"TODO\" in src/");
    }
    SECTION("Glob with pattern") {
        json input = {{"pattern", "*.ts"}};
        REQUIRE(getActivityDescription("Glob", input.dump()) == "Finding *.ts");
    }
    SECTION("WebFetch with url") {
        json input = {{"url", "https://example.com"}};
        REQUIRE(getActivityDescription("WebFetch", input.dump()) == "Fetching https://example.com");
    }
    SECTION("WebSearch with query") {
        json input = {{"query", "rust tutorials"}};
        REQUIRE(getActivityDescription("WebSearch", input.dump()) == "Searching \"rust tutorials\"");
    }
    SECTION("Agent with agent_type") {
        json input = {{"agent_type", "Explore"}};
        REQUIRE(getActivityDescription("Agent", input.dump()) == "Running Explore agent");
    }
    SECTION("LSP with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("LSP", input.dump()) == "LSP src/main.ts");
    }
    SECTION("MCP with tool_name") {
        json input = {{"tool_name", "weather"}};
        REQUIRE(getActivityDescription("MCP", input.dump()) == "Calling MCP weather");
    }
    SECTION("Unknown tool falls back") {
        json input = {{"file_path", "test.txt"}};
        REQUIRE(getActivityDescription("CustomTool", input.dump()) == "Running CustomTool");
    }
    SECTION("Empty input") {
        REQUIRE(getActivityDescription("Bash", "{}") == "Running Bash");
    }
    SECTION("Invalid JSON") {
        REQUIRE(getActivityDescription("Bash", "not json") == "Running Bash");
    }
}

TEST_CASE("ActivityDescription past tense", "[activity]") {
    SECTION("Read past tense") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Read", input.dump(), false) == "Read src/main.ts");
    }
    SECTION("Bash past tense") {
        json input = {{"command", "npm test"}};
        REQUIRE(getActivityDescription("Bash", input.dump(), false) == "Ran npm test");
    }
    SECTION("Grep past tense") {
        json input = {{"pattern", "TODO"}};
        REQUIRE(getActivityDescription("Grep", input.dump(), false) == "Searched for \"TODO\"");
    }
    SECTION("Write past tense") {
        json input = {{"file_path", "out.txt"}};
        REQUIRE(getActivityDescription("Write", input.dump(), false) == "Wrote out.txt");
    }
    SECTION("Edit past tense") {
        json input = {{"file_path", "out.txt"}};
        REQUIRE(getActivityDescription("Edit", input.dump(), false) == "Edited out.txt");
    }
    SECTION("Glob past tense") {
        json input = {{"pattern", "*.ts"}};
        REQUIRE(getActivityDescription("Glob", input.dump(), false) == "Found *.ts");
    }
    SECTION("WebFetch past tense") {
        json input = {{"url", "https://example.com"}};
        REQUIRE(getActivityDescription("WebFetch", input.dump(), false) == "Fetched https://example.com");
    }
    SECTION("WebSearch past tense") {
        json input = {{"query", "rust"}};
        REQUIRE(getActivityDescription("WebSearch", input.dump(), false) == "Searched \"rust\"");
    }
}
