#include <claude/permission/PolicyLimits.hpp>
#include <cstdlib>

namespace claude {

void PolicyLimits::loadFromConfig(const Config& config) {
    std::lock_guard lock(mutex_);
    config_ = config;
    if (config.disableBypassMode) {
        spdlog::debug("Policy: bypass mode disabled (source: {})", sourceDescription());
    }
    if (config.disableAutoMode) {
        spdlog::debug("Policy: auto mode disabled (source: {})", sourceDescription());
    }
}

void PolicyLimits::loadFromEnv() {
    std::lock_guard lock(mutex_);

    // CLAUDE_DISABLE_BYPASS - 禁止 bypass 模式
    const char* disableBypass = std::getenv("CLAUDE_DISABLE_BYPASS");
    if (disableBypass && (String(disableBypass) == "1" || String(disableBypass) == "true")) {
        config_.disableBypassMode = true;
        config_.source = Source::Environment;
        spdlog::debug("Policy: bypass mode disabled via environment variable");
    }

    // CLAUDE_MAX_PERMISSION_MODE - 最大允许的权限模式
    const char* maxMode = std::getenv("CLAUDE_MAX_PERMISSION_MODE");
    if (maxMode) {
        config_.maxModeEnforced = true;
        config_.maxAllowedMode = String(maxMode);
        config_.source = Source::Environment;
        spdlog::debug("Policy: max permission mode set to '{}' via environment", maxMode);
    }

    // CLAUDE_DISABLE_AUTO_MODE - 禁止 auto 模式
    const char* disableAuto = std::getenv("CLAUDE_DISABLE_AUTO_MODE");
    if (disableAuto && (String(disableAuto) == "1" || String(disableAuto) == "true")) {
        config_.disableAutoMode = true;
        spdlog::debug("Policy: auto mode disabled via environment variable");
    }
}

void PolicyLimits::loadFromJson(const Json& json) {
    std::lock_guard lock(mutex_);

    if (json.contains("disableBypassMode")) {
        config_.disableBypassMode = json.value("disableBypassMode", false);
    }
    if (json.contains("disableAutoMode")) {
        config_.disableAutoMode = json.value("disableAutoMode", false);
    }
    if (json.contains("disableAcceptEdits")) {
        config_.disableAcceptEdits = json.value("disableAcceptEdits", false);
    }
    if (json.contains("maxAllowedMode")) {
        config_.maxModeEnforced = true;
        config_.maxAllowedMode = json.value("maxAllowedMode", "acceptEdits");
    }
    if (json.contains("notification")) {
        config_.notification = json.value("notification", "");
    }
    config_.source = Source::LocalConfig;
}

bool PolicyLimits::isModeAllowed(PermissionMode mode) const {
    std::lock_guard lock(mutex_);

    switch (mode) {
        case PermissionMode::Bypass:
            return !config_.disableBypassMode;
        case PermissionMode::Auto:
            return !config_.disableAutoMode;
        case PermissionMode::AcceptEdits:
            return !config_.disableAcceptEdits;
        case PermissionMode::Default:
        case PermissionMode::DontAsk:
        case PermissionMode::Plan:
            return true;
    }
    return true;
}

bool PolicyLimits::isBypassDisabled() const {
    std::lock_guard lock(mutex_);
    return config_.disableBypassMode;
}

bool PolicyLimits::isAutoDisabled() const {
    std::lock_guard lock(mutex_);
    return config_.disableAutoMode;
}

String PolicyLimits::getNotification() const {
    std::lock_guard lock(mutex_);
    return config_.notification;
}

String PolicyLimits::sourceDescription() const {
    switch (config_.source) {
        case Source::None: return "none";
        case Source::LocalConfig: return "local-config";
        case Source::Remote: return "remote-managed";
        case Source::Environment: return "environment";
    }
    return "unknown";
}

} // namespace claude
