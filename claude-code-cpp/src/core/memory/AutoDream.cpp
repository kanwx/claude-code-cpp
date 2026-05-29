#include <claude/core/memory/AutoDream.hpp>
#include <claude/services/MarkdownMemoryService.hpp>
#include <claude/api/ApiClient.hpp>

#include <algorithm>
#include <unordered_map>

namespace claude {

bool AutoDream::shouldDream(std::chrono::steady_clock::time_point lastActivityTime) const {
    if (!enabled_ || !memoryService_) return false;

    auto now = std::chrono::steady_clock::now();
    auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivityTime);

    if (idle < idleThreshold_) return false;

    auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(now - lastDreamTime_);
    if (sinceLast < std::chrono::minutes(2)) return false;

    return true;
}

int AutoDream::dream(const std::vector<Message>& recentMessages) {
    std::lock_guard lock(mutex_);

    if (!enabled_ || !memoryService_) return 0;

    int consolidated = 0;

    consolidated += mergeSimilarMemories();
    consolidated += clearExpiredProjectMemories();

    if (!recentMessages.empty()) {
        consolidated += extractImplicitPreferences(recentMessages);
    }

    totalDreams_++;
    totalConsolidated_ += consolidated;
    lastDreamTime_ = std::chrono::steady_clock::now();

    if (consolidated > 0) {
        spdlog::debug("Auto-dream: consolidated {} memory items (total: {})",
            consolidated, totalConsolidated_);
    }

    return consolidated;
}

int AutoDream::mergeSimilarMemories() {
    auto allMemories = memoryService_->listAll();

    int merged = 0;
    std::unordered_map<String, int> nameCounts;
    for (const auto& mem : allMemories) {
        nameCounts[mem.name]++;
    }

    for (const auto& [name, count] : nameCounts) {
        if (count > 1) {
            spdlog::debug("Auto-dream: found duplicate memory '{}', will merge", name);
            merged++;
        }
    }

    return merged;
}

int AutoDream::clearExpiredProjectMemories() {
    return memoryService_->clearExpired();
}

int AutoDream::extractImplicitPreferences(const std::vector<Message>& recentMessages) {
    int extracted = 0;

    for (const auto& msg : recentMessages) {
        if (msg.role != MessageRole::User) continue;

        const String& content = msg.content;
        if (content.empty()) continue;

        if (content.find("不要") != String::npos || content.find("don't") != String::npos ||
            content.find("不要用") != String::npos || content.find("never use") != String::npos) {
            spdlog::debug("Auto-dream: detected implicit preference in user message");
        }
    }

    return extracted;
}

// ========== LLM-assisted consolidation ==========

int AutoDream::mergeWithLLM(ApiClient& apiClient) {
    std::lock_guard lock(mutex_);
    if (!enabled_ || !memoryService_) return 0;

    auto allIndex = memoryService_->listAll();
    if (allIndex.size() < 2) return 0;

    // Build a prompt with all memory names/descriptions for LLM similarity analysis
    String memoryList;
    for (size_t i = 0; i < allIndex.size(); ++i) {
        memoryList += std::to_string(i) + ". [" + allIndex[i].name + "] " +
                      allIndex[i].description + "\n";
    }

    String systemPrompt =
        "You are a memory consolidation assistant. Review the following memory entries and identify "
        "groups of semantically similar or overlapping memories that should be merged.\n\n"
        "For each group, output a JSON object with:\n"
        "- \"indices\": array of memory indices to merge\n"
        "- \"merged_name\": short name for the merged memory\n"
        "- \"merged_description\": combined description\n\n"
        "Return a JSON array of merge operations. If no memories should be merged, return [].\n"
        "Only merge memories that are clearly about the same topic or preference.";

    Json messages = Json::array();
    messages.push_back({{"role", "user"}, {"content", memoryList}});

    auto response = apiClient.call(messages, Json::array());
    if (!response) {
        spdlog::warn("Auto-dream LLM merge: API call failed: {}", response.error());
        return 0;
    }

    // Parse LLM response
    String content;
    if (response->contains("content") && (*response)["content"].is_array()) {
        for (const auto& block : (*response)["content"]) {
            if (block.value("type", "") == "text") {
                content += block.value("text", "");
            }
        }
    } else if (response->contains("choices") && !(*response)["choices"].empty()) {
        content = (*response)["choices"][0]["message"]["content"].get<String>();
    }

    if (content.empty()) return 0;

    // Extract JSON from response (may be wrapped in ```json ... ```)
    auto jsonStart = content.find('[');
    auto jsonEnd = content.rfind(']');
    if (jsonStart == String::npos || jsonEnd == String::npos) return 0;

    try {
        auto merges = Json::parse(content.substr(jsonStart, jsonEnd - jsonStart + 1));
        if (!merges.is_array()) return 0;

        int merged = 0;
        for (const auto& mergeOp : merges) {
            auto indices = mergeOp.value("indices", Json::array());
            if (indices.size() < 2) continue;

            String mergedName = mergeOp.value("merged_name", "");
            String mergedDesc = mergeOp.value("merged_description", "");
            if (mergedName.empty()) continue;

            // Remove old memories and save merged one
            for (const auto& idx : indices) {
                if (idx.is_number() && idx.get<int>() < static_cast<int>(allIndex.size())) {
                    memoryService_->remove(allIndex[idx.get<int>()].name);
                }
            }

            memory::MemoryEntry entry;
            entry.name = mergedName;
            entry.description = mergedDesc;
            entry.type = memory::MemoryType::User;
            entry.content = mergedDesc;
            memoryService_->save(entry);
            merged++;
        }

        return merged;
    } catch (const Json::parse_error& e) {
        spdlog::debug("Auto-dream LLM merge: failed to parse response: {}", e.what());
        return 0;
    }
}

