#include <claude/core/HookManager.hpp>
#include <claude/utils/Process.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {

// ========== Event Type Mapping ==========

std::optional<HookType> HookManager::parseEventType(const String& name) {
    static const std::map<String, HookType> mapping = {
        {"PreToolUse", HookType::PreToolUse},
        {"PostToolUse", HookType::PostToolUse},
        {"PostToolUseFailure", HookType::PostToolUseFailure},
        {"SessionStart", HookType::SessionStart},
        {"SessionEnd", HookType::SessionEnd},
        {"UserPromptSubmit", HookType::UserPromptSubmit},
        {"PrePrompt", HookType::PrePrompt},
        {"PostResponse", HookType::PostResponse},
        {"Stop", HookType::Stop},
        {"StopFailure", HookType::StopFailure},
        {"SubagentStart", HookType::SubagentStart},
        {"SubagentStop", HookType::SubagentStop},
        {"PreCompact", HookType::PreCompact},
        {"PostCompact", HookType::PostCompact},
        {"PermissionRequest", HookType::PermissionRequest},
        {"PermissionDenied", HookType::PermissionDenied},
        {"Setup", HookType::Setup},
        {"TaskCreated", HookType::TaskCreated},
        {"TaskCompleted", HookType::TaskCompleted},
        {"TeammateIdle", HookType::TeammateIdle},
        {"Elicitation", HookType::Elicitation},
        {"ElicitationResult", HookType::ElicitationResult},
        {"ConfigChange", HookType::ConfigChange},
        {"WorktreeCreate", HookType::WorktreeCreate},
        {"WorktreeRemove", HookType::WorktreeRemove},
        {"InstructionsLoaded", HookType::InstructionsLoaded},
        {"CwdChanged", HookType::CwdChanged},
        {"FileChanged", HookType::FileChanged},
        {"Notification", HookType::Notification},
    };
    auto it = mapping.find(name);
    return it != mapping.end() ? std::optional<HookType>(it->second) : std::nullopt;
}

String HookManager::eventTypeToString(HookType type) {
    static const std::map<HookType, String> mapping = {
        {HookType::PreToolUse, "PreToolUse"},
        {HookType::PostToolUse, "PostToolUse"},
        {HookType::PostToolUseFailure, "PostToolUseFailure"},
        {HookType::SessionStart, "SessionStart"},
        {HookType::SessionEnd, "SessionEnd"},
        {HookType::UserPromptSubmit, "UserPromptSubmit"},
        {HookType::PrePrompt, "PrePrompt"},
        {HookType::PostResponse, "PostResponse"},
        {HookType::Stop, "Stop"},
        {HookType::StopFailure, "StopFailure"},
        {HookType::SubagentStart, "SubagentStart"},
        {HookType::SubagentStop, "SubagentStop"},
        {HookType::PreCompact, "PreCompact"},
        {HookType::PostCompact, "PostCompact"},
        {HookType::PermissionRequest, "PermissionRequest"},
        {HookType::PermissionDenied, "PermissionDenied"},
        {HookType::Setup, "Setup"},
        {HookType::TaskCreated, "TaskCreated"},
        {HookType::TaskCompleted, "TaskCompleted"},
        {HookType::TeammateIdle, "TeammateIdle"},
        {HookType::Elicitation, "Elicitation"},
        {HookType::ElicitationResult, "ElicitationResult"},
        {HookType::ConfigChange, "ConfigChange"},
        {HookType::WorktreeCreate, "WorktreeCreate"},
        {HookType::WorktreeRemove, "WorktreeRemove"},
        {HookType::InstructionsLoaded, "InstructionsLoaded"},
        {HookType::CwdChanged, "CwdChanged"},
        {HookType::FileChanged, "FileChanged"},
        {HookType::Notification, "Notification"},
    };
    auto it = mapping.find(type);
    return it != mapping.end() ? it->second : "Unknown";
}

// ========== HookConfig ==========

std::expected<HookConfig, String> HookConfig::fromJson(const Json& j, HookType eventType) {
    HookConfig config;
    config.event = eventType;
    config.type = j.value("type", "command");
    config.id = j.value("id", "");

    if (config.type == "command") {
        if (!j.contains("command")) {
            return std::unexpected("Command hook missing 'command' field");
        }
        config.command = j["command"];
    } else if (config.type == "prompt") {
        if (!j.contains("prompt")) {
            return std::unexpected("Prompt hook missing 'prompt' field");
        }
        config.prompt = j["prompt"];
    } else {
        return std::unexpected("Unknown hook type: " + config.type);
    }

    if (j.contains("shell")) config.shell = j["shell"];
    if (j.contains("timeout")) config.timeout = j["timeout"];
    if (j.contains("statusMessage")) config.statusMessage = j["statusMessage"];
    if (j.contains("if")) config.ifCondition = j["if"];
    if (j.contains("once")) config.once = j["once"];
    if (j.contains("async")) config.async = j["async"];
    if (j.contains("asyncRewake")) config.asyncRewake = j["asyncRewake"];

    return config;
}

