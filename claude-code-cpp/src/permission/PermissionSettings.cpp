#include <claude/permission/PermissionSettings.hpp>
#include <claude/config/SettingsManager.hpp>
#include <spdlog/spdlog.h>

namespace claude {

// ========== 规则格式化 ==========

String PermissionSettings::formatRule(const PermissionRule& rule) {
    return PermissionRuleParser::format(rule);
}

// ========== Source-aware rule persistence ==========

void PermissionSettings::addPermissionRulesToSettings(
    PermissionRuleSource source,
    const std::vector<PermissionRule>& rules,
    SettingsManager* settingsManager
) {
    std::lock_guard lock(mutex_);

    // Check managed-only: block non-policy rule changes
    if (managedOnly_ && source != PermissionRuleSource::PolicySettings) {
        spdlog::warn("Cannot add rules to source {}: managed-only mode is active, "
                     "only policy rules are allowed",
                     permissionRuleSourceToString(source));
        return;
    }

    // Add rules to in-memory storage
    auto& target = getRulesVector(source);
    for (const auto& rule : rules) {
        PermissionRule r = rule;
        r.source = source;  // Ensure source matches destination
        target.push_back(r);
        spdlog::debug("Persisted rule to {}: {} {} {}",
                     permissionRuleSourceToString(source),
                     r.toolName, r.ruleContent,
                     permissionBehaviorToString(r.behavior));
    }

    // Persist to disk via SettingsManager if available
    if (settingsManager) {
        try {
            // Map PermissionRuleSource to SettingSource
            SettingSource settingSource = SettingSource::UserSettings;
            switch (source) {
                case PermissionRuleSource::PolicySettings:
                    settingSource = SettingSource::PolicySettings; break;
                case PermissionRuleSource::FlagSettings:
                    settingSource = SettingSource::FlagSettings; break;
                case PermissionRuleSource::UserSettings:
                    settingSource = SettingSource::UserSettings; break;
                case PermissionRuleSource::ProjectSettings:
                    settingSource = SettingSource::ProjectSettings; break;
                case PermissionRuleSource::LocalSettings:
                    settingSource = SettingSource::LocalSettings; break;
                default:
                    // Session, CliArg, Command are not persisted to settings files
                    return;
            }

            // Read current settings for this source
            Json currentSettings = settingsManager->getSettingsForSource(settingSource);

            // Build the permission rules arrays
            auto& allRules = getRulesVector(source);

            // Group rules by behavior
            Json allowArr = Json::array();
            Json denyArr = Json::array();
            Json askArr = Json::array();

            for (const auto& r : allRules) {
                String formatted = PermissionRuleParser::format(r);
                switch (r.behavior) {
                    case PermissionBehavior::Allow: allowArr.push_back(formatted); break;
                    case PermissionBehavior::Deny: denyArr.push_back(formatted); break;
                    case PermissionBehavior::Ask: askArr.push_back(formatted); break;
                }
            }

            // Update the permissions section in the settings JSON
            if (!currentSettings.contains("permissions")) {
                currentSettings["permissions"] = Json::object();
            }
            currentSettings["permissions"]["allow"] = allowArr;
            currentSettings["permissions"]["deny"] = denyArr;
            currentSettings["permissions"]["ask"] = askArr;

            // Write back through SettingsManager
            settingsManager->updateSettingsForSource(settingSource, currentSettings);
            settingsManager->saveSource(settingSource);

            spdlog::debug("Persisted {} rules to {} settings file",
                         rules.size(), permissionRuleSourceToString(source));
        } catch (const std::exception& e) {
            spdlog::error("Failed to persist rules to settings: {}", e.what());
        }
    }
}

bool PermissionSettings::deletePermissionRule(
    PermissionRuleSource source,
    size_t ruleIndex,
    SettingsManager* settingsManager
) {
    std::lock_guard lock(mutex_);

    // Check managed-only
    if (managedOnly_ && source != PermissionRuleSource::PolicySettings) {
        spdlog::warn("Cannot delete rules from source {}: managed-only mode is active",
                     permissionRuleSourceToString(source));
        return false;
    }

    auto& target = getRulesVector(source);
    if (ruleIndex >= target.size()) {
        return false;
    }

    auto it = target.begin() + static_cast<ptrdiff_t>(ruleIndex);
    String ruleStr = PermissionRuleParser::format(*it);
    target.erase(it);

    spdlog::debug("Deleted rule {} from source {}", ruleStr,
                 permissionRuleSourceToString(source));

    // Persist the change
    if (settingsManager) {
        try {
            SettingSource settingSource = SettingSource::UserSettings;
            switch (source) {
                case PermissionRuleSource::PolicySettings:
                    settingSource = SettingSource::PolicySettings; break;
                case PermissionRuleSource::FlagSettings:
                    settingSource = SettingSource::FlagSettings; break;
                case PermissionRuleSource::UserSettings:
                    settingSource = SettingSource::UserSettings; break;
                case PermissionRuleSource::ProjectSettings:
                    settingSource = SettingSource::ProjectSettings; break;
                case PermissionRuleSource::LocalSettings:
                    settingSource = SettingSource::LocalSettings; break;
                default:
                    return true;  // Non-disk sources don't need persistence
            }

            Json currentSettings = settingsManager->getSettingsForSource(settingSource);

            Json allowArr = Json::array();
            Json denyArr = Json::array();
            Json askArr = Json::array();

            for (const auto& r : target) {
                String formatted = PermissionRuleParser::format(r);
                switch (r.behavior) {
                    case PermissionBehavior::Allow: allowArr.push_back(formatted); break;
                    case PermissionBehavior::Deny: denyArr.push_back(formatted); break;
                    case PermissionBehavior::Ask: askArr.push_back(formatted); break;
                }
            }

            if (!currentSettings.contains("permissions")) {
                currentSettings["permissions"] = Json::object();
            }
            currentSettings["permissions"]["allow"] = allowArr;
            currentSettings["permissions"]["deny"] = denyArr;
            currentSettings["permissions"]["ask"] = askArr;

            settingsManager->updateSettingsForSource(settingSource, currentSettings);
            settingsManager->saveSource(settingSource);
        } catch (const std::exception& e) {
            spdlog::error("Failed to persist rule deletion to settings: {}", e.what());
        }
    }

    return true;
}

void PermissionSettings::syncPermissionRulesFromDisk(SettingsManager& settingsManager) {
    std::lock_guard lock(mutex_);

    // Clear all disk-based rule sources (keep session and CLI-arg which are ephemeral)
    policyRules_.clear();
    flagRules_.clear();
    userRules_.clear();
    projectRules_.clear();
    localRules_.clear();
    commandRules_.clear();

    // Load rules from each settings source
    auto loadFromSource = [&](SettingSource settingSource, PermissionRuleSource ruleSource) {
        try {
            Json sourceSettings = settingsManager.getSettingsForSource(settingSource);
            if (!sourceSettings.contains("permissions")) return;

            const auto& perms = sourceSettings["permissions"];
            auto parsed = PermissionRuleParser::parseAll(perms, ruleSource);
            auto& target = getRulesVector(ruleSource);

            for (auto& rule : parsed) {
                rule.source = ruleSource;
                target.push_back(std::move(rule));
            }

            spdlog::debug("Synced {} rules from {} settings",
                         target.size(), permissionRuleSourceToString(ruleSource));
        } catch (const std::exception& e) {
            spdlog::error("Failed to sync rules from {} settings: {}",
                         permissionRuleSourceToString(ruleSource), e.what());
        }
    };

    loadFromSource(SettingSource::PolicySettings, PermissionRuleSource::PolicySettings);
    loadFromSource(SettingSource::FlagSettings, PermissionRuleSource::FlagSettings);
    loadFromSource(SettingSource::UserSettings, PermissionRuleSource::UserSettings);
    loadFromSource(SettingSource::ProjectSettings, PermissionRuleSource::ProjectSettings);
    loadFromSource(SettingSource::LocalSettings, PermissionRuleSource::LocalSettings);

    // Update managed-only flag from settings manager
    managedOnly_ = settingsManager.shouldAllowManagedPermissionRulesOnly();
    if (managedOnly_) {
        // Clear non-policy rules when managed-only is active
        flagRules_.clear();
        userRules_.clear();
        projectRules_.clear();
        localRules_.clear();
        cliArgRules_.clear();
        commandRules_.clear();
        sessionRules_.clear();
        spdlog::debug("Policy gating active after sync: only policy rules evaluated");
    }
}

// ========== 持久化 (file-based) ==========

bool PermissionSettings::saveToFile(const std::filesystem::path& path) const {
    std::lock_guard lock(mutex_);

    try {
        Json data = {
            {"version", 2},
            {"mode", permissionModeToString(currentMode_)},
            {"managedOnly", managedOnly_},
            {"autoAllowBashIfSandboxed", autoAllowBashIfSandboxed_},
            {"policyRules", PermissionRuleParser::formatArray(policyRules_)},
            {"flagRules", PermissionRuleParser::formatArray(flagRules_)},
            {"userRules", Json::array()},
            {"projectRules", Json::array()},
            {"localRules", Json::array()},
            {"cliArgRules", PermissionRuleParser::formatArray(cliArgRules_)},
            {"commandRules", PermissionRuleParser::formatArray(commandRules_)},
            {"sessionRules", PermissionRuleParser::formatArray(sessionRules_)},
            {"decisionHistory", decisionHistory_}
        };

        // Use structured format for disk-backed rules
        auto writeRules = [&](const char* key, const std::vector<PermissionRule>& rules) {
            Json arr = Json::array();
            for (const auto& rule : rules) {
                arr.push_back(ruleToJson(rule));
            }
            data[key] = arr;
        };
        writeRules("userRules", userRules_);
        writeRules("projectRules", projectRules_);
        writeRules("localRules", localRules_);

        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << data.dump(2);

        spdlog::debug("Permission settings saved to: {}", path.string());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to save permission settings: {}", e.what());
        return false;
    }
}

bool PermissionSettings::loadFromFile(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);

