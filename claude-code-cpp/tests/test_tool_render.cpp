#include <catch2/catch_test_macros.hpp>
#include <claude/core/Types.hpp>

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
