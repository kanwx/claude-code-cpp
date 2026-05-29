#include <claude/core/UnifiedTaskStore.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>

namespace claude {

UnifiedTaskStore& UnifiedTaskStore::instance() {
    static UnifiedTaskStore store;
    return store;
}

String UnifiedTaskStore::createTask(const String& subject, const String& description) {
    std::lock_guard<std::mutex> lock(mutex_);
    String id = std::to_string(nextId_++);

    UnifiedTask task;
    task.id = id;
    task.subject = subject;
    task.description = description;
    task.status = UnifiedTask::Status::Pending;
    task.createdAt = std::chrono::steady_clock::now();

    tasks_[id] = std::move(task);
    spdlog::debug("UnifiedTaskStore: created task #{}: {}", id, subject);
    autoSave();
    return id;
}

std::optional<UnifiedTask> UnifiedTaskStore::getTask(const String& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return std::nullopt;
    return it->second;
}

std::vector<UnifiedTask> UnifiedTaskStore::listTasks(std::optional<UnifiedTask::Status> filter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UnifiedTask> result;
    result.reserve(tasks_.size());

    for (const auto& [_, task] : tasks_) {
        if (task.status == UnifiedTask::Status::Deleted) continue;
        if (filter && task.status != *filter) continue;
        result.push_back(task);
    }

    std::sort(result.begin(), result.end(),
        [](const UnifiedTask& a, const UnifiedTask& b) {
            return std::stoi(a.id) < std::stoi(b.id);
        });

    return result;
}

void UnifiedTaskStore::updateTask(const String& id, const UnifiedTask& updates) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return;

    auto& task = it->second;
    if (!updates.subject.empty()) task.subject = updates.subject;
    if (!updates.description.empty()) task.description = updates.description;
    if (!updates.activeForm.empty()) task.activeForm = updates.activeForm;
    if (!updates.owner.empty()) task.owner = updates.owner;
    if (updates.status != UnifiedTask::Status::Pending || updates.subject.empty()) {
        // Only update status if it changed from default
        if (updates.status != task.status) {
            task.status = updates.status;
            cv_.notify_all();
        }
    }

    spdlog::debug("UnifiedTaskStore: updated task #{}", id);
    autoSave();
}

bool UnifiedTaskStore::updateTaskStatus(const String& id, UnifiedTask::Status status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    it->second.status = status;
    cv_.notify_all();
    spdlog::debug("UnifiedTaskStore: task #{} status → {}", id, UnifiedTask::statusToString(status));
    autoSave();
    return true;
}

bool UnifiedTaskStore::claimTask(const String& id, const String& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    if (it->second.status != UnifiedTask::Status::Pending) return false;
    it->second.status = UnifiedTask::Status::InProgress;
    it->second.owner = owner;
    cv_.notify_all();
    spdlog::debug("UnifiedTaskStore: task #{} claimed by '{}'", id, owner);
    autoSave();
    return true;
}

bool UnifiedTaskStore::addBlockedBy(const String& id, const String& blockedById) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    auto blockerIt = tasks_.find(blockedById);
    if (it == tasks_.end() || blockerIt == tasks_.end()) return false;

    // Add blockedBy on this task
    auto& blockedBy = it->second.blockedBy;
    if (std::find(blockedBy.begin(), blockedBy.end(), blockedById) == blockedBy.end()) {
        blockedBy.push_back(blockedById);
    }

    // Add blocks on the blocker
    auto& blocks = blockerIt->second.blocks;
    if (std::find(blocks.begin(), blocks.end(), id) == blocks.end()) {
        blocks.push_back(id);
    }

    spdlog::debug("UnifiedTaskStore: task #{} blockedBy #{}", id, blockedById);
    autoSave();
    return true;
}

bool UnifiedTaskStore::addBlocks(const String& id, const String& blocksId) {
    return addBlockedBy(blocksId, id);
}

bool UnifiedTaskStore::areBlockersResolved(const String& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    for (const auto& blockerId : it->second.blockedBy) {
        auto blocker = tasks_.find(blockerId);
        if (blocker == tasks_.end()) continue;
        if (!blocker->second.isTerminal()) return false;
    }
    return true;
}

