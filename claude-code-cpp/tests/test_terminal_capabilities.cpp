#include <catch2/catch_test_macros.hpp>
#include <claude/console/TerminalCapabilities.hpp>

using namespace claude::console;

TEST_CASE("detectColorLevel returns int in range 1-3", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(level >= 1);
    REQUIRE(level <= 3);
}

TEST_CASE("supportsTrueColor is consistent with detectColorLevel", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(TerminalCapabilities::supportsTrueColor() == (level >= 3));
}

TEST_CASE("supports256Color is consistent with detectColorLevel", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(TerminalCapabilities::supports256Color() == (level >= 2));
}

TEST_CASE("supportsAtLeast16Color is consistent with detectColorLevel", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(TerminalCapabilities::supportsAtLeast16Color() == (level >= 1));
}