    try {
        std::ifstream file(path);
        if (!file) {
            spdlog::debug("No permission settings file found: {}", path.string());
            return false;
        }

        Json data = Json::parse(file);

        // Load mode
        if (data.contains("mode")) {
            auto mode = parsePermissionMode(data["mode"].get<String>());
            if (mode) currentMode_ = *mode;
        }

        // Load managed-only flag
        if (data.contains("managedOnly")) {
            managedOnly_ = data["managedOnly"].get<bool>();
        }

        // Load sandbox setting
        if (data.contains("autoAllowBashIfSandboxed")) {
            autoAllowBashIfSandboxed_ = data["autoAllowBashIfSandboxed"].get<bool>();
        }

        // Helper: load rules from JSON (supports both structured and string format)
        auto loadRules = [&](const char* key, std::vector<PermissionRule>& target,
                             PermissionRuleSource defaultSource) {
            target.clear();
            if (!data.contains(key)) return;
            const auto& arr = data[key];
            if (!arr.is_array()) return;

            for (const auto& item : arr) {
                if (item.is_string()) {
                    // String format: "Bash(npm:*)"
                    auto parsed = PermissionRuleParser::parse(
                        item.get<String>(),
                        PermissionBehavior::Allow,
                        defaultSource
                    );
                    if (parsed) target.push_back(*parsed);
                } else if (item.is_object()) {
                    // Structured format
                    target.push_back(jsonToRule(item, defaultSource));
                }
            }
        };

        loadRules("policyRules", policyRules_, PermissionRuleSource::PolicySettings);
        loadRules("flagRules", flagRules_, PermissionRuleSource::FlagSettings);
        loadRules("userRules", userRules_, PermissionRuleSource::UserSettings);
        loadRules("projectRules", projectRules_, PermissionRuleSource::ProjectSettings);
        loadRules("localRules", localRules_, PermissionRuleSource::LocalSettings);
        loadRules("cliArgRules", cliArgRules_, PermissionRuleSource::CliArg);
        loadRules("commandRules", commandRules_, PermissionRuleSource::Command);
        loadRules("sessionRules", sessionRules_, PermissionRuleSource::Session);

        // Load decision history
        if (data.contains("decisionHistory")) {
            decisionHistory_ = data["decisionHistory"];
        }

        spdlog::debug("Loaded {} policy, {} user, {} project, {} local rules from: {}",
            policyRules_.size(), userRules_.size(),
            projectRules_.size(), localRules_.size(), path.string());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load permission settings: {}", e.what());
        return false;
    }
}

