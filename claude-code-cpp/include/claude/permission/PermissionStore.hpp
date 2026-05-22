#pragma once

#include "PermissionTypes.hpp"
#include "../core/Types.hpp"
#include <map>
#include <mutex>
#include <filesystem>
#include <optional>

namespace claude {

/// Persistent storage for permission decisions.
/// Saves "always allow" and "always deny" decisions to a JSON file
/// so they survive across sessions.
///
/// File location: ~/.claude/permissions.json
/// Format: { "decisions": { "ToolName:pattern": "AlwaysAllow"|"AlwaysDeny", ... } }
class PermissionStore {
public:
    /// Singleton accessor
    static PermissionStore& instance();

    /// Load decisions from the persistent file.
    /// If configDir is empty, defaults to ~/.claude/
    void load(const std::filesystem::path& configDir = "");

    /// Save current decisions to the persistent file
    void save();

    /// Record a permission decision.
    /// Only AlwaysAllow and AlwaysDeny are persisted; AllowOnce/DenyOnce are ignored.
    void recordDecision(const String& toolName, const String& pattern, PermissionChoice choice);

    /// Look up a previous decision for a tool+pattern.
    /// Returns nullopt if no persisted decision exists.
    std::optional<PermissionChoice> lookup(const String& toolName, const String& pattern) const;

    /// Clear all saved decisions (in memory and on disk)
    void clear();

    /// Get all saved decisions (for debugging / settings display)
    std::map<String, PermissionChoice> getAll() const;

private:
    PermissionStore() = default;

    /// Build the composite key from toolName + pattern
    String makeKey(const String& toolName, const String& pattern) const;

    /// Determine the default file path (~/.claude/permissions.json)
    std::filesystem::path defaultFilePath() const;

    mutable std::mutex mutex_;
    std::map<String, PermissionChoice> decisions_;
    std::filesystem::path filePath_;
    bool loaded_ = false;
};

} // namespace claude
