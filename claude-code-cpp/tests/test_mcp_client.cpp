#include <catch2/catch_test_macros.hpp>
#include <claude/mcp/McpServer.hpp>
#include <claude/core/Types.hpp>

using namespace claude;
using namespace claude::mcp;

TEST_CASE("McpServer construction", "[mcp]") {
    McpServer server("test", "1.0");
    // No crash — construction succeeds
    REQUIRE(true);
}

TEST_CASE("McpServer addTool", "[mcp]") {
    McpServer server("test", "1.0");
    McpToolDefinition tool{
        "read_file",
        "Read the contents of a file",
        Json::object({{"type", "object"}, {"properties", Json::object()}}),
        std::nullopt
    };
    server.addTool(tool);
    // No crash — tool registration succeeds
    REQUIRE(true);
}

TEST_CASE("McpServer handleRequest initialize", "[mcp]") {
    McpServer server("test", "1.0");
    JsonRpcRequest req;
    req.jsonrpc = "2.0";
    req.id = 1;
    req.method = "initialize";
    req.params = Json::object();

    JsonRpcResponse resp = server.handleRequest(req);

    REQUIRE(resp.id == 1);
    REQUIRE_FALSE(resp.error.has_value());
    REQUIRE(resp.result.is_object());
}

TEST_CASE("McpServer handleRequest tools/list", "[mcp]") {
    McpServer server("test", "1.0");

    McpToolDefinition tool{
        "search_files",
        "Search for files matching a pattern",
        Json::object({{"type", "object"}, {"properties", Json::object({{"pattern", Json::object({{"type", "string"}})}})}}),
        std::nullopt
    };
    server.addTool(tool);

    // Must initialize first, as the server may require it
    JsonRpcRequest initReq;
    initReq.jsonrpc = "2.0";
    initReq.id = 1;
    initReq.method = "initialize";
    initReq.params = Json::object();
    server.handleRequest(initReq);

    JsonRpcRequest req;
    req.jsonrpc = "2.0";
    req.id = 2;
    req.method = "tools/list";
    req.params = Json::object();

    JsonRpcResponse resp = server.handleRequest(req);

    REQUIRE(resp.id == 2);
    REQUIRE_FALSE(resp.error.has_value());
    // The response result should contain the registered tool name
    bool found = false;
    if (resp.result.contains("tools") && resp.result["tools"].is_array()) {
        for (const auto& t : resp.result["tools"]) {
            if (t.value("name", "") == "search_files") {
                found = true;
                break;
            }
        }
    }
    REQUIRE(found);
}

TEST_CASE("McpServer handleRequest unknown method", "[mcp]") {
    McpServer server("test", "1.0");

    // Initialize the server first
    JsonRpcRequest initReq;
    initReq.jsonrpc = "2.0";
    initReq.id = 1;
    initReq.method = "initialize";
    initReq.params = Json::object();
    server.handleRequest(initReq);

    JsonRpcRequest req;
    req.jsonrpc = "2.0";
    req.id = 3;
    req.method = "nonexistent/method";
    req.params = Json::object();

    JsonRpcResponse resp = server.handleRequest(req);

    REQUIRE(resp.id == 3);
    REQUIRE(resp.error.has_value());
}
