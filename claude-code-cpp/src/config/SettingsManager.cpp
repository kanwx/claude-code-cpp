#include <claude/config/SettingsManager.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace claude {

// ── Singleton ──────────────────────────────────────────────────────

SettingsManager& SettingsManager::instance() {
    static SettingsManager inst;
    // Auto-load on first access if not yet loaded
    static bool loaded = false;
    if (!loaded) {
        inst.load();
        loaded = true;
    }
    return inst;
}

// ── Helpers: keep env vars in sync for child-process inheritance ───

namespace {
    /// Push a boolean setting to the environment so child processes see it.
    void syncEnvBool(const char* envVar, bool value) {
        if (value) {
            setenv(envVar, "true", 1);
        } else {
            unsetenv(envVar);
        }
    }

    /// Push a string setting to the environment so child processes see it.
    void syncEnvString(const char* envVar, const String& value) {
        if (!value.empty()) {
            setenv(envVar, value.c_str(), 1);
        } else {
            unsetenv(envVar);
        }
    }
} // anonymous

SettingsManager::SettingsManager() {
    configHome_ = getConfigHome();
    fs::create_directories(configHome_);
}

std::filesystem::path SettingsManager::getConfigHome() {
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / ".claude";
    return fs::path(".claude");
}

std::filesystem::path SettingsManager::getProjectSettingsDir(const fs::path& projectDir) {
    return projectDir / ".claude";
}

String SettingsManager::sourceName(SettingSource source) {
    switch (source) {
        case SettingSource::PolicySettings: return "managed policy";
        case SettingSource::FlagSettings: return "CLI flags";
        case SettingSource::UserSettings: return "user settings";
        case SettingSource::ProjectSettings: return "shared project settings";
        case SettingSource::LocalSettings: return "local project settings";
    }
    return "unknown";
}

void SettingsManager::load(const fs::path& projectDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    projectDir_ = projectDir;

    // Set up paths for each source
    sourcePaths_[static_cast<int>(SettingSource::PolicySettings)] =
        configHome_ / "managed-settings.json";
    sourcePaths_[static_cast<int>(SettingSource::UserSettings)] =
        configHome_ / "settings.json";
    if (!projectDir.empty()) {
        auto projDir = getProjectSettingsDir(projectDir);
        sourcePaths_[static_cast<int>(SettingSource::ProjectSettings)] =
            projDir / "settings.json";
        sourcePaths_[static_cast<int>(SettingSource::LocalSettings)] =
            projDir / "settings.local.json";
    }

    // Load in priority order (policy first, then flags, user, project, local)
    loadSource(SettingSource::PolicySettings);
    loadPolicySettings();  // Extra: MDM, drop-in dir
    loadSource(SettingSource::UserSettings);
    loadSource(SettingSource::ProjectSettings);
    loadSource(SettingSource::LocalSettings);

    rebuildEffective();
}

void SettingsManager::loadSource(SettingSource source) {
    auto key = static_cast<int>(source);
    auto it = sourcePaths_.find(key);
    if (it == sourcePaths_.end() || it->second.empty()) {
        sourceData_[key] = Json::object();
        return;
    }

    const auto& path = it->second;
    if (!fs::exists(path)) {
        sourceData_[key] = Json::object();
        return;
    }

    try {
        std::ifstream ifs(path);
        if (ifs) {
            sourceData_[key] = Json::parse(ifs);
            auto errors = validate(sourceData_[key]);
            if (!errors.empty()) {
                spdlog::warn("SettingsManager: validation errors in {}: {}",
                    path.string(), errors[0]);
            }
        } else {
            sourceData_[key] = Json::object();
        }
    } catch (const Json::parse_error& e) {
        spdlog::warn("SettingsManager: failed to parse {}: {}", path.string(), e.what());
        sourceData_[key] = Json::object();
    }
}