int AutoDream::extractPreferencesWithLLM(
    const std::vector<Message>& recentMessages,
    ApiClient& apiClient
) {
    std::lock_guard lock(mutex_);
    if (!enabled_ || !memoryService_ || recentMessages.empty()) return 0;

    // Build conversation context from recent user messages
    String conversation;
    for (const auto& msg : recentMessages) {
        if (msg.role == MessageRole::User && !msg.content.empty()) {
            conversation += "User: " + msg.content + "\n\n";
        }
    }
    if (conversation.empty()) return 0;

    String systemPrompt =
        "You are a memory extraction assistant. Review the following conversation and extract:\n"
        "1. User preferences (formatting, language, tool choices, coding style)\n"
        "2. Project facts (architecture decisions, naming conventions, tech stack choices)\n"
        "3. Corrections (things the user said were wrong or to avoid)\n\n"
        "For each extracted item, output a JSON object:\n"
        "- \"type\": one of \"preference\", \"project_fact\", \"correction\"\n"
        "- \"name\": short identifier (snake_case)\n"
        "- \"description\": one-line summary\n"
        "- \"content\": full detail\n\n"
        "Return a JSON array. Only extract clear, explicit items — do not infer or guess. "
        "If nothing worth extracting, return [].";

    Json messages = Json::array();
    messages.push_back({{"role", "user"}, {"content", conversation}});

    auto response = apiClient.call(messages, Json::array());
    if (!response) {
        spdlog::warn("Auto-dream LLM extract: API call failed: {}", response.error());
        return 0;
    }

    String content;
    if (response->contains("content") && (*response)["content"].is_array()) {
        for (const auto& block : (*response)["content"]) {
            if (block.value("type", "") == "text") {
                content += block.value("text", "");
            }
        }
    } else if (response->contains("choices") && !(*response)["choices"].empty()) {
        content = (*response)["choices"][0]["message"]["content"].get<String>();
    }

    if (content.empty()) return 0;

    auto jsonStart = content.find('[');
    auto jsonEnd = content.rfind(']');
    if (jsonStart == String::npos || jsonEnd == String::npos) return 0;

    try {
        auto items = Json::parse(content.substr(jsonStart, jsonEnd - jsonStart + 1));
        if (!items.is_array()) return 0;

        int extracted = 0;
        for (const auto& item : items) {
            String type = item.value("type", "");
            String name = item.value("name", "");
            String desc = item.value("description", "");
            String body = item.value("content", "");

            if (name.empty() || body.empty()) continue;

            memory::MemoryType memType = memory::MemoryType::User;
            if (type == "project_fact") memType = memory::MemoryType::Project;
            else if (type == "correction") memType = memory::MemoryType::Feedback;

            memory::MemoryEntry entry;
            entry.name = name;
            entry.description = desc.empty() ? body.substr(0, std::min(body.size(), size_t(100))) : desc;
            entry.type = memType;
            entry.content = body;
            memoryService_->save(entry);
            extracted++;

            spdlog::debug("Auto-dream LLM: extracted {} memory '{}' — {}",
                type, name, entry.description);
        }

        return extracted;
    } catch (const Json::parse_error& e) {
        spdlog::debug("Auto-dream LLM extract: failed to parse response: {}", e.what());
        return 0;
    }
}

int AutoDream::dreamWithLLM(const std::vector<Message>& recentMessages, ApiClient& apiClient) {
    std::lock_guard lock(mutex_);

    if (!enabled_ || !memoryService_) return 0;

    int consolidated = 0;

    consolidated += mergeWithLLM(apiClient);
    consolidated += clearExpiredProjectMemories();

    if (!recentMessages.empty()) {
        consolidated += extractPreferencesWithLLM(recentMessages, apiClient);
    }

    totalDreams_++;
    totalConsolidated_ += consolidated;
    lastDreamTime_ = std::chrono::steady_clock::now();

    if (consolidated > 0) {
        spdlog::debug("Auto-dream (LLM): consolidated {} memory items (total: {})",
            consolidated, totalConsolidated_);
    }

    return consolidated;
}

} // namespace claude