bool UnifiedTaskStore::setTaskAgentHandle(const String& id, std::shared_ptr<BackgroundAgentHandle> handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;
    it->second.agentHandle = std::move(handle);
    return true;
}

bool UnifiedTaskStore::setTaskResult(const String& id, const String& result, long totalTokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    it->second.status = UnifiedTask::Status::Completed;
    it->second.result = result;
    it->second.totalTokens = totalTokens;
    it->second.agentHandle.reset();
    cv_.notify_all();
    spdlog::debug("UnifiedTaskStore: task #{} completed ({} tokens)", id, totalTokens);
    autoSave();

    // Log which dependent tasks are now unblocked
    for (const auto& blockedId : it->second.blocks) {
        auto blocked = tasks_.find(blockedId);
        if (blocked != tasks_.end() && blocked->second.status == UnifiedTask::Status::Pending) {
            bool allResolved = true;
            for (const auto& blockerId : blocked->second.blockedBy) {
                auto blocker = tasks_.find(blockerId);
                if (blocker != tasks_.end() && !blocker->second.isTerminal()) {
                    allResolved = false;
                    break;
                }
            }
            if (allResolved) {
                spdlog::debug("UnifiedTaskStore: task #{} is now unblocked (all blockers resolved)", blockedId);
            }
        }
    }

    return true;
}

bool UnifiedTaskStore::setTaskError(const String& id, const String& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    it->second.status = UnifiedTask::Status::Failed;
    it->second.error = error;
    it->second.agentHandle.reset();
    cv_.notify_all();
    spdlog::debug("UnifiedTaskStore: task #{} failed: {}", id, error);
    autoSave();
    return true;
}

bool UnifiedTaskStore::cancelTask(const String& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    if (it->second.agentHandle) {
        it->second.agentHandle->cancel();
    }

    it->second.status = UnifiedTask::Status::Failed;
    it->second.error = "Cancelled by user";
    it->second.agentHandle.reset();
    cv_.notify_all();
    spdlog::debug("UnifiedTaskStore: task #{} cancelled", id);
    autoSave();
    return true;
}

bool UnifiedTaskStore::waitForTask(const String& id, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    if (it->second.isTerminal()) return true;

    if (timeoutMs < 0) {
        cv_.wait(lock, [&] {
            auto t = tasks_.find(id);
            return t == tasks_.end() || t->second.isTerminal();
        });
        return true;
    }

    return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        auto t = tasks_.find(id);
        return t == tasks_.end() || t->second.isTerminal();
    });
}

bool UnifiedTaskStore::deleteTask(const String& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;
    it->second.status = UnifiedTask::Status::Deleted;
    cv_.notify_all();
    return true;
}

void UnifiedTaskStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
    nextId_ = 1;
}

std::vector<String> UnifiedTaskStore::getNewlyUnblocked(const String& completedTaskId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<String> unblocked;

    auto it = tasks_.find(completedTaskId);
    if (it == tasks_.end()) return unblocked;

    for (const auto& blockedId : it->second.blocks) {
        auto blocked = tasks_.find(blockedId);
        if (blocked != tasks_.end() && blocked->second.status == UnifiedTask::Status::Pending) {
            bool allResolved = true;
            for (const auto& blockerId : blocked->second.blockedBy) {
                auto blocker = tasks_.find(blockerId);
                if (blocker != tasks_.end() && !blocker->second.isTerminal()) {
                    allResolved = false;
                    break;
                }
            }
            if (allResolved) {
                unblocked.push_back(blockedId);
            }
        }
    }

    return unblocked;
}

// ========== Persistence ==========

void UnifiedTaskStore::setPersistencePath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    persistencePath_ = path;
    spdlog::debug("UnifiedTaskStore: persistence path set to {}", path.string());
}

