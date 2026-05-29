#include <claude/permission/PermissionStore.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace claude {

PermissionStore& PermissionStore::instance() {
    static PermissionStore store;
    return store;
}

String PermissionStore::makeKey(const String& toolName, const String& pattern) const {
    return toolName + ":" + pattern;
}

std::filesystem::path PermissionStore::defaultFilePath() const {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".claude" / "permissions.json";
    }
    return std::filesystem::path(".claude") / "permissions.json";
}

void PermissionStore::load(const std::filesystem::path& configDir) {
    std::lock_guard lock(mutex_);

    // Determine file path
    if (!configDir.empty()) {
        filePath_ = configDir / "permissions.json";
    } else {
        filePath_ = defaultFilePath();
    }

    decisions_.clear();
    loaded_ = true;

    // Read file
    if (!std::filesystem::exists(filePath_)) {
        spdlog::debug("PermissionStore: no persisted file at {}", filePath_.string());
        return;
    }

    try {
        std::ifstream ifs(filePath_);
        if (!ifs.is_open()) {
            spdlog::warn("PermissionStore: cannot open {}", filePath_.string());
            return;
        }

        Json root = Json::parse(ifs);
        if (!root.is_object() || !root.contains("decisions") || !root["decisions"].is_object()) {
            spdlog::debug("PermissionStore: invalid format in {}", filePath_.string());
            return;
        }

        const auto& decObj = root["decisions"];
        for (auto it = decObj.begin(); it != decObj.end(); ++it) {
            const String& key = it.key();
            const String& value = it.value().get_ref<const String&>();

            PermissionChoice choice;
            if (value == "AlwaysAllow") {
                choice = PermissionChoice::AlwaysAllow;
            } else if (value == "AlwaysDeny") {
                choice = PermissionChoice::AlwaysDeny;
            } else if (value == "AllowSession") {
                // Session-scoped choices should not be persisted; skip
                spdlog::debug("PermissionStore: skipping session-scoped choice '{}' for key '{}'", value, key);
                continue;
            } else {
                spdlog::debug("PermissionStore: skipping unknown choice '{}' for key '{}'", value, key);
                continue;
            }

            decisions_[key] = choice;
        }

        spdlog::debug("PermissionStore: loaded {} decisions from {}",
                     decisions_.size(), filePath_.string());

    } catch (const std::exception& e) {
        spdlog::warn("PermissionStore: failed to parse {}: {}", filePath_.string(), e.what());
    }
}

void PermissionStore::save() {
    std::lock_guard lock(mutex_);

    if (filePath_.empty()) {
        filePath_ = defaultFilePath();
    }

    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(filePath_.parent_path(), ec);
    if (ec) {
        spdlog::warn("PermissionStore: cannot create directory {}: {}",
                     filePath_.parent_path().string(), ec.message());
        return;
    }

    try {
        Json root;
        Json decObj = Json::object();

        for (const auto& [key, choice] : decisions_) {
            String value;
            switch (choice) {
                case PermissionChoice::AlwaysAllow: value = "AlwaysAllow"; break;
                case PermissionChoice::AlwaysDeny: value = "AlwaysDeny"; break;
                case PermissionChoice::AllowSession: continue; // Session-scoped, don't persist
                default: continue; // Skip AllowOnce/DenyOnce (shouldn't be in the map)
            }
            decObj[key] = value;
        }

        root["decisions"] = std::move(decObj);

        std::ofstream ofs(filePath_);
        if (!ofs.is_open()) {
            spdlog::warn("PermissionStore: cannot write to {}", filePath_.string());
            return;
        }

        ofs << root.dump(2);
        ofs.flush();

        spdlog::debug("PermissionStore: saved {} decisions to {}",
                      decisions_.size(), filePath_.string());

    } catch (const std::exception& e) {
        spdlog::warn("PermissionStore: failed to save {}: {}", filePath_.string(), e.what());
    }
}

void PermissionStore::recordDecision(const String& toolName, const String& pattern, PermissionChoice choice) {
    // Only persist "always" choices
    if (choice != PermissionChoice::AlwaysAllow && choice != PermissionChoice::AlwaysDeny) {
        return;
    }

    String key = makeKey(toolName, pattern);

    {
        std::lock_guard lock(mutex_);
        decisions_[key] = choice;
    }

    // Auto-save after each recorded decision
    save();

    spdlog::debug("PermissionStore: recorded {} for {}",
                 permissionChoiceToString(choice), key);
}

std::optional<PermissionChoice> PermissionStore::lookup(const String& toolName, const String& pattern) const {
    std::lock_guard lock(mutex_);

    // Lazy load on first lookup if not yet loaded
    // (const cast needed for lazy init; the load itself is thread-safe)
    if (!loaded_) {
        lock.~lock_guard();
        const_cast<PermissionStore*>(this)->load();
        // Re-acquire lock after load
        mutex_.lock();
        // Now check again under lock
    }

    String key = makeKey(toolName, pattern);
    auto it = decisions_.find(key);
    if (it != decisions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void PermissionStore::clear() {
    std::lock_guard lock(mutex_);
    decisions_.clear();
    loaded_ = true; // Don't re-load

    // Remove the file from disk
    if (!filePath_.empty() && std::filesystem::exists(filePath_)) {
        std::error_code ec;
        std::filesystem::remove(filePath_, ec);
        if (ec) {
            spdlog::warn("PermissionStore: failed to remove {}: {}", filePath_.string(), ec.message());
        }
    }

    spdlog::debug("PermissionStore: cleared all decisions");
}

std::map<String, PermissionChoice> PermissionStore::getAll() const {
    std::lock_guard lock(mutex_);
    return decisions_;
}

void PermissionStore::addDefaultRule(const String& toolName, const String& pathPattern, PermissionChoice choice) {
    String key = makeKey(toolName, pathPattern);
    std::lock_guard lock(mutex_);
    // Don't overwrite user decisions
    if (decisions_.find(key) == decisions_.end()) {
        decisions_[key] = choice;
    }
}

void PermissionStore::loadDefaultRules() {
    // System-critical paths: auto-deny writes
    addDefaultRule("Write", "/etc/", PermissionChoice::AlwaysDeny);
    addDefaultRule("Write", "/usr/", PermissionChoice::AlwaysDeny);
    addDefaultRule("Write", "/System/", PermissionChoice::AlwaysDeny);
    addDefaultRule("Write", "~/Library/", PermissionChoice::AlwaysDeny);

    addDefaultRule("Edit", "/etc/", PermissionChoice::AlwaysDeny);
    addDefaultRule("Edit", "/usr/", PermissionChoice::AlwaysDeny);
    addDefaultRule("Edit", "/System/", PermissionChoice::AlwaysDeny);
    addDefaultRule("Edit", "~/Library/", PermissionChoice::AlwaysDeny);

    addDefaultRule("Bash", "rm -rf /", PermissionChoice::AlwaysDeny);
    addDefaultRule("Bash", "sudo rm -rf", PermissionChoice::AlwaysDeny);
    addDefaultRule("Bash", "mkfs.", PermissionChoice::AlwaysDeny);
    addDefaultRule("Bash", "dd if=", PermissionChoice::AlwaysDeny);

    spdlog::debug("PermissionStore: loaded default path rules");
}

} // namespace claude
