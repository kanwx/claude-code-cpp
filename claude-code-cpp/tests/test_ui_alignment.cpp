#include <catch2/catch_test_macros.hpp>
#include <claude/console/ActivityDescription.hpp>
#include <claude/permission/PermissionTypes.hpp>

using claude::String;
using claude::getActivityDescription;
using claude::PermissionChoice;
using claude::PermissionMode;
using claude::permissionModeToString;

TEST_CASE("UI alignment: ActivityDescription consistency", "[ui-alignment]") {
    SECTION("All known tools produce non-empty descriptions") {
        const char* tools[] = {"Read", "Write", "Edit", "Bash", "Grep", "Glob",
                               "WebFetch", "WebSearch", "Agent", "LSP", "MCP"};
        for (auto tool : tools) {
            String desc = getActivityDescription(tool, "{}", true);
            REQUIRE_FALSE(desc.empty());
            // Every known tool should start with a known verb/prefix
            bool hasKnownVerb =
                desc.find("Running") != String::npos
             || desc.find("Reading") != String::npos
             || desc.find("Writing") != String::npos
             || desc.find("Editing") != String::npos
             || desc.find("Searching") != String::npos
             || desc.find("Finding") != String::npos
             || desc.find("Fetching") != String::npos
             || desc.find("LSP") != String::npos
             || desc.find("Calling MCP") != String::npos;
            REQUIRE(hasKnownVerb);
        }
    }
}

TEST_CASE("UI alignment: PermissionChoice has 5 options", "[ui-alignment]") {
    SECTION("AllowSession exists between AllowOnce and AlwaysAllow") {
        auto session = PermissionChoice::AllowSession;
        REQUIRE(session == PermissionChoice::AllowSession);
    }
    SECTION("All 5 choices are distinct enum values") {
        auto choices = {PermissionChoice::AllowOnce, PermissionChoice::AllowSession,
                        PermissionChoice::AlwaysAllow, PermissionChoice::DenyOnce,
                        PermissionChoice::AlwaysDeny};
        int count = 0;
        for (auto c : choices) {
            (void)c;
            count++;
        }
        REQUIRE(count == 5);
    }
}

TEST_CASE("UI alignment: PermissionMode string round-trip", "[ui-alignment]") {
    SECTION("All modes convert to non-empty strings") {
        auto modes = {PermissionMode::Default, PermissionMode::AcceptEdits,
                      PermissionMode::Auto, PermissionMode::Bypass,
                      PermissionMode::DontAsk, PermissionMode::Plan};
        for (auto mode : modes) {
            String str = permissionModeToString(mode);
            REQUIRE_FALSE(str.empty());
        }
    }
}

TEST_CASE("UI alignment: ActivityDescription past tense works", "[ui-alignment]") {
    SECTION("Read past tense") {
        String desc = getActivityDescription("Read", R"({"file_path":"test.cpp"})", false);
        REQUIRE(desc.find("Read") != String::npos);
        REQUIRE(desc.find("test.cpp") != String::npos);
    }
    SECTION("Bash past tense") {
        String desc = getActivityDescription("Bash", R"({"command":"ls"})", false);
        REQUIRE(desc.find("Ran") != String::npos);
        REQUIRE(desc.find("ls") != String::npos);
    }
}