void SettingsManager::loadPolicySettings() {
    // Load managed-settings.json
    auto policyPath = sourcePaths_[static_cast<int>(SettingSource::PolicySettings)];
    if (fs::exists(policyPath)) {
        hasPolicySettings_ = true;
    }

    // Load drop-in directory: managed-settings.d/*.json (sorted alphabetically, merged on top)
    auto dropinDir = configHome_ / "managed-settings.d";
    if (fs::exists(dropinDir) && fs::is_directory(dropinDir)) {
        std::vector<fs::path> dropinFiles;
        for (const auto& entry : fs::directory_iterator(dropinDir)) {
            if (entry.path().extension() == ".json") {
                dropinFiles.push_back(entry.path());
            }
        }
        std::sort(dropinFiles.begin(), dropinFiles.end());

        auto key = static_cast<int>(SettingSource::PolicySettings);
        for (const auto& file : dropinFiles) {
            try {
                std::ifstream ifs(file);
                if (ifs) {
                    Json dropin = Json::parse(ifs);
                    sourceData_[key] = merge(sourceData_[key], dropin);
                    hasPolicySettings_ = true;
                }
            } catch (const Json::parse_error& e) {
                spdlog::warn("SettingsManager: failed to parse drop-in {}: {}", file.string(), e.what());
            }
        }
    }

    // Check for system-wide managed settings (macOS: /etc/claude-code/CLAUDE.md equivalent)
    auto systemPolicy = fs::path("/etc/claude-code/managed-settings.json");
    if (fs::exists(systemPolicy)) {
        try {
            std::ifstream ifs(systemPolicy);
            if (ifs) {
                Json sysPolicy = Json::parse(ifs);
                auto key = static_cast<int>(SettingSource::PolicySettings);
                sourceData_[key] = merge(sourceData_[key], sysPolicy);
                hasPolicySettings_ = true;
            }
        } catch (const Json::parse_error& e) {
            spdlog::warn("SettingsManager: failed to parse system policy: {}", e.what());
        }
    }

    if (hasPolicySettings_) {
        policyOverrides_ = sourceData_[static_cast<int>(SettingSource::PolicySettings)];
    }
}

void SettingsManager::loadManagedSettingsDropins() {
    // Already handled in loadPolicySettings()
}

void SettingsManager::rebuildEffective() {
    effective_ = Json::object();
    keySourceMap_.clear();

    // Merge in priority order: policy -> flags -> user -> project -> local
    // Later sources override earlier ones
    int order[] = {
        static_cast<int>(SettingSource::PolicySettings),
        static_cast<int>(SettingSource::FlagSettings),
        static_cast<int>(SettingSource::UserSettings),
        static_cast<int>(SettingSource::ProjectSettings),
        static_cast<int>(SettingSource::LocalSettings),
    };

    for (int src : order) {
        auto it = sourceData_.find(src);
        if (it != sourceData_.end() && it->second.is_object()) {
            effective_ = merge(effective_, it->second);
            // Track which source each key came from
            for (auto& [key, val] : it->second.items()) {
                keySourceMap_[key] = static_cast<SettingSource>(src);
            }
        }
    }
}

Json SettingsManager::getEffectiveSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return effective_;
}

Json SettingsManager::getSettingsForSource(SettingSource source) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sourceData_.find(static_cast<int>(source));
    return it != sourceData_.end() ? it->second : Json::object();
}

void SettingsManager::updateSettingsForSource(SettingSource source, const Json& updates) {
    if (!isSourceEditable(source)) {
        spdlog::warn("SettingsManager: source {} is not editable (policy restriction)", sourceName(source));
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto key = static_cast<int>(source);
    auto& data = sourceData_[key];
    if (!data.is_object()) data = Json::object();

    // Merge updates, setting a key to null deletes it
    for (auto& [k, v] : updates.items()) {
        if (v.is_null()) {
            data.erase(k);
        } else {
            data[k] = v;
        }
    }

    markDirty(source);
    rebuildEffective();
}

void SettingsManager::saveSource(SettingSource source) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = static_cast<int>(source);

    if (!sourceDirty_.count(key) || !sourceDirty_[key]) return;

    auto pathIt = sourcePaths_.find(key);
    if (pathIt == sourcePaths_.end() || pathIt->second.empty()) return;

    const auto& path = pathIt->second;
    auto parentDir = path.parent_path();
    if (!parentDir.empty()) {
        fs::create_directories(parentDir);
    }

    auto dataIt = sourceData_.find(key);
    if (dataIt == sourceData_.end()) return;

    std::ofstream ofs(path);
    if (ofs) {
        ofs << dataIt->second.dump(2);
        sourceDirty_[key] = false;
        spdlog::debug("SettingsManager: saved {}", path.string());
    } else {
        spdlog::warn("SettingsManager: failed to save {}", path.string());
    }
}

