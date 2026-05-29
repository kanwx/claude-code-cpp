#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <chrono>
#include <variant>

namespace claude {

// ========== 权限行为 ==========

/// 权限行为枚举 (3-tier: Allow/Deny/Ask)
enum class PermissionBehavior {
    Allow,  // 允许执行
    Deny,   // 拒绝执行
    Ask     // 需要用户确认
};

/// 权限模式枚举
enum class PermissionMode {
    Default,       // 默认模式：非只读工具需要用户确认
    AcceptEdits,   // 自动允许文件编辑，shell 命令仍需确认
    Bypass,        // 跳过所有权限检查（不安全）
    DontAsk,       // 自动拒绝而非询问用户（无头模式）
    Plan,          // 计划模式：仅分析不执行
    Auto           // 自动模式：AI 分类器决定是否允许，拒绝过多时降级
};

// ========== 组织策略 ==========

/// 权限策略来源
enum class PolicySource {
    None,      // 未设置
    Statsig,   // Statsig/Gate 远程控制
    Settings   // 本地设置
};

/// 权限策略 (组织级控制)
struct PermissionPolicy {
    bool disableBypassPermissionsMode = false;
    PolicySource source = PolicySource::None;
    String notification;  // 策略通知消息

    /// 检查 bypass 是否被禁用
    bool isBypassDisabled() const {
        return disableBypassPermissionsMode;
    }
};

// ========== 权限规则 ==========

/// 权限规则来源 (8-tier source hierarchy, priority order)
enum class PermissionRuleSource {
    PolicySettings,    // Admin-managed policy (managed-settings.json, MDM)
    FlagSettings,      // CLI --settings flags
    UserSettings,      // ~/.claude/settings.json
    ProjectSettings,   // .claude/settings.json (shared, VCS-tracked)
    LocalSettings,     // .claude/settings.local.json (gitignored)
    CliArg,            // 命令行参数 (--allow, --deny)
    Command,           // 斜杠命令 (/permissions)
    Session            // 会话规则 (ephemeral, cleared on exit)
};

/// 权限规则
struct PermissionRule {
    String toolName;             // 工具名称 (Bash, Write, Edit, mcp__server__tool, Agent(type) 等)
    String ruleContent;          // 规则内容: "npm:*", "git:*", "*" 通配符
    PermissionBehavior behavior; // 行为 (Allow/Deny/Ask)
    PermissionRuleSource source; // 来源

    // 工厂方法
    static PermissionRule forTool(String toolName, PermissionBehavior behavior,
                                  PermissionRuleSource source = PermissionRuleSource::Session) {
        return {std::move(toolName), "*", behavior, source};
    }

    static PermissionRule forCommand(String toolName, String prefix, PermissionBehavior behavior,
                                      PermissionRuleSource source = PermissionRuleSource::Session) {
        return {std::move(toolName), prefix + ":*", behavior, source};
    }

    /// Create a rule with explicit source
    static PermissionRule withSource(PermissionRule rule, PermissionRuleSource source) {
        rule.source = source;
        return rule;
    }
};

// ========== 决策原因 ==========

/// Decision reason: matched a rule
struct RuleDecisionReason {
    PermissionRule rule;
    PermissionRuleSource source;
};

/// Decision reason: Bash/AI classifier
struct ClassifierDecisionReason {
    String name;      // "bash" | "yolo"
    String reason;
};

/// Decision reason: hook triggered
struct HookDecisionReason {
    String hookName;
};

/// Decision reason: subcommand results aggregated
struct SubcommandResultsReason {
    int allowed;
    int denied;
    int asked;
};

/// Decision reason: sandbox override
struct SandboxOverrideReason {
    String command;
};

/// Decision reason: safety check
struct SafetyCheckReason {
    String check;
};

/// Decision reason: mode-based decision
struct ModeDecisionReason {
    PermissionMode mode;
};

/// Structured permission decision reason (variant of all possible reasons)
using PermissionDecisionReason = std::variant<
    RuleDecisionReason,
    ClassifierDecisionReason,
    HookDecisionReason,
    SubcommandResultsReason,
    SandboxOverrideReason,
    SafetyCheckReason,
    ModeDecisionReason
>;

// ========== 权限决策 ==========

/// 权限决策结果
struct PermissionDecision {
    PermissionBehavior behavior;
    String reason;
    String toolName;
    String commandPrefix;
    std::vector<PermissionRule> suggestedRules;
    std::optional<PermissionDecisionReason> decisionReason;  // Structured reason

    // 工厂方法
    static PermissionDecision allow(String reason) {
        return {PermissionBehavior::Allow, std::move(reason), {}, {}, {}, std::nullopt};
    }

    static PermissionDecision allowWithReason(String reason, PermissionDecisionReason dr) {
        return {PermissionBehavior::Allow, std::move(reason), {}, {}, {}, std::move(dr)};
    }

