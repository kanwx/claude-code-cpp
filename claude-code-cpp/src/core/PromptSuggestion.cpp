#include <claude/core/PromptSuggestion.hpp>
#include <algorithm>
#include <regex>
#include <chrono>
#include <spdlog/spdlog.h>

namespace claude {

PromptSuggestion::GateResult PromptSuggestion::checkGate(
    int assistantTurns, bool hasPendingPermissions,
    bool isPlanMode, bool isRateLimited,
    int uncachedTokens, bool isElicitation) const
{
    if (assistantTurns < minAssistantTurns_) {
        return {false, "not enough assistant turns (" + std::to_string(assistantTurns) +
                       " < " + std::to_string(minAssistantTurns_) + ")"};
    }
    if (hasPendingPermissions) {
        return {false, "pending permissions"};
    }
    if (isPlanMode) {
        return {false, "plan mode active"};
    }
    if (isRateLimited) {
        return {false, "rate limited"};
    }
    if (isElicitation) {
        return {false, "elicitation in progress"};
    }
    if (uncachedTokens > maxUncachedTokens_) {
        return {false, "too many uncached tokens (" + std::to_string(uncachedTokens) +
                       " > " + std::to_string(maxUncachedTokens_) + ")"};
    }
    return {true, ""};
}

std::optional<PromptSuggestion::Suggestion> PromptSuggestion::generate(
    const std::vector<Json>& conversationHistory)
{
    if (!generateCallback_) {
        spdlog::debug("PromptSuggestion: no generate callback");
        return std::nullopt;
    }

    // 构建系统提示
    String systemPrompt =
        "You are a prompt suggestion engine. Given the conversation so far, "
        "predict what the user would type next. Output ONLY the raw text the user "
        "would type — no quotes, no explanation, no markdown. "
        "2-12 words. Match the user's style (imperative commands, not questions). "
        "Do NOT suggest: 'done', 'looks good', 'thank you', or meta-text. "
        "Do NOT use Claude-voice ('Let me...', 'I'll...'). "
        "Suggest a concrete next action, not a vague goal.";

    // 构建用户提示 (对话摘要)
    String userPrompt = "Conversation so far:\n\n";
    int turnCount = 0;
    for (const auto& msg : conversationHistory) {
        if (!msg.is_object()) continue;
        String role = msg.value("role", "");
        if (role == "user") {
            String content = msg.value("content", "");
            if (content.size() > 200) content = content.substr(0, 200) + "...";
            userPrompt += "User: " + content + "\n";
            turnCount++;
        } else if (role == "assistant") {
            String content;
            if (msg.contains("content") && msg["content"].is_array()) {
                for (const auto& block : msg["content"]) {
                    if (!block.is_object()) continue;
                    if (block.value("type", "") == "text") {
                        content = block.value("text", "");
                        break;
                    }
                }
            } else {
                content = msg.value("content", "");
            }
            if (content.size() > 200) content = content.substr(0, 200) + "...";
            userPrompt += "Assistant: " + content + "\n";
            turnCount++;
        }
        if (turnCount >= 10) break; // 只取最近几轮
    }
    userPrompt += "\nWhat would the user type next?";

    auto result = generateCallback_(systemPrompt, userPrompt);
    if (!result) {
        spdlog::debug("PromptSuggestion: generation failed");
        return std::nullopt;
    }

    totalGenerated_++;

    // 清理
    String cleaned = cleanSuggestion(*result);

    // 过滤
    if (shouldFilter(cleaned)) {
        totalFiltered_++;
        spdlog::debug("PromptSuggestion: filtered '{}'", cleaned.substr(0, 50));
        return std::nullopt;
    }

    Suggestion suggestion;
    suggestion.text = cleaned;
    // Detect stated intent: if the user's last message contains explicit intent
    // markers like "I want to", "I need to", "please", "can you", etc.
    suggestion.promptId = "user_intent";
    if (!conversationHistory.empty()) {
        String lastUserMsg;
        for (auto it = conversationHistory.rbegin(); it != conversationHistory.rend(); ++it) {
            if (it->is_object() && it->value("role", "") == "user") {
                lastUserMsg = it->value("content", "");
                break;
            }
        }
        if (!lastUserMsg.empty()) {
            String lower = lastUserMsg;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            static const std::vector<String> intentMarkers = {
                "i want to ", "i need to ", "i'd like to ", "i would like to ",
                "please ", "can you ", "could you ", "help me ",
                "i'm trying to ", "i am trying to ", "my goal is ",
                "i'm looking for ", "i am looking for "
            };
            for (const auto& marker : intentMarkers) {
                if (lower.find(marker) != String::npos) {
                    suggestion.promptId = "stated_intent";
                    break;
                }
            }
        }
    }
    suggestion.generationRequestId = "gen_" + std::to_string(totalGenerated_);

    lastSuggestion_ = suggestion;
    spdlog::debug("PromptSuggestion: generated '{}'", cleaned.substr(0, 50));
    return suggestion;
}

bool PromptSuggestion::shouldFilter(const String& suggestion) {
    if (suggestion.empty()) return true;

    // 太短 (< 2 词)
    int wordCount = 1;
    for (char c : suggestion) {
        if (std::isspace(c)) wordCount++;
    }
    if (wordCount < 2) return true;

    // 太长 (> 12 词)
    if (wordCount > 12) return true;

    // 完成类
    static const std::vector<String> donePatterns = {
        "done", "looks good", "looks great", "thank", "thanks",
        "no more", "that's it", "all set", "perfect",
        "great job", "good job", "nice work", "well done"
    };
    String lower = suggestion;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& pattern : donePatterns) {
        if (lower.find(pattern) != String::npos) return true;
    }

