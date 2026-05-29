#include <claude/tool/impl/MCPTool.hpp>
#include <spdlog/spdlog.h>

namespace claude {

MCPTool::MCPTool(std::shared_ptr<McpManager> manager,
                 const String& serverName,
                 const McpTool& mcpTool)
    : manager_(std::move(manager))
    , serverName_(serverName)
    , originalToolName_(mcpTool.name)
    , mcpTool_(mcpTool)
    , readOnly_(false)
{
    // Detect read-only from tool annotations if available
    // MCP tools with "readOnlyHint: true" in annotations are read-only
    if (mcpTool_.inputSchema.contains("annotations")) {
        auto& ann = mcpTool_.inputSchema["annotations"];
        readOnly_ = ann.value("readOnlyHint", false);
    }
}

String MCPTool::name() const {
    // Use server-prefixed name from McpManager::getAllTools()
    return serverName_ + "_" + originalToolName_;
}

String MCPTool::description() const {
    return mcpTool_.description;
}

String MCPTool::inputSchema() const {
    return mcpTool_.inputSchema.dump();
}

String MCPTool::execute(const Json& input, ToolContext& context) {
    if (!manager_) {
        return "Error: MCP manager is not available";
    }

    spdlog::debug("MCPTool [{}] calling server '{}' tool '{}'",
                  name(), serverName_, originalToolName_);

    try {
        String result = manager_->callTool(serverName_, originalToolName_, input);
        return result;
    } catch (const std::exception& e) {
        String err = "MCP tool execution failed: " + String(e.what());
        spdlog::error("{}", err);
        return "Error: " + err;
    }
}

bool MCPTool::isReadOnly() const {
    return readOnly_;
}

bool MCPTool::isConcurrencySafe(const Json& input) const {
    return readOnly_;
}

bool MCPTool::isDestructive(const Json& input) const {
    return !readOnly_;
}

size_t MCPTool::maxResultSizeChars() const {
    return 50000;
}

String MCPTool::activityDescription(const Json& input) const {
    return "Calling MCP tool: " + name();
}

String MCPTool::userFacingName() const {
    return name();
}

std::vector<ToolPtr> MCPTool::createAllFromManager(std::shared_ptr<McpManager> manager) {
    std::vector<ToolPtr> tools;

    if (!manager) {
        spdlog::warn("MCPTool::createAllFromManager: manager is null");
        return tools;
    }

    // Get tools from each server separately so we know the server name
    auto allTools = manager->getAllTools();

    // We need the server names — iterate servers via a workaround
    // McpManager::getAllTools() prefixes names as "server:tool"
    // We need to split them back
    for (const auto& mcpTool : allTools) {
        // The name is "serverName:originalToolName" from McpManager::getAllTools()
        String fullName = mcpTool.name;
        auto colonPos = fullName.find(':');
        if (colonPos == String::npos) {
            spdlog::warn("MCPTool: unexpected tool name format: {}", fullName);
            continue;
        }

        String serverName = fullName.substr(0, colonPos);
        String originalName = fullName.substr(colonPos + 1);

        // Create a clean McpTool without the prefix
        McpTool cleanTool = mcpTool;
        cleanTool.name = originalName;

        tools.push_back(std::make_unique<MCPTool>(manager, serverName, cleanTool));
        spdlog::debug("MCPTool: registered '{}' from server '{}'", originalName, serverName);
    }

    spdlog::debug("MCPTool: created {} dynamic MCP tools", tools.size());
    return tools;
}

} // namespace claude
