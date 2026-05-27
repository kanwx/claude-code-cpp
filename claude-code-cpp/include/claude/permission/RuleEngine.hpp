#pragma once

#include "PermissionTypes.hpp"
#include "PermissionSettings.hpp"
#include "DangerousPatterns.hpp"
#include "DenialTracker.hpp"
#include "BashClassifier.hpp"
#include "YoloClassifier.hpp"
#include "PermissionStore.hpp"
#include "../core/Types.hpp"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <functional>
#include <mutex>

namespace claude {

// Forward declaration
class SettingsManager;

/// 权限规则引擎 —— 分层决策设计，匹配原版 TS permissions.ts
///
/// 决策流程 (3-tier: deny > ask > allow):
/// 1. 检查全局模式（BYPASS → 全部允许，PLAN → 仅读操作）
/// 2. Policy gating: managed-only → 只评估 PolicySettings 规则
/// 3. 检查 deny 规则 → 匹配则 DENY
/// 4. 检查 ask 规则 → 匹配则 ASK (强制提示)
/// 5. 检查 allow 规则 → 匹配则 ALLOW
/// 6. 只读工具 → ALLOW
/// 7. ACCEPT_EDITS 模式下文件操作 → ALLOW
/// 8. AUTO 模式下 YOLO 分类器 → 分类决定
/// 9. DONT_ASK 模式 → DENY
/// 10. Bash AI 分类器
/// 11. Sandbox override: autoAllowBashIfSandboxed → skip ask
/// 12. 检查危险命令 → 强制 ASK
/// 13. 默认 → ASK
class RuleEngine {
public:
    explicit RuleEngine(PermissionSettings& settings);

    // ========== 核心评估 ==========

    /// 评估工具调用的权限
    PermissionDecision evaluate(
        const String& toolName,
        const Json& input,
        bool isReadOnly
    );

    /// 评估工具调用权限 (带对话历史，用于 YOLO 分类器)
    PermissionDecision evaluate(
        const String& toolName,
        const Json& input,
        bool isReadOnly,
        const std::vector<Message>& transcript
    );

    /// 评估工具调用权限 (带 sandbox 标记)
    PermissionDecision evaluate(
        const String& toolName,
        const Json& input,
        bool isReadOnly,
        bool isSandboxed
    );

    /// 评估工具调用权限 (完整参数)
    PermissionDecision evaluate(
        const String& toolName,
        const Json& input,
        bool isReadOnly,
        bool isSandboxed,
        const std::vector<Message>* transcript
    );

    /// 应用用户选择 (source-aware: routes to correct settings source)
    void applyChoice(
        PermissionChoice choice,
        const String& toolName,
        const String& command
    );

    /// 应用用户选择 (with explicit destination source)
    void applyChoiceToSource(
        PermissionChoice choice,
        const String& toolName,
        const String& command,
        PermissionRuleSource destination
    );

    // ========== Settings Manager 连接 ==========

    /// Set the settings manager for source-aware persistence
    void setSettingsManager(SettingsManager* manager) { settingsManager_ = manager; }

    SettingsManager* settingsManager() { return settingsManager_; }

    // ========== 拒绝追踪 ==========

    DenialTracker& denialTracker() { return denialTracker_; }
    const DenialTracker& denialTracker() const { return denialTracker_; }

    // ========== Bash 分类器 ==========

    BashClassifier& bashClassifier() { return bashClassifier_; }
    const BashClassifier& bashClassifier() const { return bashClassifier_; }

    // ========== YOLO 分类器 ==========

    YoloClassifier& yoloClassifier() { return yoloClassifier_; }
    const YoloClassifier& yoloClassifier() const { return yoloClassifier_; }

    // ========== 设置访问 ==========

    PermissionSettings& settings() { return settings_; }
    const PermissionSettings& settings() const { return settings_; }

    // ========== 权限请求队列 ==========

    PermissionRequestQueue& requestQueue() { return requestQueue_; }
    const PermissionRequestQueue& requestQueue() const { return requestQueue_; }

    // ========== Disk sync ==========

    /// Re-sync all disk-based rules from settings files
    void syncFromDisk();

private:
    // ========== 内部评估 ==========

    /// 内部评估实现 (共享逻辑)
    PermissionDecision evaluateInner(
        const String& toolName,
        const Json& input,
        bool isReadOnly,
        bool isSandboxed,
        const std::vector<Message>* transcript
    );

    // ========== 规则匹配 ==========

    /// 检查规则是否匹配 (includes MCP and Agent matching)
    bool matchesRule(
        const PermissionRule& rule,
        const String& toolName,
        const String& command
    ) const;

    /// 简单 glob 匹配 (* = 任意字符, ? = 单个字符)
    bool matchesGlob(const String& text, const String& pattern) const;

    /// 从工具参数中提取命令文本
    String extractCommand(const String& toolName, const Json& input) const;

    /// 提取命令前缀 (第一个空格前的部分)
    String extractCommandPrefix(const String& command) const;

    // ========== MCP 工具匹配 ==========

    /// Check if an MCP tool name matches a rule tool name
    /// Supports: "mcp__server__tool" (exact), "mcp__server" (all tools),
    /// "mcp__server__*" (wildcard all tools)
    bool matchesMcpTool(const String& ruleToolName, const String& actualToolName) const;

    // ========== Agent deny rule matching ==========

    /// Check if tool name matches an Agent(type) deny rule
    /// e.g., rule "Agent(Explore)" matches tool name "Agent(Explore)"
    bool matchesAgentRule(const String& ruleToolName, const String& actualToolName) const;

    // ========== Source routing ==========

    /// Determine the appropriate source for an "always allow/deny" choice
    /// based on context: user → UserSettings, project → ProjectSettings, etc.
    PermissionRuleSource determineChoiceSource(PermissionChoice choice) const;

private:
    PermissionSettings& settings_;
    SettingsManager* settingsManager_ = nullptr;
    DenialTracker denialTracker_;
    BashClassifier bashClassifier_;
    YoloClassifier yoloClassifier_;
    PermissionRequestQueue requestQueue_;
    mutable std::recursive_mutex rulesMutex_;  // guards settings_ access, denialTracker_, classifiers

    // 只读工具集合
    static const std::unordered_set<String> READ_ONLY_TOOLS;

    // 文件编辑工具集合
    static const std::unordered_set<String> FILE_EDIT_TOOLS;
};

} // namespace claude
