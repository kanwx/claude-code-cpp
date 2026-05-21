#pragma once


#include "../core/Types.hpp"

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace claude {

/// MCP 工具定义
struct McpTool {
    String name;
    String description;
    Json inputSchema;
};

/// MCP 资源
struct McpResource {
    String uri;
    String name;
    String description;
};

/// MCP 客户端
class McpClient {
public:
    virtual ~McpClient() = default;

    /// 连接
    virtual bool connect() = 0;

    /// 断开
    virtual void disconnect() = 0;

    /// 获取工具列表
    virtual std::vector<McpTool> listTools() = 0;

    /// 获取资源列表
    virtual std::vector<McpResource> listResources() = 0;

    /// 调用工具
    virtual String callTool(const String& name, const Json& args) = 0;

    /// 读取资源
    virtual String readResource(const String& uri) = 0;
};

/// 创建 MCP 客户端 (通过命令启动)
std::unique_ptr<McpClient> createMcpClient(const String& command);

/// 创建 MCP 客户端 (通过配置, 支持 stdio/sse/http)
/// config: { "type": "sse", "url": "http://host:port/sse" }
///         { "type": "http", "url": "http://host:port/mcp", "timeout": 30000 }
///         { "type": "stdio", "command": "...", "args": [...] }
std::unique_ptr<McpClient> createMcpClientFromConfig(const Json& config);

} // namespace claude