    static PermissionDecision deny(String reason) {
        return {PermissionBehavior::Deny, std::move(reason), {}, {}, {}, std::nullopt};
    }

    static PermissionDecision denyWithReason(String reason, PermissionDecisionReason dr) {
        return {PermissionBehavior::Deny, std::move(reason), {}, {}, {}, std::move(dr)};
    }

    static PermissionDecision ask(String toolName, String commandPrefix) {
        return {
            PermissionBehavior::Ask,
            "Requires user confirmation",
            std::move(toolName),
            std::move(commandPrefix),
            {},
            std::nullopt
        };
    }

    static PermissionDecision askWithReason(String toolName, String commandPrefix,
                                             PermissionDecisionReason dr) {
        return {
            PermissionBehavior::Ask,
            "Requires user confirmation",
            std::move(toolName),
            std::move(commandPrefix),
            {},
            std::move(dr)
        };
    }

    // 辅助方法
    bool isAllowed() const { return behavior == PermissionBehavior::Allow; }
    bool isDenied() const { return behavior == PermissionBehavior::Deny; }
    bool needsAsk() const { return behavior == PermissionBehavior::Ask; }
};

// ========== 权限选择 ==========

/// 权限确认选项 (用户在 UI 中的选择)
enum class PermissionChoice {
    AllowOnce,      // 允许本次执行
    AllowSession,   // 允许本次会话（不持久化到磁盘）
    AlwaysAllow,    // 始终允许此模式
    DenyOnce,       // 拒绝本次执行
    AlwaysDeny      // 始终拒绝此模式
};

// ========== 权限请求 ==========

/// 权限确认请求
struct PermissionRequest {
    String toolName;
    String arguments;
    String activityDescription;
    std::optional<PermissionDecision> decision;  // 预决策
};

// ========== 权限更新 ==========

/// 权限更新目的地
enum class PermissionUpdateDestination {
    PolicySettings,
    FlagSettings,
    UserSettings,
    ProjectSettings,
    LocalSettings,
    Session,
    CliArg
};

/// 权限更新操作
struct PermissionUpdate {
    enum class Type { AddRule, RemoveRule, SetMode };
    Type type;
    std::optional<PermissionRule> rule;
    std::optional<PermissionMode> mode;
    PermissionUpdateDestination destination;
};

// ========== MCP 工具名解析 ==========

/// Parsed MCP tool name components
struct McpToolName {
    String server;                // MCP server name
    std::optional<String> tool;   // Tool name (nullopt for server-level match)

    /// Parse "mcp__server__tool" or "mcp__server" format
    static std::optional<McpToolName> parse(const String& name);

    /// Format back to string
    String toString() const;

    /// Check if this matches another McpToolName (server-level matches all tools)
    bool matches(const McpToolName& target) const;
};

// ========== Agent deny rule syntax ==========

/// Parsed Agent(type) rule
struct AgentRule {
    String agentType;             // Agent type name (e.g., "Explore")

    /// Parse "Agent(type)" format
    static std::optional<AgentRule> parse(const String& name);

    /// Format back to string
    String toString() const;
};

// ========== 权限规则解析器 ==========

/// PermissionRule parser: converts between structured PermissionRule and settings string format
///
/// Settings file format: "Tool(prefix:*)" e.g. "Bash(npm:*)", "Write(*)", "mcp__server1__*"
/// Behavior is determined by the key in the JSON: "allow" or "deny" or "ask"
class PermissionRuleParser {
public:
    /// Parse a rule string like "Bash(npm:*)" into a PermissionRule
    /// @param ruleStr Rule string in Tool(content) format
    /// @param behavior The behavior (determined by JSON key: "allow"/"deny"/"ask")
    /// @param source Rule source
    /// @return Parsed rule, or nullopt if invalid
    static std::optional<PermissionRule> parse(
        const String& ruleStr,
        PermissionBehavior behavior,
        PermissionRuleSource source = PermissionRuleSource::UserSettings
    );

    /// Format a PermissionRule to string: "Tool(content)"
    static String format(const PermissionRule& rule);

    /// Parse a JSON array of rule strings into PermissionRules
    /// @param arr JSON array of rule strings
    /// @param behavior The behavior for all rules in this array
    /// @param source Rule source
    static std::vector<PermissionRule> parseArray(
        const Json& arr,
        PermissionBehavior behavior,
        PermissionRuleSource source = PermissionRuleSource::UserSettings
    );

    /// Format a vector of PermissionRules into a JSON array of strings
    static Json formatArray(const std::vector<PermissionRule>& rules);

    /// Parse all rules from a settings JSON object with "allow", "deny", "ask" keys
    /// @param settings JSON object: {"allow": [...], "deny": [...], "ask": [...]}
    /// @param source Rule source
    static std::vector<PermissionRule> parseAll(
        const Json& settings,
        PermissionRuleSource source = PermissionRuleSource::UserSettings
    );
};

// ========== 权限请求队列 ==========

/// Permission request queue for handling multiple pending permission requests sequentially
class PermissionRequestQueue {
public:
    /// Add a request to the queue, returns a request ID
    size_t enqueue(PermissionRequest request);

