#include <claude/core/memory/MemoryExtraction.hpp>
#include <claude/api/ApiClient.hpp>
#include <claude/services/MarkdownMemoryService.hpp>
#include <spdlog/spdlog.h>

namespace claude {

std::vector<ExtractedMemory> MemoryExtraction::extractFromMessages(
    const std::vector<Message>& messages,
    int maxRecentMessages
) {
    std::vector<ExtractedMemory> results;

    int start = std::max(0, static_cast<int>(messages.size()) - maxRecentMessages);
    for (int i = start; i < static_cast<int>(messages.size()); ++i) {
        const auto& msg = messages[i];
        if (msg.role != MessageRole::User) continue;
        if (!hasExtractableContent(msg)) continue;

        // 模式匹配提取
        const String& content = msg.content;

        // 检测用户偏好 (否定模式)
        if (content.find("我不喜欢") != String::npos ||
            content.find("I prefer") != String::npos ||
            content.find("I don't like") != String::npos) {
            ExtractedMemory mem;
            mem.type = ExtractedMemory::Feedback;
            mem.name = "user-preference";
            mem.description = "User preference detected from conversation";
            mem.content = content.substr(0, std::min(content.size(), size_t(200)));
            mem.confidence = 0.6;
            results.push_back(mem);
        }

        // 检测项目状态更新
        if (content.find("项目") != String::npos ||
            content.find("project") != String::npos ||
            content.find("sprint") != String::npos ||
            content.find("deadline") != String::npos) {
            ExtractedMemory mem;
            mem.type = ExtractedMemory::Project;
            mem.name = "project-status";
            mem.description = "Project status update from user";
            mem.content = content.substr(0, std::min(content.size(), size_t(200)));
            mem.confidence = 0.5;
            results.push_back(mem);
        }

        // 检测反馈/纠正
        if (content.find("不要") != String::npos ||
            content.find("don't") != String::npos ||
            content.find("stop") != String::npos ||
            content.find("wrong") != String::npos ||
            content.find("incorrect") != String::npos) {
            ExtractedMemory mem;
            mem.type = ExtractedMemory::Feedback;
            mem.name = "user-correction";
            mem.description = "User correction/feedback from conversation";
            mem.content = content.substr(0, std::min(content.size(), size_t(200)));
            mem.confidence = 0.7;
            results.push_back(mem);
        }
    }

    return results;
}

bool MemoryExtraction::hasExtractableContent(const Message& message) {
    if (message.role != MessageRole::User) return false;
    if (message.content.empty()) return false;
    // 短消息不太可能包含可提取信息
    if (message.content.size() < 10) return false;
    return true;
}

int MemoryExtraction::saveExtracted(
    const std::vector<ExtractedMemory>& memories,
    memory::MarkdownMemoryService& service
) {
    int saved = 0;
    for (const auto& mem : memories) {
        if (mem.confidence < 0.5) continue;  // 只保存高置信度

        memory::MemoryType type;
        switch (mem.type) {
            case ExtractedMemory::User: type = memory::MemoryType::User; break;
            case ExtractedMemory::Feedback: type = memory::MemoryType::Feedback; break;
            case ExtractedMemory::Project: type = memory::MemoryType::Project; break;
            case ExtractedMemory::Reference: type = memory::MemoryType::Reference; break;
            default: type = memory::MemoryType::User; break;
        }

        try {
            memory::MemoryEntry entry;
            entry.name = mem.name;
            entry.description = mem.description;
            entry.type = type;
            entry.content = mem.content;
            entry.createdAt = std::chrono::system_clock::now();
            entry.updatedAt = entry.createdAt;
            service.save(entry);
            saved++;
            spdlog::debug("MemoryExtraction: saved memory '{}'", mem.name);
        } catch (const std::exception& e) {
            spdlog::warn("MemoryExtraction: failed to save '{}': {}", mem.name, e.what());
        }
    }
    return saved;
}

// ========== LLM-based extraction ==========

namespace {

/// Map LLM-returned type string to ExtractedMemory::Type
ExtractedMemory::Type parseMemoryType(const String& typeStr) {
    if (typeStr == "preference" || typeStr == "user" || typeStr == "User") {
        return ExtractedMemory::User;
    }
    if (typeStr == "correction" || typeStr == "feedback" || typeStr == "Feedback") {
        return ExtractedMemory::Feedback;
    }
    if (typeStr == "project" || typeStr == "Project") {
        return ExtractedMemory::Project;
    }
    if (typeStr == "reference" || typeStr == "Reference") {
        return ExtractedMemory::Reference;
    }
    // Default fallback
    return ExtractedMemory::Feedback;
}

/// Build the system prompt for LLM memory extraction
String buildExtractionSystemPrompt() {
    return R"(You are a memory extraction assistant. Analyze the conversation and extract memorable facts that should be persisted for future sessions.

Extract these categories:
- **preference**: User preferences (formatting style, language, tool preferences, coding conventions)
- **correction**: Corrections or things the user explicitly said to avoid or stop doing
- **project**: Project facts (architecture decisions, naming conventions, tech stack, deadlines, constraints)
- **reference**: Important reference information (API endpoints, config values, version numbers)

Rules:
1. Only extract explicit, concrete facts — do not infer or guess.
2. Each item must have a short name (snake_case identifier), a one-line description, and the full content.
3. Assign a confidence score 0.0-1.0 reflecting how certain you are that this is a memorable, persistent fact.
4. If nothing memorable is found, return an empty array.

Return ONLY a JSON array, no other text:
[
  {"type": "preference", "name": "coding_style", "description": "User prefers...", "content": "...", "confidence": 0.9},
  {"type": "correction", "name": "avoid_x", "description": "User said to avoid...", "content": "...", "confidence": 0.85}
]

If no memories should be extracted, return: [])";
}

} // anonymous namespace

