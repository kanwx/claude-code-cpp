#include <claude/services/AwaySummary.hpp>
#include <claude/api/ApiClient.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude {

void AwaySummary::recordActivity() {
    auto now = std::chrono::steady_clock::now();
    auto away = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity_);

    if (away >= awayThreshold_) {
        wasAway_ = true;
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(away).count();
        spdlog::debug("AwaySummary: user was away for {} minutes", minutes);
    }

    lastActivity_ = now;
}

bool AwaySummary::wasAway() const {
    return wasAway_;
}

std::optional<String> AwaySummary::generateSummary(
    const std::vector<Message>& recentMessages
) {
    if (!wasAway_) return std::nullopt;
    wasAway_ = false;

    auto awayDuration = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - lastActivity_).count();

    // 简单摘要: 列出离开期间的消息
    std::ostringstream oss;
    oss << "=== Welcome Back ===\n\n";
    oss << "You were away for approximately " << awayDuration << " minutes.\n\n";

    if (recentMessages.empty()) {
        oss << "No new activity while you were away.\n";
    } else {
        oss << "While you were away, there were " << recentMessages.size() << " messages:\n\n";

        for (const auto& msg : recentMessages) {
            String role;
            switch (msg.role) {
                case MessageRole::User: role = "You"; break;
                case MessageRole::Assistant: role = "Assistant"; break;
                case MessageRole::System: role = "System"; break;
                case MessageRole::ToolResult: role = "Tool"; break;
            }
            String content = msg.content;
            if (content.length() > 100) content = content.substr(0, 97) + "...";
            oss << "  [" << role << "] " << content << "\n";
        }
    }

    // 如果有 API 客户端，使用 LLM 生成更好的摘要
    if (apiClient_) {
        try {
            String messagesText;
            for (const auto& msg : recentMessages) {
                messagesText += msg.content + "\n";
            }

            Json messages = Json::array();
            messages.push_back({{"role", "user"}, {"content",
                "Summarize the following conversation activity in 2-3 sentences:\n\n" + messagesText}});

            auto result = apiClient_->call(
                {{{"role", "system"}, {"content", "You are a helpful assistant that summarizes conversation progress concisely."}}},
                Json::array()
            );

            if (result && (*result).contains("content")) {
                auto& content = (*result)["content"];
                if (content.is_array() && !content.empty()) {
                    String summary = content[0].value("text", "");
                    if (!summary.empty()) {
                        oss << "\nSummary: " << summary << "\n";
                    }
                } else if (content.is_string()) {
                    oss << "\nSummary: " << content.get<String>() << "\n";
                }
            }
        } catch (...) {
            // LLM 摘要失败，使用简单摘要
        }
    }

    return oss.str();
}

} // namespace claude
