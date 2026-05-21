#include <claude/mcp/McpClient.hpp>
#include <claude/mcp/McpTransport.hpp>
#include <sstream>
#include <atomic>

namespace claude {

namespace {
    std::atomic<int> requestId_{1};

    Json makeRequest(const String& method, const Json& params = Json::object()) {
        return {
            {"jsonrpc", "2.0"},
            {"id", requestId_++},
            {"method", method},
            {"params", params}
        };
    }
}

/// MCP 客户端实现
class McpClientImpl : public McpClient {
public:
    explicit McpClientImpl(std::unique_ptr<McpTransport> transport)
        : transport_(std::move(transport)) {}

    bool connect() override {
        if (!transport_) {
            return false;
        }

        // For SSE/HTTP transports, establish the transport-level connection first.
        // StdioTransport is already connected from its constructor, so connect() is a no-op.
        transport_->connect();

        if (!transport_->isConnected()) {
            return false;
        }

        // 发送 initialize 请求
        Json initRequest = makeRequest("initialize", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {
                {"tools", Json::object()}
            }},
            {"clientInfo", {
                {"name", "claude-code-cpp"},
                {"version", "0.1.0"}
            }}
        });

        transport_->send(initRequest.dump());
        String response = transport_->receive();

        if (response.empty()) {
            return false;
        }

        try {
            Json resp = Json::parse(response);
            if (resp.contains("error")) {
                return false;
            }

            // 发送 initialized 通知
            Json initialized = {
                {"jsonrpc", "2.0"},
                {"method", "notifications/initialized"}
            };
            transport_->send(initialized.dump());

            return true;
        } catch (...) {
            return false;
        }
    }

    void disconnect() override {
        transport_.reset();
    }

    std::vector<McpTool> listTools() override {
        std::vector<McpTool> tools;

        if (!transport_ || !transport_->isConnected()) {
            return tools;
        }

        Json request = makeRequest("tools/list");
        transport_->send(request.dump());

        String response = transport_->receive();
        if (response.empty()) return tools;

        try {
            Json resp = Json::parse(response);
            if (resp.contains("result") && resp["result"].contains("tools")) {
                for (const auto& t : resp["result"]["tools"]) {
                    tools.push_back({
                        .name = t.value("name", ""),
                        .description = t.value("description", ""),
                        .inputSchema = t.value("inputSchema", Json::object())
                    });
                }
            }
        } catch (...) {}

        return tools;
    }

    std::vector<McpResource> listResources() override {
        std::vector<McpResource> resources;

        if (!transport_ || !transport_->isConnected()) {
            return resources;
        }

        Json request = makeRequest("resources/list");
        transport_->send(request.dump());

        String response = transport_->receive();
        if (response.empty()) return resources;

        try {
            Json resp = Json::parse(response);
            if (resp.contains("result") && resp["result"].contains("resources")) {
                for (const auto& r : resp["result"]["resources"]) {
                    resources.push_back({
                        .uri = r.value("uri", ""),
                        .name = r.value("name", ""),
                        .description = r.value("description", "")
                    });
                }
            }
        } catch (...) {}

        return resources;
    }

    String callTool(const String& name, const Json& args) override {
        if (!transport_ || !transport_->isConnected()) {
            return "Error: Not connected";
        }

        Json request = makeRequest("tools/call", {
            {"name", name},
            {"arguments", args}
        });
        transport_->send(request.dump());

        String response = transport_->receive();
        if (response.empty()) {
            return "Error: No response";
        }

        try {
            Json resp = Json::parse(response);
            if (resp.contains("error")) {
                return "Error: " + resp["error"].value("message", "Unknown error");
            }
            if (resp.contains("result")) {
                if (resp["result"].contains("content")) {
                    // 提取文本内容
                    for (const auto& item : resp["result"]["content"]) {
                        if (item.value("type", "") == "text") {
                            return item.value("text", "");
                        }
                    }
                }
                return resp["result"].dump();
            }
        } catch (...) {}

        return "Error: Invalid response";
    }

    String readResource(const String& uri) override {
        if (!transport_ || !transport_->isConnected()) {
            return "Error: Not connected";
        }

        Json request = makeRequest("resources/read", {
            {"uri", uri}
        });
        transport_->send(request.dump());

        String response = transport_->receive();
        if (response.empty()) {
            return "Error: No response";
        }

        try {
            Json resp = Json::parse(response);
            if (resp.contains("error")) {
                return "Error: " + resp["error"].value("message", "Unknown error");
            }
            if (resp.contains("result") && resp["result"].contains("contents")) {
                std::ostringstream oss;
                for (const auto& item : resp["result"]["contents"]) {
                    oss << item.value("text", "");
                }
                return oss.str();
            }
        } catch (...) {}

        return "Error: Invalid response";
    }

private:
    std::unique_ptr<McpTransport> transport_;
};

/// 创建 MCP 客户端工厂函数
std::unique_ptr<McpClient> createMcpClient(const String& command) {
    auto transport = std::make_unique<StdioTransport>(command);
    return std::make_unique<McpClientImpl>(std::move(transport));
}

std::unique_ptr<McpClient> createMcpClientFromConfig(const Json& config) {
    String type = config.value("type", "stdio");

    std::unique_ptr<McpTransport> transport;

    if (type == "stdio") {
        String command = config.value("command", "");
        transport = std::make_unique<StdioTransport>(command);
    } else if (type == "sse") {
        String url = config.value("url", "");
        transport = std::make_unique<SseTransport>(url);
    } else if (type == "http") {
        String url = config.value("url", "");
        int timeout = config.value("timeout", 30000);
        transport = std::make_unique<HttpTransport>(url, timeout);
    } else {
        return nullptr;
    }

    return std::make_unique<McpClientImpl>(std::move(transport));
}

} // namespace claude
