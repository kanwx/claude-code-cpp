#include <claude/core/MigrationManager.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace claude {

MigrationManager::MigrationManager(const String& configPath)
    : configPath_(configPath) {}

void MigrationManager::addMigration(int version, const String& name, MigrationFn fn) {
    migrations_.push_back({version, name, std::move(fn)});
    std::sort(migrations_.begin(), migrations_.end(),
        [](const MigrationStep& a, const MigrationStep& b) { return a.version < b.version; });
}

int MigrationManager::getCurrentVersion() const {
    return loadVersion();
}

void MigrationManager::setVersion(int version) {
    saveVersion(version);
}

MigrationResult MigrationManager::runMigrations() {
    int currentVersion = loadVersion();

    if (currentVersion >= CURRENT_MIGRATION_VERSION) {
        spdlog::debug("MigrationManager: already at version {}, no migrations needed", currentVersion);
        return {true, "", currentVersion, currentVersion};
    }

    spdlog::debug("MigrationManager: running migrations from version {} to {}",
        currentVersion, CURRENT_MIGRATION_VERSION);

    int appliedVersion = currentVersion;

    for (auto& step : migrations_) {
        if (step.version <= appliedVersion) continue;

        spdlog::debug("MigrationManager: applying migration v{} '{}'", step.version, step.name);
        auto result = step.fn();
        if (!result) {
            spdlog::error("MigrationManager: migration v{} '{}' failed: {}",
                step.version, step.name, result.error());
            return {false, result.error(), currentVersion, appliedVersion};
        }

        appliedVersion = step.version;
        saveVersion(appliedVersion);
        spdlog::debug("MigrationManager: migration v{} applied successfully", step.version);
    }

    if (appliedVersion < CURRENT_MIGRATION_VERSION) {
        appliedVersion = CURRENT_MIGRATION_VERSION;
        saveVersion(appliedVersion);
    }

    return {true, "", currentVersion, appliedVersion};
}

int MigrationManager::loadVersion() const {
    try {
        std::ifstream ifs(configPath_);
        if (!ifs) return 0;
        Json config = Json::parse(ifs);
        return config.value("migrationVersion", 0);
    } catch (...) {
        return 0;
    }
}

void MigrationManager::saveVersion(int version) {
    Json config;
    try {
        std::ifstream ifs(configPath_);
        if (ifs) {
            config = Json::parse(ifs);
        }
    } catch (...) {
        config = Json::object();
    }

    config["migrationVersion"] = version;

    auto parentDir = fs::path(configPath_).parent_path();
    if (!parentDir.empty()) {
        fs::create_directories(parentDir);
    }

    std::ofstream ofs(configPath_);
    if (ofs) {
        ofs << config.dump(2);
    }
}

} // namespace claude
