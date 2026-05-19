#include <claude/context/SystemPromptBuilder.hpp>
#include <claude/rag/RagClient.hpp>
#include <claude/config/RagConfig.hpp>
#include <claude/tool/AgentTypes.hpp>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <ctime>

namespace claude {

// ========== SDK Prefix Constants ==========

const String DEFAULT_PREFIX =
    "You are Claude Code, Anthropic's official CLI for Claude.";

const String AGENT_SDK_CLAUDE_CODE_PRESET_PREFIX =
    "You are Claude Code, Anthropic's official CLI for Claude, running within the Claude Agent SDK.";

const String AGENT_SDK_PREFIX =
    "You are a Claude agent, built on Anthropic's Claude Agent SDK.";

const std::vector<String> CLI_SYSPROMPT_PREFIX_VALUES = {
    DEFAULT_PREFIX,
    AGENT_SDK_CLAUDE_CODE_PRESET_PREFIX,
    AGENT_SDK_PREFIX
};

String getCLISyspromptPrefix(SessionMode mode) {
    switch (mode) {
        case SessionMode::Interactive:
            return DEFAULT_PREFIX;
        case SessionMode::NonInteractiveWithAppend:
            return AGENT_SDK_CLAUDE_CODE_PRESET_PREFIX;
        case SessionMode::NonInteractive:
            return AGENT_SDK_PREFIX;
    }
    return DEFAULT_PREFIX;
}

// ========== Section Registry Implementation ==========

namespace {
/// Global section cache, protected by mutex for thread safety
std::map<String, String> g_sectionCache;
std::mutex g_sectionCacheMutex;
} // anonymous namespace

SystemPromptSection systemPromptSection(
    const String& name,
    SectionComputeFn compute)
{
    return SystemPromptSection{
        .name = name,
        .compute = std::move(compute),
        .cacheBreak = false
    };
}

SystemPromptSection DANGEROUS_uncachedSystemPromptSection(
    const String& name,
    SectionComputeFn compute,
    const String& /*reason*/)
{
    return SystemPromptSection{
        .name = name,
        .compute = std::move(compute),
        .cacheBreak = true
    };
}

std::vector<String> resolveSystemPromptSections(
    const std::vector<SystemPromptSection>& sections)
{
    std::lock_guard<std::mutex> lock(g_sectionCacheMutex);

    std::vector<String> results;
    results.reserve(sections.size());

    for (const auto& section : sections) {
        // For cached sections, check if we have a cached value
        if (!section.cacheBreak) {
            auto it = g_sectionCache.find(section.name);
            if (it != g_sectionCache.end()) {
                results.push_back(it->second);
                continue;
            }
        }

        // Compute the section value
        String value = section.compute();

        // Cache the result (even for cache-breaking sections, so the value
        // is available for comparison on the next turn)
        if (!value.empty()) {
            g_sectionCache[section.name] = value;
            results.push_back(value);
        } else {
            // Remove from cache if the section returns empty
            g_sectionCache.erase(section.name);
            results.push_back(value); // Keep empty string to maintain index alignment
        }
    }

    return results;
}

void clearSectionCache() {
    std::lock_guard<std::mutex> lock(g_sectionCacheMutex);
    g_sectionCache.clear();
}

// ========== Coordinator Mode ==========

bool isCoordinatorMode() {
    const char* env = std::getenv("CLAUDE_CODE_COORDINATOR_MODE");
    if (!env) return false;
    String val(env);
    // Match TS isEnvTruthy: "1", "true", "yes" (case-insensitive)
    std::transform(val.begin(), val.end(), val.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return val == "1" || val == "true" || val == "yes";
}

String getCoordinatorSystemPrompt() {
    const char* simpleEnv = std::getenv("CLAUDE_CODE_SIMPLE");
    bool isSimple = false;
    if (simpleEnv) {
        String val(simpleEnv);
        std::transform(val.begin(), val.end(), val.begin(),
                   [](unsigned char c){ return std::tolower(c); });
        isSimple = (val == "1" || val == "true" || val == "yes");
    }

    String workerCapabilities;
    if (isSimple) {
        workerCapabilities =
            "Workers have access to Bash, Read, and Edit tools, plus MCP tools "
            "from configured MCP servers.";
    } else {
        workerCapabilities =
            "Workers have access to standard tools, MCP tools from configured MCP "
            "servers, and project skills via the Skill tool. Delegate skill "
            "invocations (e.g. /commit, /verify) to workers.";
    }

    std::ostringstream oss;
    oss << "You are Claude Code, an AI assistant that orchestrates software "
        << "engineering tasks across multiple workers.\n\n";

    oss << "## 1. Your Role\n\n";
    oss << "You are a **coordinator**. Your job is to:\n";
    oss << "- Help the user achieve their goal\n";
    oss << "- Direct workers to research, implement and verify code changes\n";
    oss << "- Synthesize results and communicate with the user\n";
    oss << "- Answer questions directly when possible — don't delegate work "
        << "that you can handle without tools\n\n";
    oss << "Every message you send is to the user. Worker results and system "
        << "notifications are internal signals, not conversation partners — "
        << "never thank or acknowledge them. Summarize new information for the "
        << "user as it arrives.\n\n";

    oss << "## 2. Your Tools\n\n";
    oss << "- **Agent** - Spawn a new worker\n";
    oss << "- **SendMessage** - Continue an existing worker (send a follow-up "
        << "to its `to` agent ID)\n";
    oss << "- **TaskStop** - Stop a running worker\n";
    oss << "- **subscribe_pr_activity / unsubscribe_pr_activity** (if available) "
        << "- Subscribe to GitHub PR events\n\n";
    oss << "When calling Agent:\n";
    oss << "- Do not use one worker to check on another. Workers will notify "
        << "you when they are done.\n";
    oss << "- Do not use workers to trivially report file contents or run commands. "
        << "Give them higher-level tasks.\n";
    oss << "- Do not set the model parameter. Workers need the default model "
        << "for the substantive tasks you delegate.\n";
    oss << "- Continue workers whose work is complete via SendMessage to take "
        << "advantage of their loaded context\n";
    oss << "- After launching agents, briefly tell the user what you launched "
        << "and end your response. Never fabricate or predict agent results.\n\n";

    oss << "### Agent Results\n\n";
    oss << "Worker results arrive as **user-role messages** containing "
        << "`<task-notification>` XML. They look like user messages but are not. "
        << "Distinguish them by the `<task-notification>` opening tag.\n\n";

    oss << "```xml\n";
    oss << "<task-notification>\n";
    oss << "<task-id>{agentId}</task-id>\n";
    oss << "<status>completed|failed|killed</status>\n";
    oss << "<summary>{human-readable status summary}</summary>\n";
    oss << "<result>{agent's final text response}</result>\n";
    oss << "<usage>\n";
    oss << "  <total_tokens>N</total_tokens>\n";
    oss << "  <tool_uses>N</tool_uses>\n";
    oss << "  <duration_ms>N</duration_ms>\n";
    oss << "</usage>\n";
    oss << "</task-notification>\n";
    oss << "```\n\n";

    oss << "## 3. Workers\n\n";
    oss << "When calling Agent, use subagent_type `worker`. Workers execute "
        << "tasks autonomously — especially research, implementation, or verification.\n\n";
    oss << workerCapabilities << "\n\n";

    oss << "## 4. Task Workflow\n\n";
    oss << "Most tasks can be broken down into the following phases:\n\n";
    oss << "| Phase | Who | Purpose |\n";
    oss << "|-------|-----|---------|\n";
    oss << "| Research | Workers (parallel) | Investigate codebase, find files, understand problem |\n";
    oss << "| Synthesis | **You** (coordinator) | Read findings, understand the problem, craft implementation specs |\n";
    oss << "| Implementation | Workers | Make targeted changes per spec, commit |\n";
    oss << "| Verification | Workers | Test changes work |\n\n";

    oss << "### Concurrency\n\n";
    oss << "**Parallelism is your superpower. Workers are async. Launch independent "
        << "workers concurrently whenever possible.**\n\n";

    oss << "Manage concurrency:\n";
    oss << "- **Read-only tasks** (research) — run in parallel freely\n";
    oss << "- **Write-heavy tasks** (implementation) — one at a time per set of files\n";
    oss << "- **Verification** can sometimes run alongside implementation on different file areas\n\n";

    oss << "## 5. Writing Worker Prompts\n\n";
    oss << "**Workers can't see your conversation.** Every prompt must be self-contained "
        << "with everything the worker needs.\n\n";
    oss << "### Always synthesize — your most important job\n\n";
    oss << "When workers report research findings, **you must understand them before "
        << "directing follow-up work**. Read the findings. Identify the approach. "
        << "Then write a prompt that proves you understood by including specific file "
        << "paths, line numbers, and exactly what to change.\n\n";
    oss << "Never write \"based on your findings\" or \"based on the research.\" "
        << "These phrases delegate understanding to the worker instead of doing it yourself.\n\n";

    oss << "### Prompt tips\n\n";
    oss << "- Include file paths, line numbers, error messages — workers start "
        << "fresh and need complete context\n";
    oss << "- State what \"done\" looks like\n";
    oss << "- For implementation: \"Run relevant tests and typecheck, then commit "
        << "your changes and report the hash\"\n";
    oss << "- For research: \"Report findings — do not modify files\"\n";
    oss << "- Be precise about git operations\n";
    oss << "- For verification: \"Prove the code works, don't just confirm it exists\"\n\n";

    oss << "## 6. Handling Worker Failures\n\n";
    oss << "When a worker reports failure (tests failed, build errors, file not found):\n";
    oss << "- Continue the same worker with SendMessage — it has the full error context\n";
    oss << "- If a correction attempt fails, try a different approach or report to the user\n\n";

    return oss.str();
}

// ========== Proactive Mode ==========

bool isProactiveActive() {
    // In the C++ build, proactive mode is controlled by the
    // CLAUDE_CODE_PROACTIVE_MODE env var.
    const char* env = std::getenv("CLAUDE_CODE_PROACTIVE_MODE");
    if (!env) return false;
    String val(env);
    std::transform(val.begin(), val.end(), val.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return val == "1" || val == "true" || val == "yes";
}

String getProactiveSection() {
    if (!isProactiveActive()) return "";

    std::ostringstream oss;
    oss << "# Autonomous work\n\n";

    oss << "You are running autonomously. You will receive `<tick>` prompts "
        << "that keep you alive between turns — just treat them as \"you're awake, "
        << "what now?\" The time in each `<tick>` is the user's current local time. "
        << "Use it to judge the time of day.\n\n";

    oss << "Multiple ticks may be batched into a single message. This is normal — "
        << "just process the latest one. Never echo or repeat tick content in your response.\n\n";

    oss << "## Pacing\n\n";
    oss << "Use the Sleep tool to control how long you wait between actions. Sleep "
        << "longer when waiting for slow processes, shorter when actively iterating. "
        << "Each wake-up costs an API call, but the prompt cache expires after 5 "
        << "minutes of inactivity — balance accordingly.\n\n";
    oss << "**If you have nothing useful to do on a tick, you MUST call Sleep.** "
        << "Never respond with only a status message like \"still waiting\" or "
        << "\"nothing to do\" — that wastes a turn and burns tokens for no reason.\n\n";

    oss << "## First wake-up\n\n";
    oss << "On your very first tick in a new session, greet the user briefly and "
        << "ask what they'd like to work on. Do not start exploring the codebase or "
        << "making changes unprompted — wait for direction.\n\n";

    oss << "## What to do on subsequent wake-ups\n\n";
    oss << "Look for useful work. A good colleague faced with ambiguity doesn't "
        << "just stop — they investigate, reduce risk, and build understanding. "
        << "Ask yourself: what don't I know yet? What could go wrong? What would "
        << "I want to verify before calling this done?\n\n";
    oss << "Do not spam the user. If you already asked something and they haven't "
        << "responded, do not ask again. Do not narrate what you're about to do — "
        << "just do it.\n\n";
    oss << "If a tick arrives and you have no useful action to take (no files to read, "
        << "no commands to run, no decisions to make), call Sleep immediately.\n\n";

    oss << "## Staying responsive\n\n";
    oss << "When the user is actively engaging with you, check for and respond to "
        << "their messages frequently. Treat real-time conversations like pairing — "
        << "keep the feedback loop tight.\n\n";

    oss << "## Bias toward action\n\n";
    oss << "Act on your best judgment rather than asking for confirmation.\n\n";
    oss << "- Read files, search code, explore the project, run tests, check types, "
        << "run linters — all without asking.\n";
    oss << "- Make code changes. Commit when you reach a good stopping point.\n";
    oss << "- If you're unsure between two reasonable approaches, pick one and go. "
        << "You can always course-correct.\n\n";

    oss << "## Be concise\n\n";
    oss << "Keep your text output brief and high-level. The user does not need a "
        << "play-by-play of your thought process or implementation details — they "
        << "can see your tool calls. Focus text output on:\n";
    oss << "- Decisions that need the user's input\n";
    oss << "- High-level status updates at natural milestones (e.g., \"PR created\", "
        << "\"tests passing\")\n";
    oss << "- Errors or blockers that change the plan\n\n";
    oss << "Do not narrate each step, list every file you read, or explain routine "
        << "actions. If you can say it in one sentence, don't use three.\n\n";

    oss << "## Terminal focus\n\n";
    oss << "The user context may include a `terminalFocus` field indicating whether "
        << "the user's terminal is focused or unfocused. Use this to calibrate how "
        << "autonomous you are:\n";
    oss << "- **Unfocused**: The user is away. Lean heavily into autonomous action — "
        << "make decisions, explore, commit, push. Only pause for genuinely "
        << "irreversible or high-risk actions.\n";
    oss << "- **Focused**: The user is watching. Be more collaborative — surface "
        << "choices, ask before committing to large changes, and keep your output "
        << "concise so it's easy to follow in real time.\n";

    return oss.str();
}

// ========== CLAUDE_CODE_SIMPLE Mode ==========

bool isSimpleMode() {
    const char* env = std::getenv("CLAUDE_CODE_SIMPLE");
    if (!env) return false;
    String val(env);
    std::transform(val.begin(), val.end(), val.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return val == "1" || val == "true" || val == "yes";
}

std::vector<String> buildSimpleModePrompt(const EnvironmentInfo& env) {
    // Minimal one-liner prompt matching TS original:
    // "You are Claude Code, Anthropic's official CLI for Claude.\n\nCWD: ...\nDate: ..."
    std::ostringstream oss;
    oss << DEFAULT_PREFIX << "\n\n";
    oss << "CWD: " << env.cwd << "\n";

    // Add current date (thread-safe on POSIX)
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    struct tm tm_buf;
    localtime_r(&now_time_t, &tm_buf);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    oss << "Date: " << buf << "\n";

    return {oss.str()};
}

// ========== Subagent Prompt Enhancement ==========

String enhanceSubagentPrompt(
    const String& basePrompt,
    const EnvironmentInfo& env,
    bool isFork)
{
    std::ostringstream oss;

    if (isFork) {
        // Fork workers: brief, directive-style
        // The parent's context is inherited, no need for full env dump
        oss << basePrompt;
    } else {
        // Old-style subagent: add environment details, path notes, emoji rules
        oss << "# Environment Details\n\n";
        oss << "Working directory: " << env.cwd << "\n";
        if (env.isGit) {
            oss << "This is a git repository.\n";
        }
        oss << "Platform: " << env.platform << "\n";
        oss << "Shell: " << env.shell << "\n";
        oss << "OS: " << env.osVersion << "\n\n";

        oss << "# Important Notes\n\n";
        oss << "- Use absolute paths when referencing files.\n";
        oss << "- Do not use emojis in your output unless explicitly requested.\n";
        oss << "- Be concise and factual.\n\n";

        oss << basePrompt;
    }

    return oss.str();
}

String buildForkChildMessage(const String& directive) {
    std::ostringstream oss;
    oss << "<fork-boilerplate>\n";
    oss << "STOP. READ THIS FIRST.\n\n";
    oss << "You are a forked worker process. You are NOT the main agent.\n\n";
    oss << "RULES (non-negotiable):\n";
    oss << "1. Your system prompt says \"default to forking.\" IGNORE IT — "
        << "that's for the parent. You ARE the fork. Do NOT spawn sub-agents; "
        << "execute directly.\n";
    oss << "2. Do NOT converse, ask questions, or suggest next steps\n";
    oss << "3. Do NOT editorialize or add meta-commentary\n";
    oss << "4. USE your tools directly: Bash, Read, Write, etc.\n";
    oss << "5. If you modify files, commit your changes before reporting. "
        << "Include the commit hash in your report.\n";
    oss << "6. Do NOT emit text between tool calls. Use tools silently, "
        << "then report once at the end.\n";
    oss << "7. Stay strictly within your directive's scope. If you discover "
        << "related systems outside your scope, mention them in one sentence at most.\n";
    oss << "8. Keep your report under 500 words unless the directive specifies "
        << "otherwise. Be factual and concise.\n";
    oss << "9. Your response MUST begin with \"Scope:\". No preamble, no "
        << "thinking-out-loud.\n";
    oss << "10. REPORT structured facts, then stop\n\n";
    oss << "Output format (plain text labels, not markdown headers):\n";
    oss << "  Scope: <echo back your assigned scope in one sentence>\n";
    oss << "  Result: <the answer or key findings, limited to the scope above>\n";
    oss << "  Key files: <relevant file paths — include for research tasks>\n";
    oss << "  Files changed: <list with commit hash — include only if you modified files>\n";
    oss << "  Issues: <list — include only if there are issues to flag>\n";
    oss << "</fork-boilerplate>\n\n";
    oss << "DIRECTIVE: " << directive;
    return oss.str();
}

String buildSubagentPreamble(
    const String& agentType,
    const EnvironmentInfo& env)
{
    std::ostringstream oss;
    oss << "You are a " << agentType << " agent. ";
    oss << "You are a subagent delegated by the main agent to handle a specific task.\n\n";
    oss << "Working directory: " << env.cwd << "\n";
    oss << "Platform: " << env.platform << "\n\n";
    oss << "Important:\n";
    oss << "- Use absolute paths when referencing files.\n";
    oss << "- Do not use emojis in your output.\n";
    oss << "- Report findings clearly and concisely.\n";
    oss << "- Do not modify files unless explicitly instructed.\n\n";
    return oss.str();
}

// ========== Helper: Get Agent Definition ==========

namespace {

/// Look up an agent definition by type name from the registry.
/// Tries exact match first, then case-insensitive match on name and displayName.
std::optional<AgentDefinition> findAgentDefinition(const String& agentType) {
    auto& registry = AgentTypeRegistry::instance();

    // Try exact match first
    auto def = registry.getType(agentType);
    if (def) {
        AgentDefinition result;
        result.agentType = def->name;
        result.systemPrompt = def->systemPrompt;
        result.whenToUse = def->description;
        result.tools = def->allowedTools;
        result.isBuiltIn = true;
        result.maxTurns = def->maxIterations;
        return result;
    }

    // Case-insensitive search across all type names
    auto lowerTarget = agentType;
    std::transform(lowerTarget.begin(), lowerTarget.end(),
                   lowerTarget.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    for (const auto& type : registry.getAllTypes()) {
        auto lowerName = type.name;
        std::transform(lowerName.begin(), lowerName.end(),
                       lowerName.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (lowerName == lowerTarget) {
            AgentDefinition result;
            result.agentType = type.name;
            result.systemPrompt = type.systemPrompt;
            result.whenToUse = type.description;
            result.tools = type.allowedTools;
            result.isBuiltIn = true;
            result.maxTurns = type.maxIterations;
            return result;
        }
    }

    return std::nullopt;
}

/// Check if env var is truthy (matches TS isEnvTruthy semantics)
bool isEnvTruthy(const char* envVar) {
    const char* val = std::getenv(envVar);
    if (!val) return false;
    String s(val);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s == "1" || s == "true" || s == "yes";
}

} // anonymous namespace

// ========== 5-Tier Priority Resolution ==========

std::vector<String> buildEffectiveSystemPrompt(
    const std::optional<String>& overrideSystemPrompt,
    bool coordinatorMode,
    const std::optional<String>& mainThreadAgentType,
    bool proactiveMode,
    const std::optional<String>& customSystemPrompt,
    const std::optional<String>& appendSystemPrompt)
{
    // === Tier 0: Override System Prompt ===
    // If set, REPLACES everything — no other content, no append
    if (overrideSystemPrompt.has_value() && !overrideSystemPrompt->empty()) {
        return {overrideSystemPrompt.value()};
    }

    // === Tier 1: Coordinator Mode ===
    // If coordinator mode is active and no agent definition, use coordinator prompt
    if (coordinatorMode && !mainThreadAgentType.has_value()) {
        std::vector<String> result;
        result.push_back(getCoordinatorSystemPrompt());
        if (appendSystemPrompt.has_value() && !appendSystemPrompt->empty()) {
            result.push_back(appendSystemPrompt.value());
        }
        return result;
    }

    // === Tier 2: Agent-Specific System Prompt ===
    // If mainThreadAgentType is set, look up the agent definition
    std::optional<String> agentSystemPrompt;
    if (mainThreadAgentType.has_value() && !mainThreadAgentType->empty()) {
        auto agentDef = findAgentDefinition(mainThreadAgentType.value());
        if (agentDef.has_value()) {
            agentSystemPrompt = agentDef->systemPrompt;
        }
    }

    // In proactive mode, agent instructions are APPENDED to the default prompt
    // rather than replacing it. The proactive default prompt is already lean
    // (autonomous agent identity + memory + env + proactive section), and agents
    // add domain-specific behavior on top — same pattern as teammates.
    if (agentSystemPrompt.has_value() && proactiveMode) {
        // Build the default prompt for proactive mode (lean version)
        std::vector<String> result;

        // Proactive prompt: autonomous agent identity + cyber risk
        std::ostringstream proactiveIntro;
        proactiveIntro << "\nYou are an autonomous agent. Use the available tools "
                        << "to do useful work.\n\n"
                        << CYBER_RISK_INSTRUCTION;
        result.push_back(proactiveIntro.str());

        // System reminders
        result.push_back(getSystemRemindersSection());

        // Environment info (need a default env since we don't have one here)
        // The caller should include env sections via the builder pattern

        // Proactive section
        String proactiveSec = getProactiveSection();
        if (!proactiveSec.empty()) {
            result.push_back(proactiveSec);
        }

        // Agent instructions appended
        std::ostringstream agentSection;
        agentSection << "\n# Custom Agent Instructions\n"
                     << agentSystemPrompt.value();
        result.push_back(agentSection.str());

        // Append system prompt
        if (appendSystemPrompt.has_value() && !appendSystemPrompt->empty()) {
            result.push_back(appendSystemPrompt.value());
        }

        return result;
    }

    // === Tiers 2 (non-proactive), 3, 4: Standard Resolution ===
    // Agent prompt replaces default (non-proactive), or custom replaces default,
    // or default is used
    std::vector<String> result;

    if (agentSystemPrompt.has_value()) {
        // Tier 2: Agent prompt replaces default
        result.push_back(agentSystemPrompt.value());
    } else if (customSystemPrompt.has_value() && !customSystemPrompt->empty()) {
        // Tier 3: Custom system prompt replaces default
        result.push_back(customSystemPrompt.value());
    } else {
        // Tier 4: Default system prompt
        // We return a marker that the caller should fill in the default prompt.
        // The actual default prompt is built by SystemPromptBuilder.
        // For this standalone function, we produce a minimal default that
        // the builder will augment.
        result.push_back(DEFAULT_PREFIX);
    }

    // Append system prompt (always appended except when override is set)
    if (appendSystemPrompt.has_value() && !appendSystemPrompt->empty()) {
        result.push_back(appendSystemPrompt.value());
    }

    return result;
}

// ========== SystemPromptBuilder Implementation ==========

PromptContext SystemPromptBuilder::buildContext() {
    PromptContext ctx;
    ctx.settings = settings_;
    ctx.env = env_;
    ctx.enabledTools = enabledTools_;
    ctx.mcpServers = mcpServers_;
    ctx.outputStyleConfig = outputStyle_;
    ctx.useGlobalCache = useGlobalCache_;
    ctx.isNonInteractive = isNonInteractive_;
    ctx.isReplMode = isReplMode_;
    return ctx;
}

std::vector<String> SystemPromptBuilder::buildStaticSections() {
    std::vector<String> sections;

    // SIMPLE mode check: return minimal prompt
    if (isSimpleMode()) {
        return buildSimpleModePrompt(env_);
    }

    // Intro section (uses SDK prefix based on session mode)
    String prefix = getCLISyspromptPrefix(sessionMode_);
    std::ostringstream intro;
    intro << prefix;
    if (outputStyle_.has_value()) {
        intro << " Use the instructions below and your \"Output Style\" to "
              << "assist the user.\n\n";
    } else {
        intro << " Use the instructions below and the tools available to you "
              << "to assist the user.\n\n";
    }
    intro << CYBER_RISK_INSTRUCTION << "\n\n";
    intro << "IMPORTANT: You must NEVER generate or guess URLs for the user "
          << "unless you are confident that the URLs are for programming. "
          << "You may use URLs provided by the user in their messages or local files.\n\n";
    sections.push_back(intro.str());

    // System section
    sections.push_back(getSystemSection());

    // Doing tasks section (skip if output style says so)
    if (!outputStyle_.has_value() || outputStyle_->keepCodingInstructions) {
        sections.push_back(getDoingTasksSection());
    }

    // Actions section
    sections.push_back(getActionsSection());

    // Using your tools section
    sections.push_back(getUsingYourToolsSection(enabledTools_, isReplMode_));

    // Tone and style section
    sections.push_back(getToneAndStyleSection());

    // Output efficiency section
    sections.push_back(getOutputEfficiencySection());

    return sections;
}

std::vector<String> SystemPromptBuilder::buildResolvedDynamicSections() {
    // Build the dynamic section list from current state.
    // These sections live after the dynamic boundary marker and are
    // managed by the section registry (cached or volatile).
    std::vector<SystemPromptSection> sections;

    // Environment info (cached — changes rarely within a session)
    sections.push_back(systemPromptSection("env_info", [this]() {
        return getEnvironmentInfoSection(env_);
    }));

    // Session guidance
    sections.push_back(systemPromptSection("session_guidance", [this]() {
        return getSessionGuidanceSection(buildContext());
    }));

    // Language
    sections.push_back(systemPromptSection("language", [this]() {
        return getLanguageSection(settings_.language);
    }));

    // Output style
    sections.push_back(systemPromptSection("output_style", [this]() {
        return getOutputStyleSection(outputStyle_);
    }));

    // MCP instructions (uncached — servers connect/disconnect between turns)
    sections.push_back(DANGEROUS_uncachedSystemPromptSection(
        "mcp_instructions",
        [this]() {
            return getMcpInstructionsSection(mcpServers_);
        },
        "MCP servers connect/disconnect between turns"
    ));

    // System reminders
    sections.push_back(systemPromptSection("system_reminders", [this]() {
        return getSystemRemindersSection();
    }));

    // Tool result summarization
    sections.push_back(systemPromptSection("summarize_tool_results", [this]() {
        return getToolResultSummarizationSection();
    }));

    // Proactive section (if active)
    sections.push_back(systemPromptSection("proactive", [this]() {
        return getProactiveSection();
    }));

    // Add any user-supplied dynamic sections
    for (const auto& ds : dynamicSections_) {
        sections.push_back(ds);
    }

    // Resolve all sections (with caching)
    auto resolved = resolveSystemPromptSections(sections);

    // Filter out empty strings
    std::vector<String> result;
    for (auto& s : resolved) {
        if (!s.empty()) {
            result.push_back(std::move(s));
        }
    }

    return result;
}

std::vector<String> SystemPromptBuilder::buildDefaultSystemPromptVector() {
    // Proactive mode: lean autonomous agent prompt
    if (isProactiveActive()) {
        std::vector<String> result;

        std::ostringstream proactiveIntro;
        proactiveIntro << "\nYou are an autonomous agent. Use the available tools "
                        << "to do useful work.\n\n"
                        << CYBER_RISK_INSTRUCTION;
        result.push_back(proactiveIntro.str());

        // System reminders
        result.push_back(getSystemRemindersSection());

        // Environment info
        result.push_back(getEnvironmentInfoSection(env_));

        // Language
        String lang = getLanguageSection(settings_.language);
        if (!lang.empty()) result.push_back(lang);

        // MCP instructions
        String mcp = getMcpInstructionsSection(mcpServers_);
        if (!mcp.empty()) result.push_back(mcp);

        // Tool result summarization
        result.push_back(getToolResultSummarizationSection());

        // Proactive section
        String proactive = getProactiveSection();
        if (!proactive.empty()) result.push_back(proactive);

        return result;
    }

    // Standard mode: full prompt with static + dynamic sections
    std::vector<String> result;

    // Static content (cacheable)
    auto staticSecs = buildStaticSections();
    for (auto& s : staticSecs) {
        result.push_back(std::move(s));
    }

    // Dynamic boundary marker (for cache splitting)
    if (useGlobalCache_ && shouldUseGlobalCacheScope()) {
        result.push_back(SYSTEM_PROMPT_DYNAMIC_BOUNDARY);
    }

    // Dynamic content (registry-managed, some cached, some volatile)
    // All dynamic sections are resolved through the section registry
    auto dynSecs = buildResolvedDynamicSections();
    for (auto& s : dynSecs) {
        result.push_back(std::move(s));
    }

    return result;
}

String SystemPromptBuilder::build() {
    // SIMPLE mode check
    if (isSimpleMode()) {
        auto parts = buildSimpleModePrompt(env_);
        std::ostringstream oss;
        for (const auto& p : parts) {
            oss << p;
        }

        // Git context
        if (!gitCtx_.branch.empty()) {
            oss << "\n\n# Git Status\n";
            oss << "Current branch: " << gitCtx_.branch << "\n";
            if (!gitCtx_.status.empty()) {
                oss << "Main branch (you will usually use this for PRs): main\n\n";
                oss << "Status:\n" << gitCtx_.status << "\n";
            }
        }

        // CLAUDE.md content
        if (!claudeMd_.empty()) {
            oss << "\n\n# Codebase and user instructions\n";
            oss << "Codebase and user instructions are shown below. Be sure to adhere to these instructions. "
                << "IMPORTANT: These instructions OVERRIDE any default behavior and you must follow them exactly as written.\n\n";
            oss << "Contents of " << workDir_ << "/CLAUDE.md:\n";
            oss << claudeMd_ << "\n";
        }

        // Extra context
        if (!additionalContext_.empty()) {
            oss << "\n" << additionalContext_ << "\n";
        }

        // RAG context
        if (!ragContext_.empty()) {
            oss << "\n" << ragContext_ << "\n";
        }

        return oss.str();
    }

    // Build the default system prompt vector and join
    auto parts = buildDefaultSystemPromptVector();

    std::ostringstream oss;
    bool first = true;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == SYSTEM_PROMPT_DYNAMIC_BOUNDARY) {
            continue;  // Strip boundary marker from string output
        }
        if (!first) {
            oss << "\n\n";
        }
        oss << parts[i];
        first = false;
    }

    // Git context
    if (!gitCtx_.branch.empty()) {
        oss << "\n\n# Git Status\n";
        oss << "Current branch: " << gitCtx_.branch << "\n";
        if (!gitCtx_.status.empty()) {
            oss << "Main branch (you will usually use this for PRs): main\n\n";
            oss << "Status:\n" << gitCtx_.status << "\n";
        }
    }

    // CLAUDE.md content
    if (!claudeMd_.empty()) {
        oss << "\n\n# Codebase and user instructions\n";
        oss << "Codebase and user instructions are shown below. Be sure to adhere to these instructions. "
            << "IMPORTANT: These instructions OVERRIDE any default behavior and you must follow them exactly as written.\n\n";
        oss << "Contents of " << workDir_ << "/CLAUDE.md:\n";
        oss << claudeMd_ << "\n";
    }

    // Extra context
    if (!additionalContext_.empty()) {
        oss << "\n" << additionalContext_ << "\n";
    }

    // RAG context
    if (!ragContext_.empty()) {
        oss << "\n" << ragContext_ << "\n";
    }

    return oss.str();
}

std::vector<TextBlockParam> SystemPromptBuilder::buildBlocks() {
    // SIMPLE mode check
    if (isSimpleMode()) {
        auto parts = buildSimpleModePrompt(env_);
        std::vector<TextBlockParam> blocks;
        for (auto& p : parts) {
            blocks.push_back({.text = std::move(p)});
        }

        // Git context
        if (!gitCtx_.branch.empty()) {
            std::ostringstream oss;
            oss << "# Git Status\n";
            oss << "Current branch: " << gitCtx_.branch << "\n";
            if (!gitCtx_.status.empty()) {
                oss << "Main branch (you will usually use this for PRs): main\n\n";
                oss << "Status:\n" << gitCtx_.status << "\n";
            }
            blocks.push_back({.text = oss.str()});
        }

        // CLAUDE.md content
        if (!claudeMd_.empty()) {
            std::ostringstream oss;
            oss << "# Codebase and user instructions\n";
            oss << "Codebase and user instructions are shown below. Be sure to adhere to these instructions. "
                << "IMPORTANT: These instructions OVERRIDE any default behavior and you must follow them exactly as written.\n\n";
            oss << "Contents of " << workDir_ << "/CLAUDE.md:\n";
            oss << claudeMd_ << "\n";
            blocks.push_back({.text = oss.str()});
        }

        // Extra context
        if (!additionalContext_.empty()) {
            blocks.push_back({.text = additionalContext_});
        }

        // RAG context
        if (!ragContext_.empty()) {
            blocks.push_back({.text = ragContext_});
        }

        // Add cache control to last block
        if (!blocks.empty()) {
            blocks.back().cache_control = CacheControl{
                .type = "ephemeral",
                .scope = CacheScope::Org
            };
        }

        return blocks;
    }

    // Standard mode: build blocks from vector
    auto parts = buildDefaultSystemPromptVector();
    std::vector<TextBlockParam> blocks;

    for (auto& p : parts) {
        if (p == SYSTEM_PROMPT_DYNAMIC_BOUNDARY) {
            // Boundary marker: do NOT emit as text. Instead, apply global
            // cache control to the preceding block (last static section).
            if (!blocks.empty()) {
                blocks.back().cache_control = CacheControl{
                    .type = "ephemeral",
                    .scope = CacheScope::Global
                };
            }
            // Skip the boundary marker itself — it's structural, not content
        } else {
            blocks.push_back({.text = std::move(p)});
        }
    }

    // Git context
    if (!gitCtx_.branch.empty()) {
        std::ostringstream oss;
        oss << "# Git Status\n";
        oss << "Current branch: " << gitCtx_.branch << "\n";
        if (!gitCtx_.status.empty()) {
            oss << "Main branch (you will usually use this for PRs): main\n\n";
            oss << "Status:\n" << gitCtx_.status << "\n";
        }
        blocks.push_back({.text = oss.str()});
    }

    // CLAUDE.md content
    if (!claudeMd_.empty()) {
        std::ostringstream oss;
        oss << "# Codebase and user instructions\n";
        oss << "Codebase and user instructions are shown below. Be sure to adhere to these instructions. "
            << "IMPORTANT: These instructions OVERRIDE any default behavior and you must follow them exactly as written.\n\n";
        oss << "Contents of " << workDir_ << "/CLAUDE.md:\n";
        oss << claudeMd_ << "\n";
        blocks.push_back({.text = oss.str()});
    }

    // Extra context
    if (!additionalContext_.empty()) {
        blocks.push_back({.text = additionalContext_});
    }

    // RAG context
    if (!ragContext_.empty()) {
        blocks.push_back({.text = ragContext_});
    }

    // Add cache control to last block (org-level cache)
    if (!blocks.empty()) {
        blocks.back().cache_control = CacheControl{
            .type = "ephemeral",
            .scope = CacheScope::Org
        };
    }

    return blocks;
}

// ========== Builder Setters ==========

SystemPromptBuilder& SystemPromptBuilder::withClaudeMd(const String& content) {
    claudeMd_ = content;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withGitContext(const GitContext& ctx) {
    gitCtx_ = ctx;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withWorkDir(const String& dir) {
    workDir_ = dir;
    env_.cwd = dir;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withAdditionalContext(const String& context) {
    additionalContext_ = context;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withRagContext(const String& context) {
    ragContext_ = context;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withAutoRag(const String& query, int maxDocs) {
    if (!isRagEnabled()) {
        return *this;
    }

    auto& manager = rag::RagManager::instance();

    if (!manager.shouldTrigger(query)) {
        return *this;
    }

    if (!manager.isAvailable()) {
        return *this;
    }

    ragContext_ = manager.buildContext(query, maxDocs, 3000);
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withEnvironment(const EnvironmentInfo& env) {
    env_ = env;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withSettings(const PromptSettings& settings) {
    settings_ = settings;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withEnabledTools(const std::vector<ToolInfo>& tools) {
    enabledTools_ = tools;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withMcpServers(const std::vector<McpServerInfo>& servers) {
    mcpServers_ = servers;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withOutputStyle(const std::optional<OutputStyleConfig>& style) {
    outputStyle_ = style;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withGlobalCache(bool useGlobalCache) {
    useGlobalCache_ = useGlobalCache;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withNonInteractive(bool nonInteractive) {
    isNonInteractive_ = nonInteractive;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withReplMode(bool replMode) {
    isReplMode_ = replMode;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withSessionMode(SessionMode mode) {
    sessionMode_ = mode;
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::withDynamicSection(const SystemPromptSection& section) {
    dynamicSections_.push_back(section);
    return *this;
}

SystemPromptBuilder& SystemPromptBuilder::clearDynamicSections() {
    dynamicSections_.clear();
    return *this;
}

bool SystemPromptBuilder::isRagEnabled() {
    auto configPath = rag::RagConfig::getDefaultPath();
    auto config = rag::RagConfig::load(configPath);
    return config && config->enabled;
}

} // namespace claude
