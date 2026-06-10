#include <catch2/catch_test_macros.hpp>
#include "claude/stream/IncrementalBlockParser.hpp"

using namespace claude;

TEST_CASE("Blank line triggers boundary", "[blockparser]") {
    IncrementalBlockParser parser;
    CHECK_FALSE(parser.append("Hello world"));
    CHECK(parser.append("\n\n"));
}

TEST_CASE("ATX header triggers boundary", "[blockparser]") {
    IncrementalBlockParser parser;
    CHECK_FALSE(parser.append("Some text\n"));
    CHECK(parser.append("\n# Heading"));
}

TEST_CASE("Code fence enter and exit", "[blockparser]") {
    IncrementalBlockParser parser;
    CHECK(parser.append("\n```cpp\n"));
    CHECK_FALSE(parser.append("code line\n\nmore code\n"));
    CHECK(parser.append("```\n"));
}

TEST_CASE("List item after blank line triggers boundary", "[blockparser]") {
    IncrementalBlockParser parser;
    CHECK_FALSE(parser.append("Paragraph text\n"));
    CHECK(parser.append("\n- List item"));
}

TEST_CASE("Blockquote start triggers boundary", "[blockparser]") {
    IncrementalBlockParser parser;
    CHECK_FALSE(parser.append("Normal text\n"));
    CHECK(parser.append("\n> Quote"));
}

TEST_CASE("Reset clears state", "[blockparser]") {
    IncrementalBlockParser parser;
    parser.append("Text\n\n");
    parser.reset();
    CHECK(parser.lastBoundaryPos() == 0);
}

TEST_CASE("No boundary in continuous text", "[blockparser]") {
    IncrementalBlockParser parser;
    CHECK_FALSE(parser.append("Just a single line"));
    CHECK(parser.lastBoundaryPos() == 0);
}
