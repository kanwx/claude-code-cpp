#pragma once

#include "../core/Types.hpp"
#include <filesystem>
#include <chrono>
#include <vector>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace claude::utils {

inline void backupFile(const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(filePath)) return;

    auto historyDir = filePath.parent_path() / ".claude" / "file_history";
    std::filesystem::create_directories(historyDir);

    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    auto backupName = filePath.filename().string() + "." + std::to_string(timestamp);
    auto backupPath = historyDir / backupName;

    std::filesystem::copy_file(filePath, backupPath,
        std::filesystem::copy_options::overwrite_existing);

    // Keep only last 10 backups per file
    std::vector<std::filesystem::path> backups;
    String prefix = filePath.filename().string() + ".";
    for (const auto& entry : std::filesystem::directory_iterator(historyDir)) {
        if (entry.is_regular_file()) {
            String name = entry.path().filename().string();
            if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                backups.push_back(entry.path());
            }
        }
    }
    if (backups.size() > 10) {
        std::sort(backups.begin(), backups.end());
        for (size_t i = 0; i < backups.size() - 10; ++i) {
            std::filesystem::remove(backups[i]);
        }
    }

    spdlog::debug("FileHistory: backed up {} to {}", filePath.string(), backupPath.string());
}

} // namespace claude::utils