Json HookConfig::toJson() const {
    Json j = {{"type", type}};
    if (!id.empty()) j["id"] = id;
    if (type == "command") {
        j["command"] = command;
        if (shell) j["shell"] = *shell;
        if (timeout) j["timeout"] = *timeout;
        if (statusMessage) j["statusMessage"] = *statusMessage;
    } else if (type == "prompt") {
        j["prompt"] = prompt;
    }
    if (ifCondition) j["if"] = *ifCondition;
    if (once) j["once"] = true;
    if (async) j["async"] = true;
    if (asyncRewake) j["asyncRewake"] = true;
    return j;
}

bool HookConfig::matchesCondition(const HookContext& ctx) const {
    if (!ifCondition) return true;
    // Simple condition matching: "tool_name:Bash" or "command_prefix:npm"
    String cond = *ifCondition;
    auto colonPos = cond.find(':');
    if (colonPos == String::npos) return true;

    String field = cond.substr(0, colonPos);
    String value = cond.substr(colonPos + 1);

    if (field == "tool_name") return ctx.toolName == value;
    if (field == "command_prefix") return ctx.permissionCommandPrefix.find(value) == 0;
    if (field == "subagent_type") return ctx.subagentType == value;

    return true;
}

// ========== HookManager Implementation ==========

std::expected<void, String> HookManager::loadFromConfig(const Json& config) {
    std::lock_guard lock(hooksMutex_);
    if (!config.is_object()) return {};

    for (const auto& [eventName, hooksArray] : config.items()) {
        auto eventType = parseEventType(eventName);
        if (!eventType) {
            spdlog::warn("HookManager: unknown event type: {}", eventName);
            continue;
        }

        if (!hooksArray.is_array()) {
            spdlog::warn("HookManager: hooks for '{}' must be an array", eventName);
            continue;
        }

        for (const auto& hookJson : hooksArray) {
            auto hookConfig = HookConfig::fromJson(hookJson, *eventType);
            if (!hookConfig) {
                spdlog::warn("HookManager: invalid hook config: {}", hookConfig.error());
                continue;
            }
            declarativeHooks_.push_back(std::move(*hookConfig));
        }
    }

    spdlog::debug("HookManager: loaded {} declarative hooks", declarativeHooks_.size());
    return {};
}

Json HookManager::saveToConfig() const {
    std::lock_guard lock(hooksMutex_);
    Json config;
    for (const auto& hook : declarativeHooks_) {
        String eventName = eventTypeToString(hook.event);
        config[eventName].push_back(hook.toJson());
    }
    return config;
}

HookResult HookManager::execute(HookType type, HookContext& ctx) {
    ctx.eventType = type;

    // Copy hooks under lock, then execute from copies — don't hold lock during execution
    std::vector<HookFn> codeHooksCopy;
    std::vector<HookConfig> declarativeCopy;
    ApiClient* apiClientCopy = nullptr;
    {
        std::lock_guard lock(hooksMutex_);
        auto it = codeHooks_.find(type);
        if (it != codeHooks_.end()) codeHooksCopy = it->second;
        declarativeCopy = declarativeHooks_;
        apiClientCopy = apiClient_;
    }

    // Execute code-based hooks first (from copy)
    for (const auto& hook : codeHooksCopy) {
        auto result = hook(ctx);
        if (result.shouldAbort()) return result;
    }

    // Execute declarative hooks (from copy)
    std::vector<size_t> ranIndices;
    for (size_t i = 0; i < declarativeCopy.size(); ++i) {
        auto& hook = declarativeCopy[i];
        if (hook.event != type) continue;
        if (hook.once && hook.hasRun) continue;
        if (!hook.matchesCondition(ctx)) continue;

        HookResult result;
        if (hook.type == "command") {
            result = executeCommandHook(hook, ctx);
        } else if (hook.type == "prompt") {
            // Use the snapshotted API client for prompt hooks
            result = executePromptHook(hook, ctx, apiClientCopy);
        }

        // Track which hooks ran, to mark hasRun after execution
        if (hook.once) {
            ranIndices.push_back(i);
        }
        if (result.shouldAbort()) {
            // Apply hasRun mutations before returning
            if (!ranIndices.empty()) {
                std::lock_guard lock(hooksMutex_);
                for (size_t idx : ranIndices) {
                    if (idx < declarativeHooks_.size() &&
                        declarativeHooks_[idx].event == type) {
                        declarativeHooks_[idx].hasRun = true;
                    }
                }
            }
            return result;
        }
    }

    // Apply hasRun mutations after all hooks complete
    if (!ranIndices.empty()) {
        std::lock_guard lock(hooksMutex_);
        for (size_t idx : ranIndices) {
            if (idx < declarativeHooks_.size() &&
                declarativeHooks_[idx].event == type) {
                declarativeHooks_[idx].hasRun = true;
            }
        }
    }

    return HookResult::ok();
}

