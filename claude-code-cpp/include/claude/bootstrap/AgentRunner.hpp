#pragma once

#include <claude/core/Types.hpp>
#include <claude/permission/PermissionTypes.hpp>
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace claude {

class ApiClient;
class AgentLoop;
class ToolRegistry;
class AppConfig;
class TokenTracker;
class ContextInjector;
class ClaudeMdLoader;
class PermissionSettings;
class RuleEngine;
class CommandRegistry;
class McpManager;
class Spinner;
class FtxuiRepl;

namespace agent_runner {

/// Parameters for creating an API client — extracted from CLI args and config.
struct ApiClientParams {
    String provider;
    String model;
    String apiKey;
    String baseUrl;
    int maxTokens = 0;
};

/// Create and configure an API client. Returns both the owning pointer and raw pointer.
struct ApiClientHolder {
    std::unique_ptr<ApiClient> owned;
    ApiClient* raw = nullptr;
};
ApiClientHolder createApiClient(const ApiClientParams& params);

/// Parameters for creating an AgentLoop.
/// Uses pointers instead of references so the struct is default-constructible
/// and assignable from main.cpp's unique_ptr-managed objects.
struct AgentLoopParams {
    ApiClient* apiClient = nullptr;
    ToolRegistry* toolRegistry = nullptr;
    AppConfig* config = nullptr;
    PermissionSettings* permissionSettings = nullptr;
    RuleEngine* permissionEngine = nullptr;
    String systemPromptOverride;
    String appendSystemPrompt;
    String provider;        // from CLI --provider
    String model;           // from CLI --model
    std::vector<String> allowedTools;
    std::vector<String> disallowedTools;
    int maxTurns = 0;
    bool continueSession = false;
    bool interactive = false;
};

/// Result of creating an AgentLoop — includes all owned sub-objects that the loop needs.
struct AgentLoopHolder {
    std::unique_ptr<AgentLoop> loop;
    std::unique_ptr<TokenTracker> tokenTracker;
    std::unique_ptr<ContextInjector> contextInjector;
    std::unique_ptr<ClaudeMdLoader> claudeMdLoader;
};
AgentLoopHolder createAgentLoop(const AgentLoopParams& params);

/// Set up all AgentLoop callbacks (tool events, streaming, thinking, etc.).
/// The permissionCallback is required — it's called when the agent needs a permission decision.
/// Other callbacks are set up based on whether useFtxui is true (FTXUI mode) or false (readline mode).
void setupCallbacks(AgentLoop& loop,
                     bool useFtxui,
                     Spinner* spinner,
                     FtxuiRepl* ftxuiRepl,
                     std::function<PermissionChoice(const PermissionRequest&)> permissionCallback);

/// Resume the most recent session from ~/.claude/sessions/ into the given AgentLoop.
/// Returns true if a session was resumed, false otherwise.
bool resumeLastSession(AgentLoop& loop);

/// Initialize MCP servers from ~/.claude/mcp_settings.json and register their tools.
/// Returns the McpManager (shared_ptr) if any servers were started, nullptr otherwise.
std::shared_ptr<McpManager> initMcp(ToolRegistry& tools);

} // namespace agent_runner

} // namespace claude