void SettingsManager::saveAll() {
    for (int i = 0; i <= static_cast<int>(SettingSource::LocalSettings); ++i) {
        saveSource(static_cast<SettingSource>(i));
    }
}

std::filesystem::path SettingsManager::getFilePathForSource(SettingSource source) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sourcePaths_.find(static_cast<int>(source));
    return it != sourcePaths_.end() ? it->second : fs::path();
}

bool SettingsManager::isSourceEditable(SettingSource source) const {
    // Policy settings are never user-editable
    if (source == SettingSource::PolicySettings) return false;
    // If policy has restrictEditing, other sources may be locked too
    if (hasPolicySettings_) {
        auto policy = sourceData_.find(static_cast<int>(SettingSource::PolicySettings));
        if (policy != sourceData_.end()) {
            if (policy->second.value("restrictEditing", false)) {
                return source == SettingSource::FlagSettings;
            }
        }
    }
    return true;
}

bool SettingsManager::shouldAllowManagedPermissionRulesOnly() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto policy = sourceData_.find(static_cast<int>(SettingSource::PolicySettings));
    if (policy == sourceData_.end()) return false;
    return policy->second.value("allowManagedPermissionRulesOnly", false);
}

std::optional<SettingSource> SettingsManager::getSourceForKey(const String& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keySourceMap_.find(key);
    if (it != keySourceMap_.end()) return it->second;
    return std::nullopt;
}

void SettingsManager::setFlagSettings(const Json& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    sourceData_[static_cast<int>(SettingSource::FlagSettings)] = settings;
    rebuildEffective();
}

std::vector<SettingSourceInfo> SettingsManager::getSourceInfos() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SettingSourceInfo> infos;
    infos.push_back({SettingSource::UserSettings, sourceName(SettingSource::UserSettings),
        sourcePaths_.count(static_cast<int>(SettingSource::UserSettings))
            ? sourcePaths_.at(static_cast<int>(SettingSource::UserSettings)) : fs::path(),
        false});
    infos.push_back({SettingSource::ProjectSettings, sourceName(SettingSource::ProjectSettings),
        sourcePaths_.count(static_cast<int>(SettingSource::ProjectSettings))
            ? sourcePaths_.at(static_cast<int>(SettingSource::ProjectSettings)) : fs::path(),
        false});
    infos.push_back({SettingSource::LocalSettings, sourceName(SettingSource::LocalSettings),
        sourcePaths_.count(static_cast<int>(SettingSource::LocalSettings))
            ? sourcePaths_.at(static_cast<int>(SettingSource::LocalSettings)) : fs::path(),
        true});  // gitignored
    if (hasPolicySettings_) {
        infos.insert(infos.begin(), {SettingSource::PolicySettings, sourceName(SettingSource::PolicySettings),
            sourcePaths_.count(static_cast<int>(SettingSource::PolicySettings))
                ? sourcePaths_.at(static_cast<int>(SettingSource::PolicySettings)) : fs::path(),
            false});
    }
    return infos;
}

std::vector<String> SettingsManager::validate(const Json& settings) const {
    std::vector<String> errors;
    // Validate permission rules format
    if (settings.contains("permissions")) {
        const auto& perms = settings["permissions"];
        if (perms.contains("allow") && perms["allow"].is_array()) {
            for (size_t i = 0; i < perms["allow"].size(); ++i) {
                if (!perms["allow"][i].is_string()) {
                    errors.push_back("permissions.allow[" + std::to_string(i) + "] must be a string");
                }
            }
        }
        if (perms.contains("deny") && perms["deny"].is_array()) {
            for (size_t i = 0; i < perms["deny"].size(); ++i) {
                if (!perms["deny"][i].is_string()) {
                    errors.push_back("permissions.deny[" + std::to_string(i) + "] must be a string");
                }
            }
        }
    }
    return errors;
}

void SettingsManager::markDirty(SettingSource source) {
    sourceDirty_[static_cast<int>(source)] = true;
}

Json SettingsManager::merge(const Json& base, const Json& overlay) {
    Json result = base;
    if (!overlay.is_object()) return result;
    for (auto& [key, value] : overlay.items()) {
        if (result.contains(key) && result[key].is_object() && value.is_object()) {
            result[key] = merge(result[key], value);
        } else {
            result[key] = value;
        }
    }
    return result;
}

