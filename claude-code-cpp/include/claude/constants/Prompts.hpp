#pragma once

#include "../core/Types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace claude {

// ========== System Prompt Types ==========

/// Output style configuration
struct OutputStyleConfig {
    String name;
    String prompt;
    bool keepCodingInstructions = true;
};

/// Settings for prompt generation
struct PromptSettings {
    String language = "auto";           // auto | en | zh | ja | ko
    String outputStyle = "default";     // default | compact | verbose
    bool colorEnabled = true;
    bool vimMode = false;
};

/// Environment info
struct EnvironmentInfo {
    String cwd;
    bool isGit = false;
    bool isWorktree = false;
    String platform;
    String shell;
    String osVersion;
    String modelId;
    String modelName;
    std::vector<String> additionalWorkingDirs;
};

/// MCP server info for instructions
struct McpServerInfo {
    String name;
    String instructions;
};

/// Tool info for enabled tools
struct ToolInfo {
    String name;
    String description;
};

/// Context for building system prompt
struct PromptContext {
    PromptSettings settings;
    EnvironmentInfo env;
    std::vector<ToolInfo> enabledTools;
    std::vector<McpServerInfo> mcpServers;
    std::optional<OutputStyleConfig> outputStyleConfig;
    bool useGlobalCache = true;
    bool isNonInteractive = false;
    bool isReplMode = false;
};

// ========== Section Functions ==========

/// Get intro section
String getIntroSection(const std::optional<OutputStyleConfig>& outputStyle);

/// Get system section
String getSystemSection();

/// Get doing tasks section
String getDoingTasksSection();

/// Get actions section
String getActionsSection();

/// Get using your tools section
String getUsingYourToolsSection(const std::vector<ToolInfo>& enabledTools, bool isReplMode);

/// Get tone and style section
String getToneAndStyleSection();

/// Get output efficiency section
String getOutputEfficiencySection();

/// Get environment info section
String getEnvironmentInfoSection(const EnvironmentInfo& env);

/// Get language section
String getLanguageSection(const String& language);

/// Get output style section
String getOutputStyleSection(const std::optional<OutputStyleConfig>& config);

/// Get MCP instructions section
String getMcpInstructionsSection(const std::vector<McpServerInfo>& servers);

/// Get session-specific guidance section
String getSessionGuidanceSection(const PromptContext& ctx);

/// Get hooks section
String getHooksSection();

/// Get system reminders section
String getSystemRemindersSection();

/// Get tool result summarization section
String getToolResultSummarizationSection();

/// Get memory section (conditional on MEMORY.md existence)
String getMemorySection();

/// Get scratchpad section
String getScratchpadSection();

/// Get frc (function result clearing) section
String getFrcSection();

// ========== Main Prompt Builder ==========

/// Build complete system prompt as blocks (for caching)
std::vector<TextBlockParam> buildSystemPromptBlocks(const PromptContext& ctx);

/// Build complete system prompt as string (simple mode)
String buildSystemPromptString(const PromptContext& ctx);

/// Build simple environment info
String buildSimpleEnvInfo(const EnvironmentInfo& env);

// ========== Utility Functions ==========

/// Prepend bullets to items
std::vector<String> prependBullets(const std::vector<String>& items);

/// Check if should use global cache scope
bool shouldUseGlobalCacheScope();

/// Get dynamic boundary marker
String getDynamicBoundaryMarker();

// ========== Constants ==========

/// Dynamic boundary marker for cache splitting
extern const String SYSTEM_PROMPT_DYNAMIC_BOUNDARY;

/// Cyber risk instruction
extern const String CYBER_RISK_INSTRUCTION;

/// Tool names (for reference in prompts)
extern const String FILE_READ_TOOL_NAME;
extern const String FILE_EDIT_TOOL_NAME;
extern const String FILE_WRITE_TOOL_NAME;
extern const String GLOB_TOOL_NAME;
extern const String GREP_TOOL_NAME;
extern const String BASH_TOOL_NAME;
extern const String AGENT_TOOL_NAME;
extern const String ASK_USER_QUESTION_TOOL_NAME;

} // namespace claude