void UnifiedTaskStore::save() const {
    if (persistencePath_.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    try {
        // Create parent directory if needed
        auto parent = persistencePath_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        Json j = toJson();
        std::ofstream file(persistencePath_);
        if (file) {
            file << j.dump(2);
            spdlog::debug("UnifiedTaskStore: saved {} tasks to {}", tasks_.size(), persistencePath_.string());
        } else {
            spdlog::warn("UnifiedTaskStore: failed to open file for writing: {}", persistencePath_.string());
        }
    } catch (const std::exception& e) {
        spdlog::warn("UnifiedTaskStore: save failed: {}", e.what());
    }
}

void UnifiedTaskStore::load() {
    if (persistencePath_.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    try {
        if (!std::filesystem::exists(persistencePath_)) {
            spdlog::debug("UnifiedTaskStore: no persistence file at {}", persistencePath_.string());
            return;
        }

        std::ifstream file(persistencePath_);
        if (!file) return;

        Json j;
        file >> j;
        fromJson(j);
        spdlog::debug("UnifiedTaskStore: loaded {} tasks from {}", tasks_.size(), persistencePath_.string());
    } catch (const std::exception& e) {
        spdlog::warn("UnifiedTaskStore: load failed: {}", e.what());
    }
}

void UnifiedTaskStore::autoSave() const {
    if (persistencePath_.empty()) return;
    // Save is called under lock, but we need to unlock for file I/O
    // Simple approach: just write directly (fstream is safe under lock)
    try {
        auto parent = persistencePath_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        Json j = toJson();
        std::ofstream file(persistencePath_);
        if (file) {
            file << j.dump(2);
        }
    } catch (const std::exception& e) {
        spdlog::debug("UnifiedTaskStore: autoSave failed: {}", e.what());
    }
}

Json UnifiedTaskStore::toJson() const {
    Json j;
    j["nextId"] = nextId_;
    j["tasks"] = Json::object();

    for (const auto& [id, task] : tasks_) {
        Json tj;
        tj["id"] = task.id;
        tj["subject"] = task.subject;
        tj["description"] = task.description;
        tj["activeForm"] = task.activeForm;
        tj["status"] = UnifiedTask::statusToString(task.status);
        tj["owner"] = task.owner;
        tj["blockedBy"] = task.blockedBy;
        tj["blocks"] = task.blocks;
        tj["agentType"] = task.agentType;
        tj["prompt"] = task.prompt;
        tj["result"] = task.result;
        tj["error"] = task.error;
        tj["totalTokens"] = task.totalTokens;

        // Timestamps — convert steady_clock to a serializable form
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
            task.createdAt.time_since_epoch()).count();
        tj["createdAtSecs"] = secs;

        j["tasks"][id] = tj;
    }

    return j;
}

void UnifiedTaskStore::fromJson(const Json& j) {
    tasks_.clear();

    nextId_ = j.value("nextId", 1);

    if (!j.contains("tasks") || !j["tasks"].is_object()) return;

    for (auto& [id, tj] : j["tasks"].items()) {
        UnifiedTask task;
        task.id = tj.value("id", id);
        task.subject = tj.value("subject", "");
        task.description = tj.value("description", "");
        task.activeForm = tj.value("activeForm", "");
        task.owner = tj.value("owner", "");
        task.agentType = tj.value("agentType", "");
        task.prompt = tj.value("prompt", "");
        task.result = tj.value("result", "");
        task.error = tj.value("error", "");
        task.totalTokens = tj.value("totalTokens", 0L);

        // Status
        auto statusStr = tj.value("status", "pending");
        auto statusOpt = UnifiedTask::statusFromString(statusStr);
        task.status = statusOpt.value_or(UnifiedTask::Status::Pending);

        // Dependencies
        if (tj.contains("blockedBy") && tj["blockedBy"].is_array()) {
            task.blockedBy = tj["blockedBy"].get<std::vector<String>>();
        }
        if (tj.contains("blocks") && tj["blocks"].is_array()) {
            task.blocks = tj["blocks"].get<std::vector<String>>();
        }

        // Timestamp
        if (tj.contains("createdAtSecs")) {
            auto secs = tj.value("createdAtSecs", 0LL);
            task.createdAt = std::chrono::steady_clock::time_point(
                std::chrono::seconds(secs));
        }

        // Skip completed/failed/deleted tasks older than 24h
        if (task.isTerminal()) {
            continue;
        }

        tasks_[id] = std::move(task);
    }
}

} // namespace claude
