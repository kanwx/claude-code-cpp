#include <claude/services/SettingsSync.hpp>
#include <claude/config/AppConfig.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace claude {

Json SettingsSync::exportSettings(const AppConfig& config) {
    Json settings;
    settings["version"] = 1;
    settings["exportedAt"] = std::chrono::system_clock::now().time_since_epoch().count();
    settings["config"] = config.raw();
    return settings;
}

bool SettingsSync::importSettings(AppConfig& config, const Json& settings) {
    if (!settings.contains("config")) {
        spdlog::warn("SettingsSync: no config section in imported settings");
        return false;
    }

    try {
        auto merged = AppConfig::merge(config.raw(), settings["config"]);
        config.raw() = merged;
        config.save();
        spdlog::debug("SettingsSync: imported settings successfully");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("SettingsSync: failed to import: {}", e.what());
        return false;
    }
}

bool SettingsSync::saveToFile(const Json& settings, const std::filesystem::path& path) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << settings.dump(2);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("SettingsSync: save failed: {}", e.what());
        return false;
    }
}

std::optional<Json> SettingsSync::loadFromFile(const std::filesystem::path& path) {
    try {
        std::ifstream file(path);
        if (!file) return std::nullopt;
        return Json::parse(file);
    } catch (const std::exception& e) {
        spdlog::error("SettingsSync: load failed: {}", e.what());
        return std::nullopt;
    }
}

SettingsSync::ConflictInfo SettingsSync::detectConflict(
    const std::filesystem::path& localPath,
    const std::filesystem::path& remotePath
) {
    ConflictInfo info;
    info.hasConflict = false;

    try {
        if (!std::filesystem::exists(localPath) || !std::filesystem::exists(remotePath)) {
            return info;
        }

        auto localTime = std::filesystem::last_write_time(localPath);
        auto remoteTime = std::filesystem::last_write_time(remotePath);

        info.localModified = std::chrono::system_clock::now();  // Approximate
        info.remoteModified = std::chrono::system_clock::now();

        // Check if files differ
        std::ifstream localFile(localPath);
        std::ifstream remoteFile(remotePath);
        String localContent((std::istreambuf_iterator<char>(localFile)),
                            std::istreambuf_iterator<char>());
        String remoteContent((std::istreambuf_iterator<char>(remoteFile)),
                             std::istreambuf_iterator<char>());

        if (localContent != remoteContent) {
            info.hasConflict = true;
        }
    } catch (...) {}

    return info;
}

Json SettingsSync::mergeSettings(const Json& local, const Json& remote, bool remoteWins) {
    return remoteWins ? AppConfig::merge(local, remote) : AppConfig::merge(remote, local);
}

} // namespace claude