    /// Dequeue the next request (returns nullopt if empty)
    std::optional<std::pair<size_t, PermissionRequest>> dequeue();

    /// Peek at the front request without removing
    std::optional<std::pair<size_t, PermissionRequest>> peek() const;

    /// Get the number of pending requests
    size_t size() const;

    /// Check if queue is empty
    bool empty() const;

    /// Remove a specific request by ID
    bool remove(size_t requestId);

    /// Clear all pending requests
    void clear();

private:
    std::vector<std::pair<size_t, PermissionRequest>> queue_;
    size_t nextId_ = 1;
};

// ========== 辅助函数 ==========

/// 将权限模式转为字符串
inline String permissionModeToString(PermissionMode mode) {
    switch (mode) {
        case PermissionMode::Default: return "default";
        case PermissionMode::AcceptEdits: return "acceptEdits";
        case PermissionMode::Bypass: return "bypassPermissions";
        case PermissionMode::DontAsk: return "dontAsk";
        case PermissionMode::Plan: return "plan";
        case PermissionMode::Auto: return "auto";
    }
    return "default";
}

/// 从字符串解析权限模式
inline std::optional<PermissionMode> parsePermissionMode(const String& s) {
    if (s == "default") return PermissionMode::Default;
    if (s == "acceptEdits") return PermissionMode::AcceptEdits;
    if (s == "bypassPermissions") return PermissionMode::Bypass;
    if (s == "dontAsk") return PermissionMode::DontAsk;
    if (s == "plan") return PermissionMode::Plan;
    if (s == "auto") return PermissionMode::Auto;
    return std::nullopt;
}

/// 将权限行为转为字符串
inline String permissionBehaviorToString(PermissionBehavior b) {
    switch (b) {
        case PermissionBehavior::Allow: return "allow";
        case PermissionBehavior::Deny: return "deny";
        case PermissionBehavior::Ask: return "ask";
    }
    return "allow";
}

/// 从字符串解析权限行为
inline std::optional<PermissionBehavior> parsePermissionBehavior(const String& s) {
    if (s == "allow") return PermissionBehavior::Allow;
    if (s == "deny") return PermissionBehavior::Deny;
    if (s == "ask") return PermissionBehavior::Ask;
    return std::nullopt;
}

/// 将权限选择转为字符串
inline String permissionChoiceToString(PermissionChoice choice) {
    switch (choice) {
        case PermissionChoice::AllowOnce: return "Y";
        case PermissionChoice::AllowSession: return "S";
        case PermissionChoice::AlwaysAllow: return "A";
        case PermissionChoice::DenyOnce: return "N";
        case PermissionChoice::AlwaysDeny: return "D";
    }
    return "N";
}

/// 将权限规则来源转为字符串
inline String permissionRuleSourceToString(PermissionRuleSource source) {
    switch (source) {
        case PermissionRuleSource::PolicySettings: return "policy";
        case PermissionRuleSource::FlagSettings: return "flag";
        case PermissionRuleSource::UserSettings: return "user";
        case PermissionRuleSource::ProjectSettings: return "project";
        case PermissionRuleSource::LocalSettings: return "local";
        case PermissionRuleSource::CliArg: return "cli";
        case PermissionRuleSource::Command: return "command";
        case PermissionRuleSource::Session: return "session";
    }
    return "user";
}

/// 从字符串解析权限规则来源
inline PermissionRuleSource stringToPermissionRuleSource(const String& s) {
    if (s == "policy") return PermissionRuleSource::PolicySettings;
    if (s == "flag") return PermissionRuleSource::FlagSettings;
    if (s == "cli") return PermissionRuleSource::CliArg;
    if (s == "command") return PermissionRuleSource::Command;
    if (s == "session") return PermissionRuleSource::Session;
    if (s == "project") return PermissionRuleSource::ProjectSettings;
    if (s == "local") return PermissionRuleSource::LocalSettings;
    return PermissionRuleSource::UserSettings;
}

/// Get source priority (higher = evaluated first)
inline int permissionRuleSourcePriority(PermissionRuleSource source) {
    switch (source) {
        case PermissionRuleSource::PolicySettings: return 7;
        case PermissionRuleSource::FlagSettings: return 6;
        case PermissionRuleSource::CliArg: return 5;
        case PermissionRuleSource::Command: return 4;
        case PermissionRuleSource::Session: return 3;
        case PermissionRuleSource::UserSettings: return 2;
        case PermissionRuleSource::ProjectSettings: return 1;
        case PermissionRuleSource::LocalSettings: return 0;
    }
    return 0;
}

} // namespace claude
