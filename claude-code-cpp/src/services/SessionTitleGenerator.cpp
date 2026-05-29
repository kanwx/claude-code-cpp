#include <claude/services/SessionTitleGenerator.hpp>
#include <claude/api/ApiClient.hpp>
#include <spdlog/spdlog.h>

namespace claude {

std::optional<String> SessionTitleGenerator::generate(const std::vector<Message>& messages) {
    // 如果已有标题，不重复生成
    if (cachedTitle_) return cachedTitle_;

    // 至少需要一条用户消息
    bool hasUserMsg = false;
    for (const auto& msg : messages) {
        if (msg.role == MessageRole::User && !msg.content.empty()) {
            hasUserMsg = true;
            break;
        }
    }
    if (!hasUserMsg) return std::nullopt;

    // 尝试 LLM 生成
    if (apiClient_) {
        try {
            // 构建摘要用消息
            String conversation;
            int msgCount = 0;
            for (const auto& msg : messages) {
                if (msgCount >= 6) break;  // 只取前几条
                if (msg.role == MessageRole::User || msg.role == MessageRole::Assistant) {
                    String content = msg.content;
                    if (content.length() > 200) content = content.substr(0, 197) + "...";
                    conversation += content + "\n";
                    msgCount++;
                }
            }

            auto result = apiClient_->call(
                {{{"role", "system"}, {"content",
                    "Generate a very short title (3-6 words) for a coding conversation. "
                    "Only output the title, nothing else. No quotes. No punctuation at the end."}},
                 {{"role", "user"}, {"content", conversation}}},
                Json::array()
            );

            if (result && (*result).contains("content")) {
                auto& content = (*result)["content"];
                String title;
                if (content.is_array() && !content.empty()) {
                    title = content[0].value("text", "");
                } else if (content.is_string()) {
                    title = content.get<String>();
                }

                // Clean up title
                if (!title.empty()) {
                    // Remove quotes
                    if (title.starts_with('"') && title.ends_with('"')) {
                        title = title.substr(1, title.length() - 2);
                    }
                    if (title.starts_with('\'') && title.ends_with('\'')) {
                        title = title.substr(1, title.length() - 2);
                    }
                    // Trim
                    size_t start = title.find_first_not_of(" \t\n\r");
                    size_t end = title.find_last_not_of(" \t\n\r");
                    if (start != String::npos) {
                        title = title.substr(start, end - start + 1);
                    }
                    // Limit length
                    if (title.length() > 60) {
                        title = title.substr(0, 57) + "...";
                    }

                    if (!title.empty()) {
                        cachedTitle_ = title;
                        spdlog::debug("Session title: '{}'", title);
                        return cachedTitle_;
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::debug("SessionTitleGenerator: LLM generation failed: {}", e.what());
        }
    }

    // 回退: 简单提取
    String simpleTitle = extractSimpleTitle(messages);
    if (!simpleTitle.empty()) {
        cachedTitle_ = simpleTitle;
        spdlog::debug("Session title (simple): '{}'", simpleTitle);
        return cachedTitle_;
    }

    return std::nullopt;
}

String SessionTitleGenerator::extractSimpleTitle(const std::vector<Message>& messages) {
    for (const auto& msg : messages) {
        if (msg.role != MessageRole::User) continue;
        if (msg.content.empty()) continue;

        String content = msg.content;

        // 跳过斜杠命令
        if (content.starts_with("/")) continue;

        // 取第一行
        size_t newline = content.find('\n');
        if (newline != String::npos) {
            content = content.substr(0, newline);
        }

        // Trim
        size_t start = content.find_first_not_of(" \t");
        if (start == String::npos) continue;
        content = content.substr(start);

        // 截断
        if (content.length() > 50) {
            content = content.substr(0, 47) + "...";
        }

        return content;
    }

    return "";
}

} // namespace claude