HookResult HookManager::executeCommandHook(const HookConfig& config, HookContext& ctx) {
    String cmd = substituteTemplateVars(config.command, ctx);
    String shell = config.shell.value_or("/bin/bash");
    int timeout = config.timeout.value_or(30000);

    // Convert ms to seconds for Process::execute
    int timeoutSec = std::max(1, timeout / 1000);

    spdlog::debug("HookManager: executing command hook: {}", cmd);

    auto result = Process::execute(cmd + " 2>&1", std::filesystem::current_path(), timeoutSec);

    if (result.exitCode != 0) {
        spdlog::warn("HookManager: command hook failed (exit {}): {}", result.exitCode, result.stderr);
    }

    HookResult hr;
    hr.output = result.stdout + result.stderr;

    // A non-zero exit from command hook means abort
    if (result.exitCode != 0) {
        hr.action = HookResult::Abort;
        hr.reason = "Hook command returned non-zero exit code";
    }

    return hr;
}

HookResult HookManager::executePromptHook(const HookConfig& config, HookContext& ctx, ApiClient* client) {
    if (!client) {
        spdlog::warn("HookManager: cannot execute prompt hook — no API client");
        return HookResult::ok();
    }

    String promptText = substituteTemplateVars(config.prompt, ctx);

    // Build a simple API call for the LLM evaluation
    Json messages = Json::array({
        {{"role", "user"}, {"content", promptText}}
    });

    Json request = {
        {"model", "claude-haiku-4-5-20251001"},
        {"max_tokens", 1024},
        {"messages", messages}
    };

    try {
        auto response = client->call({
            {{"role", "user"}, {"content", promptText}}
        }, {});

        if (response) {
            String llmResponse = response->value("content", Json::array())[0]
                                     .value("text", "");
            HookResult hr;
            hr.output = llmResponse;

            // If the LLM says "ABORT" or "DENY", abort
            String lower = llmResponse;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower.find("abort") != String::npos || lower.find("deny") != String::npos) {
                hr.action = HookResult::Abort;
                hr.reason = "Prompt hook evaluation returned abort";
            }

            return hr;
        }
    } catch (const std::exception& e) {
        spdlog::warn("HookManager: prompt hook failed: {}", e.what());
    }

    return HookResult::ok();
}

bool HookManager::hasHooks(HookType type) const {
    std::lock_guard lock(hooksMutex_);
    auto it = codeHooks_.find(type);
    if (it != codeHooks_.end() && !it->second.empty()) return true;

    for (const auto& hook : declarativeHooks_) {
        if (hook.event == type) return true;
    }
    return false;
}

// ========== Private Helpers ==========

bool HookManager::evaluateCondition(const String& condition, const HookContext& ctx) const {
    auto colonPos = condition.find(':');
    if (colonPos == String::npos) return true;

    String field = condition.substr(0, colonPos);
    String value = condition.substr(colonPos + 1);

    if (field == "tool_name") return ctx.toolName == value;
    if (field == "command_prefix") return ctx.permissionCommandPrefix.find(value) == 0;
    if (field == "subagent_type") return ctx.subagentType == value;
    if (field == "session_id") return ctx.sessionId == value;
    if (field == "task_id") return ctx.taskId == value;

    return true;
}

HookResult HookManager::runShellCommand(const String& command, const String& shell,
                                         int timeout, HookContext& ctx) const {
    int timeoutSec = std::max(1, timeout / 1000);
    auto result = Process::execute(command + " 2>&1", std::filesystem::current_path(), timeoutSec);

    HookResult hr;
    hr.output = result.stdout + result.stderr;

    if (result.exitCode != 0) {
        hr.action = HookResult::Abort;
        hr.reason = "Hook command returned non-zero exit code";
    }

    return hr;
}

String HookManager::substituteTemplateVars(const String& tmpl, const HookContext& ctx) const {
    String result = tmpl;

    // Replace {{tool_name}} with the tool name
    String toolNameVar = "{{tool_name}}";
    auto pos = result.find(toolNameVar);
    while (pos != String::npos) {
        result.replace(pos, toolNameVar.length(), ctx.toolName);
        pos = result.find(toolNameVar, pos + ctx.toolName.length());
    }

    // Replace {{session_id}}
    String sessionIdVar = "{{session_id}}";
    pos = result.find(sessionIdVar);
    while (pos != String::npos) {
        result.replace(pos, sessionIdVar.length(), ctx.sessionId);
        pos = result.find(sessionIdVar, pos + ctx.sessionId.length());
    }

    // Replace {{task_id}}
    String taskIdVar = "{{task_id}}";
    pos = result.find(taskIdVar);
    while (pos != String::npos) {
        result.replace(pos, taskIdVar.length(), ctx.taskId);
        pos = result.find(taskIdVar, pos + ctx.taskId.length());
    }

    return result;
}

} // namespace claude
