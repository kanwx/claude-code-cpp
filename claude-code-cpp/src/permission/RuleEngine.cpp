#include <claude/permission/RuleEngine.hpp>
#include <claude/permission/PermissionStore.hpp>
#include <claude/config/SettingsManager.hpp>
#include <regex>

namespace claude {

const std::unordered_set<String> RuleEngine::READ_ONLY_TOOLS = {
    "Read", "Glob", "Grep", "ListFiles", "WebFetch", "WebSearch",
    "TodoRead", "TaskGet", "TaskList", "AskUserQuestion"
};

const std::unordered_set<String> RuleEngine::FILE_EDIT_TOOLS = {
    "Write", "Edit", "NotebookEdit"
};

RuleEngine::RuleEngine(PermissionSettings& settings)
    : settings_(settings) {}

// ========== 核心评估 ==========

PermissionDecision RuleEngine::evaluate(
    const String& toolName,
    const Json& input,
    bool isReadOnly
) {
    return evaluateInner(toolName, input, isReadOnly, false, nullptr);
}

PermissionDecision RuleEngine::evaluate(
    const String& toolName,
    const Json& input,
    bool isReadOnly,
    const std::vector<Message>& transcript
) {
    return evaluateInner(toolName, input, isReadOnly, false, &transcript);
}

PermissionDecision RuleEngine::evaluate(
    const String& toolName,
    const Json& input,
    bool isReadOnly,
    bool isSandboxed
) {
    return evaluateInner(toolName, input, isReadOnly, isSandboxed, nullptr);
}

PermissionDecision RuleEngine::evaluate(
    const String& toolName,
    const Json& input,
    bool isReadOnly,
    bool isSandboxed,
    const std::vector<Message>* transcript
) {
    return evaluateInner(toolName, input, isReadOnly, isSandboxed, transcript);
}

PermissionDecision RuleEngine::evaluateInner(
    const String& toolName,
    const Json& input,
    bool isReadOnly,
    bool isSandboxed,
    const std::vector<Message>* transcript
) {
    PermissionMode mode = settings_.getCurrentMode();

    // 1. BYPASS 模式：全部允许
    if (mode == PermissionMode::Bypass) {
        denialTracker_.recordSuccess();
        return PermissionDecision::allowWithReason(
            "Bypass mode enabled",
            ModeDecisionReason{PermissionMode::Bypass}
        );
    }

    // 2. PLAN 模式：仅允许只读工具
    if (mode == PermissionMode::Plan) {
        if (isReadOnly || READ_ONLY_TOOLS.contains(toolName)) {
            denialTracker_.recordSuccess();
            return PermissionDecision::allowWithReason(
                "Read-only tool allowed in plan mode",
                ModeDecisionReason{PermissionMode::Plan}
            );
        }
        return PermissionDecision::denyWithReason(
            "Plan mode: execution disabled (analysis only)",
            ModeDecisionReason{PermissionMode::Plan}
        );
    }

    // 获取命令内容
    String command = extractCommand(toolName, input);

    // Synthesize effective toolName for Agent rules:
    // If toolName is "Agent" and input has subagent_type, synthesize "Agent(type)"
    String effectiveToolName = toolName;
    if (toolName == "Agent" && input.is_object() && input.contains("subagent_type")) {
        String subagentType = input.value("subagent_type", "");
        if (!subagentType.empty()) {
            effectiveToolName = "Agent(" + subagentType + ")";
        }
    }

    // 获取所有规则 (sorted by source priority, managed-only aware)
    auto rules = settings_.getAllRules();

    // 3. 检查 deny 规则 (bypass-immune: 即使 Bypass 模式也生效)
    for (const auto& rule : rules) {
        if (rule.behavior == PermissionBehavior::Deny && matchesRule(rule, effectiveToolName, command)) {
            denialTracker_.recordDenial();
            return PermissionDecision::denyWithReason(
                "Denied by rule: " + PermissionSettings::formatRule(rule),
                RuleDecisionReason{rule, rule.source}
            );
        }
    }

    // 4. 检查 ask 规则 (force permission prompt, overrides allow)
    //    Skip ask rules for sandboxed Bash when autoAllowBashIfSandboxed is true
    bool skipAskForSandboxedBash = (toolName == "Bash" && isSandboxed &&
                                     settings_.autoAllowBashIfSandboxed());

    if (!skipAskForSandboxedBash) {
        for (const auto& rule : rules) {
            if (rule.behavior == PermissionBehavior::Ask && matchesRule(rule, effectiveToolName, command)) {
                String prefix = extractCommandPrefix(command);
                return PermissionDecision::askWithReason(
                    toolName, prefix,
                    RuleDecisionReason{rule, rule.source}
                );
            }
        }
    }

    // 5. 检查 allow 规则
    for (const auto& rule : rules) {
        if (rule.behavior == PermissionBehavior::Allow && matchesRule(rule, effectiveToolName, command)) {
            denialTracker_.recordSuccess();
            return PermissionDecision::allowWithReason(
                "Allowed by rule: " + PermissionSettings::formatRule(rule),
                RuleDecisionReason{rule, rule.source}
            );
        }
    }

    // Sandbox override: sandboxed Bash commands auto-allow
    if (skipAskForSandboxedBash) {
        denialTracker_.recordSuccess();
        return PermissionDecision::allowWithReason(
            "Sandboxed Bash command auto-allowed",
            SandboxOverrideReason{command}
        );
    }

    // 6. 只读工具直接放行
    if (isReadOnly || READ_ONLY_TOOLS.contains(toolName)) {
        denialTracker_.recordSuccess();
        return PermissionDecision::allow("Read-only tool");
    }

    // 7. ACCEPT_EDITS 模式：文件操作工具自动允许
    if (mode == PermissionMode::AcceptEdits && FILE_EDIT_TOOLS.contains(toolName)) {
        denialTracker_.recordSuccess();
        return PermissionDecision::allowWithReason(
            "File edits auto-allowed in accept-edits mode",
            ModeDecisionReason{PermissionMode::AcceptEdits}
        );
    }

    // ========== AUTO 模式核心 ==========

    // 8. AUTO 模式：YOLO 分类器决策
    if (mode == PermissionMode::Auto) {
        // 8.1 拒绝追踪检查：过多拒绝时降级到提示模式
        if (denialTracker_.shouldFallbackToPrompting()) {
            String fallbackReason;
            if (denialTracker_.consecutive() >= DenialTracker::CONSECUTIVE_THRESHOLD) {
                fallbackReason = std::to_string(denialTracker_.consecutive()) +
                    " consecutive actions were blocked. Please review the transcript before continuing.";
            } else {
                fallbackReason = std::to_string(denialTracker_.total()) +
                    " actions were blocked this session. Please review the transcript before continuing.";
                denialTracker_.reset();
            }
            String prefix = extractCommandPrefix(command);
            return PermissionDecision::ask(toolName, prefix);
        }

        // 8.2 AcceptEdits 快速路径
        if (YoloClassifier::wouldAcceptInEditMode(toolName, input)) {
            denialTracker_.recordSuccess();
            return PermissionDecision::allowWithReason(
                "Auto mode: AcceptEdits fast-path",
                ClassifierDecisionReason{"yolo", "acceptEdits fast-path"}
            );
        }

        // 8.3 安全工具白名单
        if (SAFE_YOLO_ALLOWLISTED_TOOLS.contains(toolName)) {
            denialTracker_.recordSuccess();
            return PermissionDecision::allowWithReason(
                "Auto mode: safe tool allowlist",
                ClassifierDecisionReason{"yolo", "safe tool allowlist"}
            );
        }

        // 8.4 YOLO 分类器 (需要对话历史)
        if (yoloClassifier_.isEnabled()) {
            std::vector<Json> jsonTranscript;
            if (transcript) {
                for (const auto& msg : *transcript) {
                    String roleStr;
                    switch (msg.role) {
                        case MessageRole::System: roleStr = "system"; break;
                        case MessageRole::User: roleStr = "user"; break;
                        case MessageRole::Assistant: roleStr = "assistant"; break;
                        case MessageRole::ToolResult: roleStr = "tool"; break;
                    }
                    jsonTranscript.push_back({
                        {"role", roleStr},
                        {"content", msg.content}
                    });
                }
            }

            auto yoloResult = yoloClassifier_.classify(toolName, input, jsonTranscript);

            if (yoloResult.shouldBlock) {
                denialTracker_.recordDenial();
                return PermissionDecision::denyWithReason(
                    "Auto mode blocked: " + yoloResult.reason,
                    ClassifierDecisionReason{"yolo", yoloResult.reason}
                );
            } else {
                denialTracker_.recordSuccess();
                return PermissionDecision::allowWithReason(
                    "Auto mode allowed: " + yoloResult.reason,
                    ClassifierDecisionReason{"yolo", yoloResult.reason}
                );
            }
        }

        // 8.5 无分类器时的降级：AcceptEdits 行为 + Ask
        if (FILE_EDIT_TOOLS.contains(toolName)) {
            denialTracker_.recordSuccess();
            return PermissionDecision::allow("Auto mode (no classifier): file edit allowed");
        }
        String prefix = extractCommandPrefix(command);
        return PermissionDecision::ask(toolName, prefix);
    }

    // ========== 非 AUTO 模式的后续检查 ==========

    // 9. DONT_ASK 模式：自动拒绝
    if (mode == PermissionMode::DontAsk) {
        return PermissionDecision::denyWithReason(
            "Auto-denied in dont-ask mode",
            ModeDecisionReason{PermissionMode::DontAsk}
        );
    }

    // 10. Bash AI 分类器：对 Bash 工具自动分类安全/危险命令
    if (toolName == "Bash" && !command.empty()) {
        auto classification = bashClassifier_.classify(command);
        switch (classification) {
            case BashClassification::AlwaysAllow:
                denialTracker_.recordSuccess();
                return PermissionDecision::allowWithReason(
                    "Bash classifier: safe command auto-allowed",
                    ClassifierDecisionReason{"bash", "safe command"}
                );
            case BashClassification::AutoDeny:
                denialTracker_.recordDenial();
                return PermissionDecision::denyWithReason(
                    "Bash classifier: dangerous command auto-denied",
                    ClassifierDecisionReason{"bash", "dangerous command"}
                );
            case BashClassification::RequirePermission:
                break;
        }
    }

    // 11. 检查危险命令
    if (!command.empty()) {
        auto danger = DangerousPatterns::detectDangerous(command);
        if (danger) {
            String prefix = extractCommandPrefix(command);
            return PermissionDecision::askWithReason(
                toolName, prefix,
                SafetyCheckReason{*danger}
            );
        }
    }

    // 12. Check PermissionStore for persisted "always" decisions
    if (!command.empty()) {
        String prefix = extractCommandPrefix(command);
        String pattern = prefix.empty() ? command : prefix;
        auto persisted = PermissionStore::instance().lookup(toolName, pattern);
        if (persisted) {
            switch (*persisted) {
                case PermissionChoice::AlwaysAllow:
                    denialTracker_.recordSuccess();
                    return PermissionDecision::allowWithReason(
                        "Allowed by persisted decision: " + toolName + ":" + pattern,
                        RuleDecisionReason{
                            PermissionRule{toolName, pattern + ":*", PermissionBehavior::Allow, PermissionRuleSource::UserSettings},
                            PermissionRuleSource::UserSettings
                        }
                    );
                case PermissionChoice::AlwaysDeny:
                    denialTracker_.recordDenial();
                    return PermissionDecision::denyWithReason(
                        "Denied by persisted decision: " + toolName + ":" + pattern,
                        RuleDecisionReason{
                            PermissionRule{toolName, pattern + ":*", PermissionBehavior::Deny, PermissionRuleSource::UserSettings},
                            PermissionRuleSource::UserSettings
                        }
                    );
                default:
                    break;
            }
        }
    }

    // 13. 默认：需要用户确认
    String prefix = extractCommandPrefix(command);
    return PermissionDecision::ask(toolName, prefix);
}

void RuleEngine::applyChoice(
    PermissionChoice choice,
    const String& toolName,
    const String& command
) {
    PermissionRuleSource destSource = determineChoiceSource(choice);
    applyChoiceToSource(choice, toolName, command, destSource);
}

void RuleEngine::applyChoiceToSource(
    PermissionChoice choice,
    const String& toolName,
    const String& command,
    PermissionRuleSource destination
) {
    String prefix = extractCommandPrefix(command);

    switch (choice) {
        case PermissionChoice::AlwaysAllow: {
            auto rule = prefix.empty()
                ? PermissionRule::forTool(toolName, PermissionBehavior::Allow, destination)
                : PermissionRule::forCommand(toolName, prefix, PermissionBehavior::Allow, destination);

            String ruleStr = PermissionSettings::formatRule(rule);
            if (!DangerousPatterns::isDangerousWildcard(ruleStr)) {
                settings_.addPermissionRulesToSettings(
                    destination,
                    {rule},
                    settingsManager_
                );
            }

            // Persist to PermissionStore for cross-session recall
            String pattern = prefix.empty() ? command : prefix;
            PermissionStore::instance().recordDecision(toolName, pattern, PermissionChoice::AlwaysAllow);
            break;
        }

        case PermissionChoice::AlwaysDeny: {
            auto rule = prefix.empty()
                ? PermissionRule::forTool(toolName, PermissionBehavior::Deny, destination)
                : PermissionRule::forCommand(toolName, prefix, PermissionBehavior::Deny, destination);
            settings_.addPermissionRulesToSettings(
                destination,
                {rule},
                settingsManager_
            );

            // Persist to PermissionStore for cross-session recall
            String pattern = prefix.empty() ? command : prefix;
            PermissionStore::instance().recordDecision(toolName, pattern, PermissionChoice::AlwaysDeny);
            break;
        }

        default:
            break;
    }
}

PermissionRuleSource RuleEngine::determineChoiceSource(PermissionChoice choice) const {
    // Default routing: "Always allow/deny" choices go to user settings
    // If we have a settings manager context, we could route differently
    // For now, default to UserSettings (matches TS behavior for non-project rules)
    return PermissionRuleSource::UserSettings;
}

void RuleEngine::syncFromDisk() {
    if (settingsManager_) {
        settings_.syncPermissionRulesFromDisk(*settingsManager_);
    }
}

// ========== 规则匹配 ==========

bool RuleEngine::matchesRule(
    const PermissionRule& rule,
    const String& toolName,
    const String& command
) const {
    const String& ruleToolName = rule.toolName;
    const String& content = rule.ruleContent;

    // Check MCP tool matching first (special format: mcp__server__tool)
    if (ruleToolName.starts_with("mcp__") || toolName.starts_with("mcp__")) {
        if (matchesMcpTool(ruleToolName, toolName)) {
            // MCP rules: the ruleContent is the tool part within the MCP name
            // or "*" for all tools under that server
            if (content == "*") return true;

            // For MCP rules, command may contain the full tool invocation
            if (!command.empty() && content != "*") {
                // Use standard content matching logic on command
                if (content.starts_with("re:")) {
                    String pattern = content.substr(3);
                    try {
                        std::regex re(pattern, std::regex::ECMAScript);
                        if (std::regex_search(command, re)) return true;
                    } catch (const std::regex_error&) {}
                } else if (content.find('*') != String::npos || content.find('?') != String::npos) {
                    if (matchesGlob(command, content)) return true;
                } else if (command.starts_with(content)) {
                    return true;
                }
            }
            return content == "*";
        }
        return false;
    }

    // Check Agent(type) deny rule matching
    if (ruleToolName.starts_with("Agent(") || toolName.starts_with("Agent(")) {
        return matchesAgentRule(ruleToolName, toolName);
    }

    // Standard tool name match
    if (ruleToolName != toolName) return false;

    // Wildcard * matches all
    if (content == "*") return true;

    // Regex pattern: re:prefix
    if (content.starts_with("re:")) {
        String pattern = content.substr(3);
        try {
            std::regex re(pattern, std::regex::ECMAScript);
            if (!command.empty() && std::regex_search(command, re)) {
                return true;
            }
        } catch (const std::regex_error&) {
            // Invalid regex, fall through
        }
        return false;
    }

    // Path-based rule: ~/ or starts with /
    if (content.starts_with("~/") || content.starts_with("/") ||
        content.starts_with("~\\")) {
        String expandedPath = content;
        if (expandedPath.starts_with("~/")) {
            const char* home = std::getenv("HOME");
            if (home) {
                expandedPath = String(home) + expandedPath.substr(1);
            }
        }
        if (!command.empty() && command.starts_with(expandedPath)) {
            return true;
        }
        if (expandedPath.find("**") != String::npos) {
            String upToGlob = expandedPath.substr(0, expandedPath.find("**"));
            if (!command.empty() && command.starts_with(upToGlob)) {
                return true;
            }
        }
        return false;
    }

    // Glob pattern matching
    if (content.find('*') != String::npos || content.find('?') != String::npos) {
        // suffix:* means prefix match
        if (content.ends_with(":*") && !command.empty()) {
            String pfx = content.substr(0, content.length() - 2);
            return command.starts_with(pfx);
        }

        if (!command.empty() && matchesGlob(command, content)) {
            return true;
        }
        return false;
    }

    // Exact match
    return content == command;
}

bool RuleEngine::matchesGlob(const String& text, const String& pattern) const {
    size_t ti = 0, pi = 0;
    size_t starPos = String::npos;
    size_t matchPos = 0;

    while (ti < text.size()) {
        if (pi < pattern.size()) {
            if (pattern[pi] == text[ti]) {
                ti++;
                pi++;
                continue;
            }
            if (pattern[pi] == '?') {
                ti++;
                pi++;
                continue;
            }
            if (pattern[pi] == '*') {
                starPos = pi++;
                matchPos = ti;
                continue;
            }
        }
        if (starPos != String::npos) {
            pi = starPos + 1;
            ti = ++matchPos;
            continue;
        }
        return false;
    }

    while (pi < pattern.size() && pattern[pi] == '*') {
        pi++;
    }

    return pi == pattern.size();
}

String RuleEngine::extractCommand(const String& toolName, const Json& input) const {
    if (input.is_null()) return "";

    if (toolName == "Bash") {
        return input.value("command", "");
    } else if (toolName == "Write" || toolName == "Edit" || toolName == "NotebookEdit") {
        return input.value("file_path", "");
    } else if (toolName == "Read" || toolName == "Glob" || toolName == "Grep") {
        // Extract file path for read-only tools — needed for path-based permission rules
        if (input.contains("file_path")) return input.value("file_path", "");
        if (input.contains("path")) return input.value("path", "");
        if (toolName == "Glob" && input.contains("pattern")) return input.value("pattern", "");
        if (toolName == "Grep" && input.contains("path")) return input.value("path", "");
        return "";
    } else if (toolName.starts_with("mcp__")) {
        // MCP tools: extract the tool invocation details
        // The command for MCP tools is derived from the input arguments
        if (input.is_object()) {
            // Try common MCP input fields
            if (input.contains("command")) return input.value("command", "");
            if (input.contains("input")) {
                // MCP tools often wrap their input
                const auto& mcpInput = input["input"];
                if (mcpInput.is_string()) return mcpInput.get<String>();
                if (mcpInput.is_object()) return mcpInput.dump();
            }
            if (input.contains("arguments")) {
                const auto& args = input["arguments"];
                if (args.is_string()) return args.get<String>();
            }
        }
        return "";
    }

    return "";
}

String RuleEngine::extractCommandPrefix(const String& command) const {
    if (command.empty()) return "";

    String trimmed = command;
    size_t start = trimmed.find_first_not_of(" \t");
    if (start != String::npos) {
        trimmed = trimmed.substr(start);
    }

    size_t space = trimmed.find(' ');
    return space != String::npos ? trimmed.substr(0, space) : trimmed;
}

// ========== MCP 工具匹配 ==========

bool RuleEngine::matchesMcpTool(const String& ruleToolName, const String& actualToolName) const {
    auto ruleMcp = McpToolName::parse(ruleToolName);
    auto actualMcp = McpToolName::parse(actualToolName);

    // Neither is an MCP name, not our concern
    if (!ruleMcp && !actualMcp) return false;

    // If the rule is an MCP name
    if (ruleMcp) {
        // Rule "mcp__server" (no tool part) matches all tools from that server
        if (!ruleMcp->tool.has_value()) {
            if (actualMcp) {
                return ruleMcp->server == actualMcp->server;
            }
            // Also try matching the rule's server against a non-MCP tool name
            // (shouldn't happen, but be safe)
            return false;
        }

        // Rule "mcp__server__*" matches all tools from that server
        if (ruleMcp->tool == "*") {
            if (actualMcp) {
                return ruleMcp->server == actualMcp->server;
            }
            return false;
        }

        // Rule "mcp__server__tool" matches exact tool
        if (actualMcp) {
            return ruleMcp->server == actualMcp->server &&
                   ruleMcp->tool == actualMcp->tool;
        }
        // Rule is "mcp__server__tool", actual is not MCP → no match
        return false;
    }

    // Rule is not MCP but actual is → no match
    return false;
}

// ========== Agent deny rule matching ==========

bool RuleEngine::matchesAgentRule(const String& ruleToolName, const String& actualToolName) const {
    auto ruleAgent = AgentRule::parse(ruleToolName);
    if (!ruleAgent) return false;

    // The actual tool invocation for agents has toolName="Agent"
    // with the subagent type in the input JSON's "subagent_type" field.
    // The AgentRule matching happens when the rule toolName is "Agent(type)"
    // and the actual toolName is "Agent(type)" (synthesized during evaluation).
    //
    // Alternatively, if the rule is "Agent(type)" and the actual is just "Agent",
    // we need the input context to determine the type. This is handled at a
    // higher level (in evaluateInner where we synthesize the toolName).

    auto actualAgent = AgentRule::parse(actualToolName);
    if (!actualAgent) return false;

    // Match exact agent type
    return ruleAgent->agentType == actualAgent->agentType;
}

} // namespace claude