// ── Convenience getters / setters ──────────────────────────────────
// Each setter:
//   1. Writes the value to the LocalSettings layer (per-developer, gitignored)
//   2. Persists that layer to disk
//   3. Syncs the corresponding env var so child processes inherit the setting
//
// Each getter reads from the effective (merged) settings, falling back to
// the env var if the setting was never stored in JSON.

void SettingsManager::setVimMode(bool enabled) {
    updateSettingsForSource(SettingSource::LocalSettings,
        Json::object({{"vim_mode", enabled}}));
    saveSource(SettingSource::LocalSettings);
    syncEnvBool("CLAUDE_VIM_MODE", enabled);
}

bool SettingsManager::getVimMode() const {
    auto eff = getEffectiveSettings();
    if (eff.contains("vim_mode")) {
        return eff["vim_mode"].get<bool>();
    }
    const char* env = std::getenv("CLAUDE_VIM_MODE");
    return env && String(env) == "true";
}

void SettingsManager::setDebugMode(bool enabled) {
    updateSettingsForSource(SettingSource::LocalSettings,
        Json::object({{"debug_mode", enabled}}));
    saveSource(SettingSource::LocalSettings);
    syncEnvBool("CLAUDE_DEBUG", enabled);
}

bool SettingsManager::getDebugMode() const {
    auto eff = getEffectiveSettings();
    if (eff.contains("debug_mode")) {
        return eff["debug_mode"].get<bool>();
    }
    const char* env = std::getenv("CLAUDE_DEBUG");
    return env && String(env) == "true";
}

void SettingsManager::setEffort(const String& effort) {
    updateSettingsForSource(SettingSource::LocalSettings,
        Json::object({{"effort", effort}}));
    saveSource(SettingSource::LocalSettings);
    syncEnvString("CLAUDE_EFFORT", effort);
}

String SettingsManager::getEffort() const {
    auto eff = getEffectiveSettings();
    if (eff.contains("effort")) {
        return eff["effort"].get<String>();
    }
    const char* env = std::getenv("CLAUDE_EFFORT");
    return env ? String(env) : "medium";
}

void SettingsManager::setFastMode(bool enabled) {
    updateSettingsForSource(SettingSource::LocalSettings,
        Json::object({{"fast_mode", enabled}}));
    saveSource(SettingSource::LocalSettings);
    syncEnvBool("CLAUDE_FAST_MODE", enabled);
}

bool SettingsManager::getFastMode() const {
    auto eff = getEffectiveSettings();
    if (eff.contains("fast_mode")) {
        return eff["fast_mode"].get<bool>();
    }
    const char* env = std::getenv("CLAUDE_FAST_MODE");
    return env && String(env) == "true";
}

void SettingsManager::setBughunterMode(bool enabled) {
    updateSettingsForSource(SettingSource::LocalSettings,
        Json::object({{"bughunter_mode", enabled}}));
    saveSource(SettingSource::LocalSettings);
    syncEnvBool("CLAUDE_BUGHUNTER_MODE", enabled);
}

bool SettingsManager::getBughunterMode() const {
    auto eff = getEffectiveSettings();
    if (eff.contains("bughunter_mode")) {
        return eff["bughunter_mode"].get<bool>();
    }
    const char* env = std::getenv("CLAUDE_BUGHUNTER_MODE");
    return env && String(env) == "true";
}

void SettingsManager::setProactiveMode(bool enabled) {
    updateSettingsForSource(SettingSource::LocalSettings,
        Json::object({{"proactive_mode", enabled}}));
    saveSource(SettingSource::LocalSettings);
    syncEnvBool("CLAUDE_CODE_PROACTIVE_MODE", enabled);
}

bool SettingsManager::getProactiveMode() const {
    auto eff = getEffectiveSettings();
    if (eff.contains("proactive_mode")) {
        return eff["proactive_mode"].get<bool>();
    }
    const char* env = std::getenv("CLAUDE_CODE_PROACTIVE_MODE");
    if (env) {
        String val(env);
        std::transform(val.begin(), val.end(), val.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return val == "1" || val == "true" || val == "yes";
    }
    return false;
}

} // namespace claude
