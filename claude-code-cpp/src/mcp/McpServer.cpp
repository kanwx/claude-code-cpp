#include <claude/mcp/McpServer.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>

namespace claude::mcp {

McpServer::McpServer(const String& name, const String& version)
    : serverName_(name), version_(version) {}

void McpServer::addTool(const McpToolDefinition& tool) {
    tools_.push_back(tool);
}

void McpServer::addToolsFromRegistry(ToolRegistry& registry) {
    for (const auto& def : registry.toToolDefinitions()) {
        McpToolDefinition mcpTool;
        mcpTool.name = def.name;
        mcpTool.description = def.description;
        mcpTool.inputSchema = def.inputSchema;
        tools_.push_back(std::move(mcpTool));
    }
}

void McpServer::setToolExecutor(ToolExecutor executor) {
    toolExecutor_ = std::move(executor);
}

void McpServer::runStdio() {
    spdlog::debug("McpServer: starting stdio server '{}'", serverName_);

    while (true) {
        String line = readLine();
        if (line.empty()) {
            // stdin closed
            spdlog::debug("McpServer: stdin closed, shutting down");
            break;
        }

        try {
            Json reqJson = Json::parse(line);
            JsonRpcRequest req;
            req.jsonrpc = reqJson.value("jsonrpc", "2.0");
            req.id = reqJson.value("id", 0);
            req.method = reqJson.value("method", "");
            if (reqJson.contains("params")) {
                req.params = reqJson["params"];
            }

            auto resp = handleRequest(req);
            sendResponse(resp);
        } catch (const Json::parse_error& e) {
            JsonRpcResponse resp;
            resp.jsonrpc = "2.0";
            resp.id = 0;
            resp.error = Json{{"code", -32700}, {"message", "Parse error"}};
            sendResponse(resp);
        }
    }
}

JsonRpcResponse McpServer::handleRequest(const JsonRpcRequest& req) {
    if (req.method == "initialize") return handleInitialize(req);
    if (req.method == "tools/list") return handleListTools(req);
    if (req.method == "tools/call") return handleCallTool(req);
    if (req.method == "ping") return handlePing(req);

    // Notifications (no response needed)
    if (req.method == "notifications/initialized") {
        initialized_ = true;
        return {req.jsonrpc, req.id, Json::object(), std::nullopt};
    }

    JsonRpcResponse resp;
    resp.jsonrpc = "2.0";
    resp.id = req.id;
    resp.error = Json{{"code", -32601}, {"message", "Method not found: " + req.method}};
    return resp;
}

JsonRpcResponse McpServer::handleInitialize(const JsonRpcRequest& req) {
    initialized_ = true;

    Json result;
    result["protocolVersion"] = "2024-11-05";
    result["serverInfo"] = {{"name", serverName_}, {"version", version_}};
    result["capabilities"] = {{"tools", Json::object()}};

    return {"2.0", req.id, result, std::nullopt};
}

JsonRpcResponse McpServer::handleListTools(const JsonRpcRequest& req) {
    Json toolsArray = Json::array();
    for (const auto& tool : tools_) {
        Json t;
        t["name"] = tool.name;
        t["description"] = tool.description;
        t["inputSchema"] = tool.inputSchema;
        if (tool.outputSchema) {
            t["outputSchema"] = *tool.outputSchema;
        }
        toolsArray.push_back(t);
    }

    return {"2.0", req.id, {{"tools", toolsArray}}, std::nullopt};
}

JsonRpcResponse McpServer::handleCallTool(const JsonRpcRequest& req) {
    String toolName = req.params.value("name", "");
    Json arguments = req.params.value("arguments", Json::object());

    // Find tool
    auto it = std::find_if(tools_.begin(), tools_.end(),
        [&](const McpToolDefinition& t) { return t.name == toolName; });

    if (it == tools_.end()) {
        return {"2.0", req.id, Json::object(),
            Json{{"code", -32602}, {"message", "Unknown tool: " + toolName}}};
    }

    if (!toolExecutor_) {
        return {"2.0", req.id, Json::object(),
            Json{{"code", -32603}, {"message", "No tool executor configured"}}};
    }

    auto result = toolExecutor_(toolName, arguments);
    Json content = Json::array();

    if (result) {
        content.push_back({{"type", "text"}, {"text", result.value()}});
        return {"2.0", req.id, {{"content", content}}, std::nullopt};
    } else {
        content.push_back({{"type", "text"}, {"text", result.error()}});
        return {"2.0", req.id,
            {{"content", content}, {"isError", true}},
            std::nullopt};
    }
}

JsonRpcResponse McpServer::handlePing(const JsonRpcRequest& req) {
    return {"2.0", req.id, Json::object(), std::nullopt};
}

String McpServer::readLine() {
    String line;
    std::getline(std::cin, line);
    // Trim trailing \r
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

void McpServer::sendResponse(const JsonRpcResponse& resp) {
    Json j;
    j["jsonrpc"] = resp.jsonrpc;
    j["id"] = resp.id;
    if (resp.error) {
        j["error"] = *resp.error;
    } else {
        j["result"] = resp.result;
    }
    std::cout << j.dump() << "\n" << std::flush;
}

void McpServer::sendNotification(const String& method, const Json& params) {
    Json j;
    j["jsonrpc"] = "2.0";
    j["method"] = method;
    j["params"] = params;
    std::cout << j.dump() << "\n" << std::flush;
}

} // namespace claude::mcp