    // Claude 语气
    if (lower.find("let me") == 0) return true;
    if (lower.find("i'll ") != String::npos) return true;
    if (lower.find("i will ") != String::npos) return true;
    if (lower.find("i can ") != String::npos) return true;
    if (lower.find("i would ") != String::npos) return true;

    // 评估性短语
    static const std::vector<String> evaluative = {
        "i think", "i believe", "in my opinion", "it seems",
        "appears to", "probably", "maybe", "perhaps"
    };
    for (const auto& pattern : evaluative) {
        if (lower.find(pattern) != String::npos) return true;
    }

    // 多句 (包含句号+空格)
    if (suggestion.find(". ") != String::npos) return true;

    // 格式化文本 (markdown)
    if (suggestion.find("**") != String::npos) return true;
    if (suggestion.find("```") != String::npos) return true;
    if (suggestion[0] == '#') return true;
    if (suggestion[0] == '-') return true;

    // 括号包裹
    if (suggestion.front() == '(' && suggestion.back() == ')') return true;
    if (suggestion.front() == '[' && suggestion.back() == ']') return true;

    // 引号包裹
    if (suggestion.front() == '"' && suggestion.back() == '"') return true;
    if (suggestion.front() == '\'' && suggestion.back() == '\'') return true;

    // 元文本
    static const std::vector<String> meta = {
        "silence", "as an ai", "as a language model",
        "i'm sorry", "i am sorry", "i cannot"
    };
    for (const auto& pattern : meta) {
        if (lower.find(pattern) != String::npos) return true;
    }

    return false;
}

String PromptSuggestion::cleanSuggestion(const String& raw) {
    String result = raw;

    // 去首尾空白
    while (!result.empty() && std::isspace(result.front())) result.erase(result.begin());
    while (!result.empty() && std::isspace(result.back())) result.pop_back();

    // 去首尾引号
    if (result.size() >= 2) {
        if ((result.front() == '"' && result.back() == '"') ||
            (result.front() == '\'' && result.back() == '\'') ||
            (result.front() == '`' && result.back() == '`')) {
            result = result.substr(1, result.size() - 2);
        }
    }

    // 去可能的 "User: " 前缀
    if (result.size() > 6 && result.substr(0, 6) == "User: ") {
        result = result.substr(6);
    }
    if (result.size() > 12 && result.substr(0, 12) == "Next prompt: ") {
        result = result.substr(12);
    }

    // 再次去首尾空白
    while (!result.empty() && std::isspace(result.front())) result.erase(result.begin());
    while (!result.empty() && std::isspace(result.back())) result.pop_back();

    return result;
}

void PromptSuggestion::markShown() {
    if (lastSuggestion_) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        lastSuggestion_->shownAt = now;
    }
}

void PromptSuggestion::markAccepted() {
    if (lastSuggestion_) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        lastSuggestion_->acceptedAt = now;
    }
}

} // namespace claude