std::vector<ExtractedMemory> MemoryExtraction::extractWithLLM(
    const std::vector<Message>& messages,
    ApiClient& apiClient,
    int maxMessages
) {
    std::vector<ExtractedMemory> results;

    if (messages.empty()) {
        return results;
    }

    // Build conversation text from last N messages
    int start = std::max(0, static_cast<int>(messages.size()) - maxMessages);
    std::ostringstream conversationText;
    for (int i = start; i < static_cast<int>(messages.size()); ++i) {
        const auto& msg = messages[i];
        String roleLabel;
        switch (msg.role) {
            case MessageRole::User: roleLabel = "User"; break;
            case MessageRole::Assistant: roleLabel = "Assistant"; break;
            case MessageRole::System: roleLabel = "System"; break;
            case MessageRole::ToolResult: roleLabel = "ToolResult"; break;
            default: roleLabel = "Unknown"; break;
        }
        // Truncate very long messages to keep prompt size manageable
        String content = msg.content;
        if (content.size() > 1000) {
            content = content.substr(0, 997) + "...";
        }
        if (!content.empty()) {
            conversationText << "[" << roleLabel << "]: " << content << "\n\n";
        }
    }

    if (conversationText.str().empty()) {
        return results;
    }

    // Build API request
    String systemPrompt = buildExtractionSystemPrompt();
    String userPrompt = "Extract memories from the following conversation:\n\n" + conversationText.str();

    Json requestMessages = Json::array();
    requestMessages.push_back({{"role", "system"}, {"content", systemPrompt}});
    requestMessages.push_back({{"role", "user"}, {"content", userPrompt}});

    Json noTools = Json::array();

    // Call LLM
    auto response = apiClient.call(requestMessages, noTools);
    if (!response) {
        spdlog::warn("MemoryExtraction::extractWithLLM: LLM call failed: {}", response.error());
        return results;
    }

    // Extract text from response (handle both Anthropic and OpenAI formats)
    String responseText;
    if (response->contains("content") && (*response)["content"].is_array()) {
        for (const auto& block : (*response)["content"]) {
            if (block.value("type", "") == "text") {
                responseText += block["text"].get<String>();
            }
        }
    } else if (response->contains("choices") && !(*response)["choices"].empty()) {
        responseText = (*response)["choices"][0]["message"]["content"].get<String>();
    }

    if (responseText.empty()) {
        spdlog::debug("MemoryExtraction::extractWithLLM: LLM returned empty response");
        return results;
    }

    // Parse JSON from response — LLM may wrap in markdown code blocks
    String jsonStr = responseText;
    // Strip ```json ... ``` wrapper if present
    auto fenceStart = jsonStr.find("```json");
    if (fenceStart != String::npos) {
        auto contentStart = jsonStr.find('\n', fenceStart);
        if (contentStart != String::npos) {
            auto fenceEnd = jsonStr.find("```", contentStart + 1);
            if (fenceEnd != String::npos) {
                jsonStr = jsonStr.substr(contentStart + 1, fenceEnd - contentStart - 1);
            }
        }
    } else {
        auto fenceStartPlain = jsonStr.find("```");
        if (fenceStartPlain != String::npos) {
            auto contentStart = jsonStr.find('\n', fenceStartPlain);
            if (contentStart != String::npos) {
                auto fenceEnd = jsonStr.find("```", contentStart + 1);
                if (fenceEnd != String::npos) {
                    jsonStr = jsonStr.substr(contentStart + 1, fenceEnd - contentStart - 1);
                }
            }
        }
    }

    // Trim whitespace
    while (!jsonStr.empty() && (jsonStr.front() == ' ' || jsonStr.front() == '\n' || jsonStr.front() == '\r' || jsonStr.front() == '\t')) {
        jsonStr.erase(jsonStr.begin());
    }
    while (!jsonStr.empty() && (jsonStr.back() == ' ' || jsonStr.back() == '\n' || jsonStr.back() == '\r' || jsonStr.back() == '\t')) {
        jsonStr.pop_back();
    }

    // Parse the JSON array
    Json parsed;
    try {
        parsed = Json::parse(jsonStr);
    } catch (const Json::parse_error& e) {
        spdlog::warn("MemoryExtraction::extractWithLLM: failed to parse LLM JSON response: {}", e.what());
        spdlog::debug("MemoryExtraction::extractWithLLM: raw response was: {}", responseText.substr(0, 500));
        return results;
    }

    if (!parsed.is_array()) {
        spdlog::debug("MemoryExtraction::extractWithLLM: LLM response is not a JSON array");
        return results;
    }

    // Convert each item to ExtractedMemory
    for (const auto& item : parsed) {
        try {
            ExtractedMemory mem;
            mem.type = parseMemoryType(item.value("type", "feedback"));
            mem.name = item.value("name", "unnamed");
            mem.description = item.value("description", "");
            mem.content = item.value("content", "");
            mem.confidence = item.value("confidence", 0.5);

            // Clamp confidence to [0, 1]
            if (mem.confidence < 0.0) mem.confidence = 0.0;
            if (mem.confidence > 1.0) mem.confidence = 1.0;

            // Skip entries with empty content
            if (mem.content.empty() && mem.description.empty()) {
                continue;
            }

            results.push_back(std::move(mem));
        } catch (const std::exception& e) {
            spdlog::debug("MemoryExtraction::extractWithLLM: skipping malformed item: {}", e.what());
        }
    }

    spdlog::debug("MemoryExtraction::extractWithLLM: extracted {} memories via LLM", results.size());
    return results;
}

} // namespace claude
