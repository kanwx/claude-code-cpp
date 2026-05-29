#include <claude/api/MessageRepair.hpp>
#include <spdlog/spdlog.h>
#include <unordered_set>
#include <sstream>

namespace claude {
namespace MessageRepair {

// ============================================================================
// Helpers
// ============================================================================

/// Extract all tool_use ids from an assistant message's content blocks.
/// Handles both string content and array-of-blocks content.
static std::unordered_set<String> collectToolUseIds(const Json& message) {
    std::unordered_set<String> ids;
    if (message.value("role", "") != "assistant") return ids;

    const auto& content = message["content"];
    if (content.is_string()) return ids;  // plain text content, no tool_use

    if (content.is_array()) {
        for (const auto& block : content) {
            String type = block.value("type", "");
            if (type == "tool_use" || type == "server_tool_use") {
                String id = block.value("id", "");
                if (!id.empty()) {
                    ids.insert(id);
                }
            }
        }
    }
    return ids;
}

/// Extract all tool_result tool_use_ids from a user message's content blocks.
static std::unordered_set<String> collectToolResultIds(const Json& message) {
    std::unordered_set<String> ids;
    if (message.value("role", "") != "user") return ids;

    const auto& content = message["content"];
    if (content.is_string()) return ids;

    if (content.is_array()) {
        for (const auto& block : content) {
            if (block.value("type", "") == "tool_result") {
                String toolUseId = block.value("tool_use_id", "");
                if (!toolUseId.empty()) {
                    ids.insert(toolUseId);
                }
            }
        }
    }
    return ids;
}

/// Make a synthetic error tool_result block for a given tool_use id.
static Json makeSyntheticToolResult(const String& toolUseId, const String& toolName = "") {
    Json block;
    block["type"] = "tool_result";
    block["tool_use_id"] = toolUseId;
    block["content"] = "Error: tool_result for tool_use " + toolUseId +
                       (toolName.empty() ? "" : " (" + toolName + ")") +
                       " was not provided. This is a synthetic error result.";
    block["is_error"] = true;
    return block;
}

/// Get tool_use name by id from an assistant message.
static String getToolNameById(const Json& message, const String& id) {
    if (message.value("role", "") != "assistant") return "";
    const auto& content = message["content"];
    if (!content.is_array()) return "";
    for (const auto& block : content) {
        String type = block.value("type", "");
        if ((type == "tool_use" || type == "server_tool_use") && block.value("id", "") == id) {
            return block.value("name", "");
        }
    }
    return "";
}

// ============================================================================
// Forward repair: insert synthetic tool_result for unmatched tool_use
// ============================================================================

Json ensureToolResultPairing(const Json& messages) {
    if (!messages.is_array() || messages.empty()) return messages;

    Json result = messages;

    for (size_t i = 0; i < result.size(); ++i) {
        const auto& msg = result[i];
        if (msg.value("role", "") != "assistant") continue;

        auto toolUseIds = collectToolUseIds(msg);
        if (toolUseIds.empty()) continue;

        // Look at the next message for matching tool_result ids
        std::unordered_set<String> resultIds;
        size_t nextIdx = i + 1;

        if (nextIdx < result.size() && result[nextIdx].value("role", "") == "user") {
            resultIds = collectToolResultIds(result[nextIdx]);
        }

        // Find unmatched tool_use ids
        std::vector<std::pair<String, String>> unmatched;  // (id, name)
        for (const auto& id : toolUseIds) {
            if (resultIds.find(id) == resultIds.end()) {
                String name = getToolNameById(msg, id);
                unmatched.emplace_back(id, name);
            }
        }

        if (unmatched.empty()) continue;

        spdlog::warn("Forward repair: {} unmatched tool_use blocks at message index {}",
                      unmatched.size(), i);

        // If the next message is a user message with array content, append to it
        if (nextIdx < result.size() && result[nextIdx].value("role", "") == "user") {
            auto& nextContent = result[nextIdx]["content"];
            // Ensure content is an array
            if (nextContent.is_string()) {
                String text = nextContent.get<String>();
                nextContent = Json::array();
                nextContent.push_back({{"type", "text"}, {"text", text}});
            }
            if (!nextContent.is_array()) {
                nextContent = Json::array();
            }
            for (const auto& [id, name] : unmatched) {
                nextContent.push_back(makeSyntheticToolResult(id, name));
            }
        } else {
            // No following user message; insert one
            Json syntheticMsg;
            syntheticMsg["role"] = "user";
            syntheticMsg["content"] = Json::array();
            for (const auto& [id, name] : unmatched) {
                syntheticMsg["content"].push_back(makeSyntheticToolResult(id, name));
            }
            // Insert after the assistant message
            auto it = result.begin() + static_cast<ptrdiff_t>(nextIdx);
            result.insert(it, syntheticMsg);
        }
    }

    return result;
}

// ============================================================================
// Reverse repair: strip orphaned tool_result blocks
// ============================================================================

Json stripOrphanedToolResults(const Json& messages) {
    if (!messages.is_array() || messages.empty()) return messages;

    Json result = messages;

    for (size_t i = 0; i < result.size(); ++i) {
        auto& msg = result[i];
        if (msg.value("role", "") != "user") continue;
        if (!msg["content"].is_array()) continue;

        // Collect tool_use ids from the preceding assistant message
        std::unordered_set<String> validIds;
        if (i > 0 && result[i - 1].value("role", "") == "assistant") {
            validIds = collectToolUseIds(result[i - 1]);
        }

        // Filter tool_result blocks: keep only those with valid tool_use ids
        auto& content = msg["content"];
        Json newContent = Json::array();
        int stripped = 0;
        for (const auto& block : content) {
            if (block.value("type", "") == "tool_result") {
                String toolUseId = block.value("tool_use_id", "");
                if (validIds.find(toolUseId) == validIds.end()) {
                    stripped++;
                    spdlog::warn("Reverse repair: stripping orphaned tool_result for id '{}'", toolUseId);
                    continue;
                }
            }
            newContent.push_back(block);
        }

        if (stripped > 0) {
            // If we stripped everything and only had tool_results, leave at least
            // a minimal text block so the message is not empty
            if (newContent.empty()) {
                newContent.push_back({{"type", "text"}, {"text", "[orphaned results removed]"}});
            }
            content = newContent;
        }
    }

    return result;
}

// ============================================================================
// Deduplicate tool_use IDs across messages
// ============================================================================

Json deduplicateToolUseIds(const Json& messages) {
    if (!messages.is_array() || messages.empty()) return messages;

    Json result = messages;
    std::unordered_set<String> seenIds;
    int dedupCount = 0;

    for (auto& msg : result) {
        if (msg.value("role", "") != "assistant") continue;
        auto& content = msg["content"];
        if (!content.is_array()) continue;

        for (auto& block : content) {
            String type = block.value("type", "");
            if (type != "tool_use" && type != "server_tool_use") continue;

            String id = block.value("id", "");
            if (id.empty()) continue;

            if (seenIds.count(id)) {
                // Generate a unique id by appending a suffix
                String baseId = id;
                int suffix = 2;
                String newId;
                do {
                    newId = baseId + "_dup" + std::to_string(suffix++);
                } while (seenIds.count(newId));

                spdlog::warn("Deduplicating tool_use id '{}' -> '{}'", id, newId);
                block["id"] = newId;
                id = newId;
                dedupCount++;
            }
            seenIds.insert(id);
        }
    }

    if (dedupCount > 0) {
        spdlog::debug("Deduplicated {} tool_use ids", dedupCount);
    }

    return result;
}

// ============================================================================
// Full repair pipeline
// ============================================================================

Json repair(const Json& messages) {
    if (!messages.is_array() || messages.empty()) return messages;

    spdlog::debug("MessageRepair::repair() starting with {} messages", messages.size());

    // 1. Deduplicate tool_use ids first
    auto result = deduplicateToolUseIds(messages);

    // 2. Forward repair: add missing tool_results
    result = ensureToolResultPairing(result);

    // 3. Reverse repair: strip orphaned tool_results
    result = stripOrphanedToolResults(result);

    spdlog::debug("MessageRepair::repair() done, {} messages", result.size());
    return result;
}

} // namespace MessageRepair
} // namespace claude
