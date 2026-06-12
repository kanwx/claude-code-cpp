#pragma once

#include "../core/Types.hpp"
#include "../constants/Prompts.hpp"
#include "../context/GitContext.hpp"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <map>
#include <mutex>
#include <chrono>

namespace claude {

// ========== SDK Prefix Constants ==========

/// Default CLI prefix: standard interactive mode
extern const String DEFAULT_PREFIX;

/// Agent SDK prefix with Claude Code preset: non-interactive with append prompt
extern const String AGENT_SDK_CLAUDE_CODE_PRESET_PREFIX;

/// Agent SDK prefix: non-interactive without append prompt
extern const String AGENT_SDK_PREFIX;

/// All known CLI sysprompt prefix values (for split detection)
extern const std::vector<String> CLI_SYSPROMPT_PREFIX_VALUES;

/// Session mode for prefix selection
enum class SessionMode {
    Interactive,             // DEFAULT_PREFIX
    NonInteractiveWithAppend, // AGENT_SDK_CLAUDE_CODE_PRESET_PREFIX
    NonInteractive           // AGENT_SDK_PREFIX
};

/// Get the appropriate CLI sysprompt prefix based on session mode
String getCLISyspromptPrefix(SessionMode mode);

// ========== Section Registry ==========

/// Compute function for a system prompt section
using SectionComputeFn = std::function<String()>;

/// A named system prompt section with caching control
struct SystemPromptSection {
    String name;                     // Section identifier (for caching)
    SectionComputeFn compute;        // Compute the section content
    bool cacheBreak;                 // If true, recompute every turn (breaks cache)
};

/// Create a memoized system prompt section.
/// Computed once, cached until /clear or /compact.
SystemPromptSection systemPromptSection(
    const String& name,
    SectionComputeFn compute);

/// Create a volatile system prompt section that recomputes every turn.
/// This WILL break the prompt cache when the value changes.
/// The reason parameter documents why cache-breaking is necessary.
SystemPromptSection DANGEROUS_uncachedSystemPromptSection(
    const String& name,
    SectionComputeFn compute,
    const String& reason);

/// Resolve all system prompt sections, returning prompt strings.
/// Uses cached values for non-cache-breaking sections.
std::vector<String> resolveSystemPromptSections(
    const std::vector<SystemPromptSection>& sections);

/// Clear all section cache entries. Called on /clear and /compact.
void clearSectionCache();

// ========== Agent Definition (for Tier 2) ==========

/// Agent definition used for main-thread agent system prompts
struct AgentDefinition {
    String agentType;                // e.g., "explore", "plan", "verification"
    String systemPrompt;             // The agent's system prompt text
    String whenToUse;                // Description of when to use this agent
    std::vector<String> tools;       // Allowed tools (empty = all)
    std::vector<String> disallowedTools; // Blocked tools
    bool isBuiltIn = false;          // Whether this is a built-in agent
    String memory;                   // Memory scope (if any)
    int maxTurns = 50;               // Max iterations
    String model;                    // Model override (empty = default)
};

// ========== Coordinator Mode ==========

/// Check if coordinator mode is active via CLAUDE_CODE_COORDINATOR_MODE env var
bool isCoordinatorMode();

/// Get the coordinator system prompt (worker orchestration instructions)
String getCoordinatorSystemPrompt();

// ========== Proactive Mode ==========

/// Check if proactive mode is active
bool isProactiveActive();

/// Get the proactive section (autonomous work instructions with pacing/sleep/bias)
String getProactiveSection();

// ========== CLAUDE_CODE_SIMPLE Mode ==========

/// Check if CLAUDE_CODE_SIMPLE mode is active (--bare flag)
bool isSimpleMode();

/// Build the minimal one-liner prompt for SIMPLE mode
std::vector<String> buildSimpleModePrompt(const EnvironmentInfo& env);

// ========== Subagent Prompt Enhancement ==========

/// Enhance a subagent prompt with environment details, absolute path notes, emoji avoidance
String enhanceSubagentPrompt(
    const String& basePrompt,
    const EnvironmentInfo& env,
    bool isFork);

/// Build the fork child message (directive-style prompt for forked workers)
String buildForkChildMessage(const String& directive);

/// Build the old-style subagent preamble (for non-fork delegation)
String buildSubagentPreamble(
    const String& agentType,
    const EnvironmentInfo& env);

// ========== 5-Tier Priority Resolution ==========

/// Build the effective system prompt based on 5-tier priority:
///
/// Tier 0: overrideSystemPrompt -- if set, REPLACES everything, no append
/// Tier 1: Coordinator prompt -- if coordinator mode active and no agent
/// Tier 2: Agent prompt -- if mainThreadAgentType set, replaces default
///         (except proactive mode where it appends)
/// Tier 3: customSystemPrompt -- if set via --system-prompt, replaces default
/// Tier 4: Default system prompt -- the standard getSystemPrompt() output
///
/// After tier selection, appendSystemPrompt is appended (except when override)
///
/// @param overrideSystemPrompt  Tier 0: complete replacement
/// @param coordinatorMode       Tier 1: is coordinator mode active
/// @param mainThreadAgentType   Tier 2: agent type name (looked up in registry)
/// @param proactiveMode         Whether proactive mode is active (affects Tier 2)
/// @param customSystemPrompt    Tier 3: --system-prompt / SDK option
/// @param appendSystemPrompt    Always appended after resolution (except override)
/// @return Vector of prompt strings (the effective system prompt)
std::vector<String> buildEffectiveSystemPrompt(
    const std::optional<String>& overrideSystemPrompt,
    bool coordinatorMode,
    const std::optional<String>& mainThreadAgentType,
    bool proactiveMode,
    const std::optional<String>& customSystemPrompt,
    const std::optional<String>& appendSystemPrompt);

// ========== System Prompt Builder (Builder Pattern) ==========

/// System prompt builder - matches original TS implementation
/// Constructs the "default" system prompt (Tier 4) and provides
/// section registry, caching, and block-based construction for prompt caching.
class SystemPromptBuilder {
public:
    /// Build system prompt as a single string
    String build();

