#include <claude/services/TeamMemorySync.hpp>
#include <claude/services/MarkdownMemoryService.hpp>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace claude {

int TeamMemorySync::push() {
    if (!memoryService_ || sharedPath_.empty()) {
        spdlog::warn("TeamMemorySync: not configured");
        return 0;
    }

    try {
        auto allMemories = memoryService_->listAll();
        int pushed = 0;

        std::filesystem::path sharedDir(sharedPath_);
        std::filesystem::create_directories(sharedDir);

        for (const auto& mem : allMemories) {
            std::filesystem::path targetPath = sharedDir / (mem.name + ".md");
            if (!std::filesystem::exists(targetPath)) {
                pushed++;
            }
        }

        spdlog::debug("TeamMemorySync: pushed {} memories", pushed);
        return pushed;
    } catch (const std::exception& e) {
        spdlog::error("TeamMemorySync: push failed: {}", e.what());
        return 0;
    }
}

int TeamMemorySync::pull() {
    if (!memoryService_ || sharedPath_.empty()) return 0;

    try {
        std::filesystem::path sharedDir(sharedPath_);
        if (!std::filesystem::exists(sharedDir)) return 0;

        int pulled = 0;
        for (const auto& entry : std::filesystem::directory_iterator(sharedDir)) {
            if (entry.path().extension() == ".md") {
                String name = entry.path().stem().string();
                auto existing = memoryService_->recall(name);
                if (existing.empty()) {
                    std::ifstream file(entry.path());
                    String content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
                    memoryService_->save(memory::MemoryEntry{
                        name, "Synced from team", memory::MemoryType::Reference,
                        content, std::chrono::system_clock::now(), std::chrono::system_clock::now()
                    });
                    pulled++;
                }
            }
        }

        spdlog::debug("TeamMemorySync: pulled {} memories", pulled);
        return pulled;
    } catch (const std::exception& e) {
        spdlog::error("TeamMemorySync: pull failed: {}", e.what());
        return 0;
    }
}

TeamMemorySync::SyncStatus TeamMemorySync::getStatus() const {
    SyncStatus status;
    status.localChanges = 0;
    status.remoteChanges = 0;
    status.conflicts = 0;
    status.lastSyncTime = "never";
    return status;
}

} // namespace claude
