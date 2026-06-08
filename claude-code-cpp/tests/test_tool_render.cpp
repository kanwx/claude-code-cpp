#include <catch2/catch_test_macros.hpp>
#include <claude/core/Types.hpp>
#include <claude/tool/Tool.hpp>
#include <claude/core/ApiTypes.hpp>

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
