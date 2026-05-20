#include <claude/api/RetryableClient.hpp>
#include <spdlog/spdlog.h>

namespace claude {

std::expected<Json, String> RetryableClient::callWithRetry(
    const Json& messages,
    const Json& tools,
    std::function<void(const ApiError& error, int attempt, int delayMs)> onRetry
) {
    int attempt = 0;

    while (true) {
        ++attempt;

        // 尝试调用
        auto result = client_->call(messages, tools);

        if (result) {
            // 成功 → 重置 529 计数器
            consecutiveOverloadErrors_ = 0;
            return result;
        }

        // 失败，分析错误
        const String& errorMsg = result.error();

        // 尝试解析错误中的状态码
        ApiError error;
        int statusCode = 0;

        // 格式: "API error: 429 - {...}" 或 "HTTP request failed: ..."
        if (errorMsg.find("API error:") == 0) {
            auto colonPos = errorMsg.find(':', 10);
            if (colonPos != String::npos) {
                auto dashPos = errorMsg.find('-', colonPos);
                if (dashPos != String::npos) {
                    try {
                        statusCode = std::stoi(errorMsg.substr(colonPos + 1, dashPos - colonPos - 1));
                    } catch (...) {}
                }
            }
            error = RetryPolicy::classifyError(statusCode, errorMsg);
        } else if (errorMsg.find("HTTP request failed:") == 0) {
            error = RetryPolicy::classifyNetworkError(errorMsg);
        } else {
            error.type = ApiErrorType::Unknown;
            error.message = errorMsg;
        }

        // ========== 模型回退：529/overload 检测 ==========
        if (statusCode == 529 || statusCode == 429 ||
            errorMsg.find("overloaded") != String::npos ||
            errorMsg.find("rate_limit") != String::npos) {
            consecutiveOverloadErrors_++;

            if (consecutiveOverloadErrors_ >= OVERLOAD_THRESHOLD && attemptFallback()) {
                spdlog::info("Switched to fallback model: {}", client_->getModelName());
                // 用剥离 thinking 的消息重试
                auto stripped = stripThinkingBlocks(messages);
                result = client_->call(stripped, tools);
                if (result) {
                    consecutiveOverloadErrors_ = 0;
                    return result;
                }
            }
        } else {
            // 非 overload 错误 → 重置计数器
            consecutiveOverloadErrors_ = 0;
        }

        // 检查是否应该重试
        if (!policy_.shouldRetry(error, attempt)) {
            SPDLOG_ERROR("API call failed (attempt {}): {} - {}",
                        attempt, static_cast<int>(error.type), errorMsg);
            return std::unexpected(errorMsg);
        }

        // 计算延迟
        int delayMs = policy_.calculateDelay(error, attempt);

        SPDLOG_WARN("API call failed, retrying in {}ms (attempt {}/{}): {}",
                   delayMs, attempt, policy_.config().maxRetries, errorMsg);

        // 回调
        if (onRetry) {
            onRetry(error, attempt, delayMs);
        }

        // 等待
        RetryPolicy::sleepMs(delayMs);
    }
}

void RetryableClient::streamWithRetry(
    const Json& messages,
    const Json& tools,
    std::function<void(const Json& chunk)> onChunk,
    std::function<void(const ApiError& error, int attempt, int delayMs)> onRetry
) {
    int attempt = 0;
    bool success = false;

    while (!success) {
        ++attempt;

        bool hasError = false;
        String errorMsg;
        int statusCode = 0;

        try {
            client_->stream(messages, tools, onChunk);
            success = true;
            consecutiveOverloadErrors_ = 0;
        } catch (const std::exception& e) {
            errorMsg = e.what();
            hasError = true;
        }

        if (success) return;

        // 分析错误
        ApiError error;
        if (errorMsg.find("HTTP") != String::npos) {
            error = RetryPolicy::classifyNetworkError(errorMsg);
        } else {
            error.type = ApiErrorType::Unknown;
            error.message = errorMsg;
        }

        // 检测 529
        if (errorMsg.find("529") != String::npos ||
            errorMsg.find("overloaded") != String::npos ||
            errorMsg.find("429") != String::npos) {
            consecutiveOverloadErrors_++;
            if (consecutiveOverloadErrors_ >= OVERLOAD_THRESHOLD && attemptFallback()) {
                spdlog::info("Switched to fallback model for stream: {}", client_->getModelName());
                auto stripped = stripThinkingBlocks(messages);
                try {
                    client_->stream(stripped, tools, onChunk);
                    consecutiveOverloadErrors_ = 0;
                    return;
                } catch (const std::exception& e) {
                    errorMsg = e.what();
                }
            }
        } else {
            consecutiveOverloadErrors_ = 0;
        }

        if (!policy_.shouldRetry(error, attempt)) {
            SPDLOG_ERROR("Stream failed (attempt {}): {}", attempt, errorMsg);
            throw std::runtime_error(errorMsg);
        }

        int delayMs = policy_.calculateDelay(error, attempt);
        SPDLOG_WARN("Stream failed, retrying in {}ms (attempt {}/{}): {}",
                   delayMs, attempt, policy_.config().maxRetries, errorMsg);

        if (onRetry) {
            onRetry(error, attempt, delayMs);
        }

        RetryPolicy::sleepMs(delayMs);
    }
}

bool RetryableClient::attemptFallback() {
    if (fallbackModel_.empty()) {
        return false;  // 没有配置回退模型
    }

    if (fallbackActive_) {
        return true;  // 已经在回退模型上了
    }

    spdlog::warn("Model fallback triggered: switching from {} to {}",
        client_->getModelName(), fallbackModel_);

    // 切换模型
    client_->setModel(fallbackModel_);

    // 如果配置了跨 provider 回退
    if (!fallbackBaseUrl_.empty()) {
        client_->setBaseUrl(fallbackBaseUrl_);
    }
    if (!fallbackApiKey_.empty()) {
        client_->setApiKey(fallbackApiKey_);
    }

    fallbackActive_ = true;
    return true;
}

Json RetryableClient::stripThinkingBlocks(const Json& messages) {
    // Strip thinking/signature/redacted_thinking blocks
    // Different providers are incompatible with each other's extended thinking blocks
    if (!messages.is_array()) return messages;

    Json stripped = Json::array();
    for (const auto& msg : messages) {
        Json m = msg;

        // Remove top-level thinking/signature fields
        m.erase("thinking");
        m.erase("signature");

        // Remove thinking-related content blocks (Anthropic format)
        if (m.contains("content") && m["content"].is_array()) {
            Json newContent = Json::array();
            for (const auto& block : m["content"]) {
                String type = block.value("type", "");
                if (type == "thinking" || type == "redacted_thinking" || type == "signature") {
                    continue;  // Strip all thinking-related block types
                }
                newContent.push_back(block);
            }
            m["content"] = newContent;
        }

        stripped.push_back(m);
    }
    return stripped;
}

} // namespace claude