// ========== 决策记录 ==========

void PermissionSettings::recordDecision(
    const String& toolName,
    const String& action,
    PermissionBehavior behavior
) {
    std::lock_guard lock(mutex_);

    String key = toolName + ":" + action;

    if (!decisionHistory_.contains(key)) {
        decisionHistory_[key] = Json::object();
    }

    String behaviorStr = permissionBehaviorToString(behavior);
    if (!decisionHistory_[key].contains(behaviorStr)) {
        decisionHistory_[key][behaviorStr] = 0;
    }
    decisionHistory_[key][behaviorStr] = decisionHistory_[key][behaviorStr].get<int>() + 1;

    // Auto-learn: after 3 consistent allow decisions, add as rule
    int allowCount = decisionHistory_[key].value("allow", 0);
    int denyCount = decisionHistory_[key].value("deny", 0);
    int askCount = decisionHistory_[key].value("ask", 0);

    if (allowCount >= 3 && denyCount == 0 && askCount == 0) {
        PermissionRule rule;
        rule.toolName = toolName;
        rule.ruleContent = action;
        rule.behavior = PermissionBehavior::Allow;
        rule.source = PermissionRuleSource::UserSettings;

        bool exists = false;
        for (const auto& r : userRules_) {
            if (r.toolName == toolName && r.ruleContent == action) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            userRules_.push_back(rule);
            spdlog::debug("Auto-learned rule: {}({}) -> ALLOW", toolName, action);
        }
    } else if (denyCount >= 3 && allowCount == 0) {
        PermissionRule rule;
        rule.toolName = toolName;
        rule.ruleContent = action;
        rule.behavior = PermissionBehavior::Deny;
        rule.source = PermissionRuleSource::UserSettings;

        bool exists = false;
        for (const auto& r : userRules_) {
            if (r.toolName == toolName && r.ruleContent == action) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            userRules_.push_back(rule);
            spdlog::debug("Auto-learned rule: {}({}) -> DENY", toolName, action);
        }
    }
}

} // namespace claude
