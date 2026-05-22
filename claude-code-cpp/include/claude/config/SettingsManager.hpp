#pragma once
#include <claude/core/Types.hpp>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <mutex>

namespace claude {

/// Setting source priority (lower = higher priority, applied first)
enum class SettingSource {
    PolicySettings = 0,     // Admin-managed (managed-settings.json, MDM, remote)
    FlagSettings = 1,       // CLI --settings flags
    UserSettings = 2,       // ~/.claude/settings.json
    ProjectSettings = 3,    // .claude/settings.json (shared, VCS-tracked)
    LocalSettings = 4,      // .claude/settings.local.json (gitignored, per-developer)
};

/// Setting source file info
struct SettingSourceInfo {
    SettingSource source;
    String name;            // Display name: "user settings", "shared project settings", etc.
    std::filesystem::path filePath;
    bool isGitignored = false;
};

/// Settings manager with cascading 5-source merge
class SettingsManager {
public:
    SettingsManager();
    ~SettingsManager() = default;

    /// Non-copyable
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    /// Singleton access (auto-loads on first use)
    static SettingsManager& instance();

    /// Load all settings sources for a project
    void load(const std::filesystem::path& projectDir = {});

    /// Get effective merged settings
    Json getEffectiveSettings() const;

    /// Get settings from a specific source
    Json getSettingsForSource(SettingSource source) const;

    /// Update settings for a specific source
    void updateSettingsForSource(SettingSource source, const Json& updates);

    /// Save a specific source to disk
    void saveSource(SettingSource source);

    /// Save all dirty sources
    void saveAll();

    /// Get the file path for a source
    std::filesystem::path getFilePathForSource(SettingSource source) const;

    /// Get display name for a source
    static String sourceName(SettingSource source);

    /// Check if a source is allowed (policy may restrict editing)
    bool isSourceEditable(SettingSource source) const;

    /// Check if managed/policy settings restrict permission rule changes
    bool shouldAllowManagedPermissionRulesOnly() const;

    /// Get which source a specific key came from
    std::optional<SettingSource> getSourceForKey(const String& key) const;

    /// Register flag settings (from CLI --settings)
    void setFlagSettings(const Json& settings);

    /// Get all source infos
    std::vector<SettingSourceInfo> getSourceInfos() const;

    /// Validate settings JSON
    std::vector<String> validate(const Json& settings) const;

    /// Merge two JSON objects (overlay on top of base)
    static Json merge(const Json& base, const Json& overlay);

    /// Get config home directory
    static std::filesystem::path getConfigHome();

    /// Get project settings dir
    static std::filesystem::path getProjectSettingsDir(const std::filesystem::path& projectDir);

    // ── Convenience getters / setters for common settings ──────────
    // These use getEffectiveSettings() / updateSettingsForSource() so they
    // respect the full cascading layer model and persist to the Local layer.

    void setVimMode(bool enabled);
    bool getVimMode() const;

    void setDebugMode(bool enabled);
    bool getDebugMode() const;

    void setEffort(const String& effort);
    String getEffort() const;

    void setFastMode(bool enabled);
    bool getFastMode() const;

    void setBughunterMode(bool enabled);
    bool getBughunterMode() const;

    void setProactiveMode(bool enabled);
    bool getProactiveMode() const;

private:
    void loadSource(SettingSource source);
    void loadPolicySettings();
    void loadManagedSettingsDropins();
    void rebuildEffective();
    void markDirty(SettingSource source);

    std::filesystem::path projectDir_;
    std::filesystem::path configHome_;

    // Per-source data
    std::unordered_map<int, Json> sourceData_;         // int(SettingSource) -> Json
    std::unordered_map<int, std::filesystem::path> sourcePaths_;
    std::unordered_map<int, bool> sourceDirty_;

    // Effective merged settings (rebuilt on any change)
    Json effective_;

    // Policy tracking
    Json policyOverrides_;
    bool hasPolicySettings_ = false;

    // Cache
    std::unordered_map<String, SettingSource> keySourceMap_;
    mutable std::mutex mutex_;
};

} // namespace claude
