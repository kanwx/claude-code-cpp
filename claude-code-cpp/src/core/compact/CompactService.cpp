#include <claude/core/compact/CompactService.hpp>
#include <claude/core/compact/CompactPrompt.hpp>
#include <claude/api/ApiClient.hpp>
#include <regex>
#include <spdlog/spdlog.h>

namespace claude::compact {

CompactService::CompactService(const CompactConfig& config)
    : config_(config) {}

CompactResult CompactService::compact(const std::vector<Message>& messages) {
    CompactResult result;
    result.originalTokens = 0;
    result.success = false;

    if (messages.empty()) {
        result.error = "No messages to compact";
        return result;
    }

    // 估算原始 token
    for (const auto& msg : messages) {
        result.originalTokens += estimateTokens(msg.content);
    }

    // 移除图片 (如果配置)
    std::vector<Message> processedMsgs;
    if (config_.stripImages) {
        for (const auto& msg : messages) {
            processedMsgs.push_back(stripImages(msg));
        }
    } else {
        processedMsgs = messages;
    }

    // 保留最近 N 条消息
    std::vector<Message> recentMsgs;
    if (processedMsgs.size() > config_.retainRecentMsgs) {
        recentMsgs.assign(processedMsgs.end() - config_.retainRecentMsgs,
                          processedMsgs.end());
    } else {
        recentMsgs = processedMsgs;
    }

    // 生成压缩提示
    String compactPrompt = buildCompactPrompt(processedMsgs);

    // Call LLM for summary if API client is available
    if (apiClient_) {
        try {
            // Build messages for the summarization request
            Json apiMessages = Json::array();
            Json systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = CompactPrompt::getBasePrompt();
            apiMessages.push_back(systemMsg);

            Json userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = compactPrompt;
            apiMessages.push_back(userMsg);

            // No tools needed for summarization
            Json noTools = Json::array();

            // Set max tokens for compact output
            apiClient_->setMaxTokens(static_cast<int>(config_.maxOutputTokens));

            auto apiResult = apiClient_->call(apiMessages, noTools);
            if (apiResult && apiResult->contains("content") && (*apiResult)["content"].is_array()) {
                String summary;
                for (const auto& block : (*apiResult)["content"]) {
                    if (block.value("type", "") == "text") {
                        summary += block.value("text", "");
                    }
                }
                if (!summary.empty()) {
                    result.summary = summary;
                    spdlog::debug("CompactService: LLM summary generated ({} chars)", summary.size());
                } else {
                    result.summary = compactPrompt;
                    spdlog::warn("CompactService: LLM returned empty summary, using prompt as fallback");
                }
            } else {
                result.summary = compactPrompt;
                spdlog::warn("CompactService: LLM call failed, using prompt as fallback");
            }
        } catch (const std::exception& e) {
            result.summary = compactPrompt;
            spdlog::warn("CompactService: LLM exception: {}, using prompt as fallback", e.what());
        }
    } else {
        result.summary = compactPrompt;
        spdlog::debug("CompactService: no API client, using prompt as summary");
    }

    result.compressedTokens = estimateTokens(result.summary);
    result.retainedMsgs = recentMsgs;
    result.success = true;

    return result;
}

size_t CompactService::estimateTokens(const String& text) const {
    // 简单估算: ~4 字符 = 1 token
    // 中文可能更少，这里用保守估计
    return text.size() / 4;
}

String CompactService::buildCompactPrompt(const std::vector<Message>& messages) const {
    String prompt = CompactPrompt::getBasePrompt();
    prompt += "\n\n=== CONVERSATION TO SUMMARIZE ===\n\n";

    for (const auto& msg : messages) {
        switch (msg.role) {
            case MessageRole::User:
                prompt += "[USER]: " + msg.content + "\n\n";
                break;
            case MessageRole::Assistant:
                prompt += "[ASSISTANT]: " + msg.content + "\n\n";
                if (msg.hasToolCalls()) {
                    for (const auto& call : msg.toolCalls) {
                        prompt += "[TOOL_CALL]: " + call.name + "(" + call.arguments + ")\n";
                    }
                    prompt += "\n";
                }
                break;
            case MessageRole::ToolResult:
                for (const auto& resp : msg.toolResults) {
                    prompt += "[TOOL_RESULT]: " + resp.content + "\n\n";
                }
                break;
            case MessageRole::System:
                // 系统消息通常保留，不压缩
                break;
        }
    }

    return prompt;
}

Message CompactService::stripImages(const Message& msg) const {
    Message result = msg;

    // 移除图片标记 (简化实现)
    // 匹配 ![alt](url) 或 <image> 标签
    static const std::regex imgPattern(R"(!\[[^\]]*\]\([^)]+\)|<image[^>]*>.*?</image>)");
    result.content = std::regex_replace(result.content, imgPattern, "[IMAGE REMOVED]");

    return result;
}

String CompactService::getSummaryExample() const {
    return CompactPrompt::getSummaryExample();
}

} // namespace claude::compact