    /// Build system prompt as blocks (for caching)
    std::vector<TextBlockParam> buildBlocks();

    /// Build the default system prompt as a vector of strings
    /// (for use by buildEffectiveSystemPrompt Tier 4)
    std::vector<String> buildDefaultSystemPromptVector();

    // --- Builder setters ---

    SystemPromptBuilder& withClaudeMd(const String& content);
    SystemPromptBuilder& withGitContext(const GitContext& ctx);
    SystemPromptBuilder& withWorkDir(const String& dir);
    SystemPromptBuilder& withMemoryPath(const String& path);
    SystemPromptBuilder& withAdditionalContext(const String& context);
    SystemPromptBuilder& withRagContext(const String& context);
    SystemPromptBuilder& withAutoRag(const String& query, int maxDocs = 5);
    SystemPromptBuilder& withEnvironment(const EnvironmentInfo& env);
    SystemPromptBuilder& withSettings(const PromptSettings& settings);
    SystemPromptBuilder& withEnabledTools(const std::vector<ToolInfo>& tools);
    SystemPromptBuilder& withMcpServers(const std::vector<McpServerInfo>& servers);
    SystemPromptBuilder& withOutputStyle(const std::optional<OutputStyleConfig>& style);
    SystemPromptBuilder& withGlobalCache(bool useGlobalCache);
    SystemPromptBuilder& withNonInteractive(bool nonInteractive);
    SystemPromptBuilder& withReplMode(bool replMode);

    // --- Session mode for prefix selection ---
    SystemPromptBuilder& withSessionMode(SessionMode mode);

    // --- Section registry ---

    /// Add a dynamic section to the builder.
    /// Sections are resolved in order and inserted at the dynamic boundary.
    SystemPromptBuilder& withDynamicSection(const SystemPromptSection& section);

    /// Clear dynamic sections
    SystemPromptBuilder& clearDynamicSections();

    /// Whether RAG is enabled
    static bool isRagEnabled();

private:
    String claudeMd_;
    GitContext gitCtx_;
    String workDir_;
    String ragContext_;
    String additionalContext_;
    String memoryPath_;

    EnvironmentInfo env_;
    PromptSettings settings_;
    std::vector<ToolInfo> enabledTools_;
    std::vector<McpServerInfo> mcpServers_;
    std::optional<OutputStyleConfig> outputStyle_;
    bool useGlobalCache_ = true;
    bool isNonInteractive_ = false;
    bool isReplMode_ = false;
    SessionMode sessionMode_ = SessionMode::Interactive;

    /// Dynamic sections (resolved at build time, some cached, some volatile)
    std::vector<SystemPromptSection> dynamicSections_;

    /// Build the PromptContext from current state
    PromptContext buildContext();

    /// Build the static sections of the system prompt
    std::vector<String> buildStaticSections();

    /// Build the resolved dynamic sections
    std::vector<String> buildResolvedDynamicSections();
};

} // namespace claude
