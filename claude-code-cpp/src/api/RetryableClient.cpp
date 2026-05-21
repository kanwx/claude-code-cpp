#include <claude/api/RetryableClient.hpp>
#include <claude/services/OAuthService.hpp>
#include <spdlog/spdlog.h>

namespace claude {

bool RetryableClient::isRetryableStatus(int status) {
    return status == 429 || status == 503 || status == 529;
}

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
                auto stripped = stripThinkingBlocks(messages);
                throw FallbackTriggered(
                    lastFallbackFromModel_,
                    client_->getModelName(),
                    stripped
                );
            }
        } else {
            // 非 overload 错误 → 重置计数器
            consecutiveOverloadErrors_ = 0;
        }

        // ========== 413 Prompt Too Long — surface as structured exception (no retry) ==========
        if (statusCode == 413 || errorMsg.find("prompt-too-long") != String::npos ||
            errorMsg.find("context_length_exceeded") != String::npos) {
            long actualTokens = 0, maxTokens = 0;
            auto gtPos = errorMsg.find(" tokens > ");
            if (gtPos != String::npos) {
                try {
                    auto numEnd = errorMsg.rfind(' ', gtPos - 1);
                    if (numEnd == String::npos) numEnd = 0; else numEnd++;
                    actualTokens = std::stol(errorMsg.substr(numEnd, gtPos - numEnd));
                    auto maxStart = gtPos + 11;
                    auto maxEnd = errorMsg.find(' ', maxStart);
                    maxTokens = std::stol(errorMsg.substr(maxStart, maxEnd - maxStart));
                } catch (...) {}
            }
            throw PromptTooLongException(actualTokens, maxTokens);
        }

        // ========== 401 Unauthorized — try refreshing the OAuth token before giving up ==========
        if (error.type == ApiErrorType::AuthError && attempt == 1) {
            try {
                auto& oauthManager = oauth::OAuthManager::instance();
                auto& anthropicClient = oauthManager.getClient("anthropic");
                if (anthropicClient.isAuthenticated()) {
                    spdlog::info("401 received, attempting OAuth token refresh");
                    bool refreshed = anthropicClient.ensureValidToken();
                    if (refreshed) {
                        auto tokenOpt = anthropicClient.getCurrentToken();
                        if (tokenOpt) {
                            client_->setApiKey(tokenOpt->accessToken);
                            spdlog::info("OAuth token refreshed, retrying with new token");
                            continue;
                        }
                    }
                    spdlog::warn("OAuth token refresh failed on 401");
                }
            } catch (const std::exception& e) {
                spdlog::warn("OAuth token refresh attempt failed: {}", e.what());
            }
        }

        // 检查是否应该重试
        if (!policy_.shouldRetry(error, attempt)) {
            // In unattended mode, keep retrying indefinitely on rate-limit / server overload
            if (policy_.config().unattended && isRetryableStatus(statusCode)) {
                if (onKeepAlive_) {
                    onKeepAlive_("Still waiting for API availability... (attempt " +
                        std::to_string(attempt + 1) + ")");
                }
                spdlog::info("Unattended mode: persistent retry on status {}", statusCode);
                // Fall through to delay calculation and continue retrying
            } else {
                SPDLOG_ERROR("API call failed (attempt {}): {} - {}",
                            attempt, static_cast<int>(error.type), errorMsg);
                return std::unexpected(errorMsg);
            }
        }

        // 计算延迟
        int delayMs = policy_.calculateDelay(error, attempt);

        SPDLOG_WARN("API call failed, retrying in {}ms (attempt {}{}): {}",
                   delayMs, attempt,
                   policy_.config().unattended ? " [unattended]" :
                   "/" + std::to_string(policy_.config().maxRetries),
                   errorMsg);

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
                auto stripped = stripThinkingBlocks(messages);
                throw FallbackTriggered(
                    lastFallbackFromModel_,
                    client_->getModelName(),
                    stripped
                );
            }
        } else {
            consecutiveOverloadErrors_ = 0;
        }

        // ========== 413 Prompt Too Long — surface as structured exception (no retry) ==========
        if (errorMsg.find("413") != String::npos ||
            errorMsg.find("prompt-too-long") != String::npos ||
            errorMsg.find("context_length_exceeded") != String::npos) {
            long actualTokens = 0, maxTokens = 0;
            auto gtPos = errorMsg.find(" tokens > ");
            if (gtPos != String::npos) {
                try {
                    auto numEnd = errorMsg.rfind(' ', gtPos - 1);
                    if (numEnd == String::npos) numEnd = 0; else numEnd++;
                    actualTokens = std::stol(errorMsg.substr(numEnd, gtPos - numEnd));
                    auto maxStart = gtPos + 11;
                    auto maxEnd = errorMsg.find(' ', maxStart);
                    maxTokens = std::stol(errorMsg.substr(maxStart, maxEnd - maxStart));
                } catch (...) {}
            }
            throw PromptTooLongException(actualTokens, maxTokens);
        }

        // ========== 401 Unauthorized — try refreshing the OAuth token before giving up ==========
        if ((error.type == ApiErrorType::AuthError || errorMsg.find("401") != String::npos) && attempt == 1) {
            try {
                auto& oauthManager = oauth::OAuthManager::instance();
                auto& anthropicClient = oauthManager.getClient("anthropic");
                if (anthropicClient.isAuthenticated()) {
                    spdlog::info("401 received on stream, attempting OAuth token refresh");
                    bool refreshed = anthropicClient.ensureValidToken();
                    if (refreshed) {
                        auto tokenOpt = anthropicClient.getCurrentToken();
                        if (tokenOpt) {
                            client_->setApiKey(tokenOpt->accessToken);
                            spdlog::info("OAuth token refreshed, retrying stream with new token");
                            continue;
                        }
                    }
                    spdlog::warn("OAuth token refresh failed on 401 stream error");
                }
            } catch (const std::exception& e) {
                spdlog::warn("OAuth token refresh attempt failed: {}", e.what());
            }
        }

        if (!policy_.shouldRetry(error, attempt)) {
            // In unattended mode, keep retrying indefinitely on rate-limit / server overload
            // Extract status code from error message for isRetryableStatus check
            int streamStatus = 0;
            if (errorMsg.find("429") != String::npos) streamStatus = 429;
            else if (errorMsg.find("503") != String::npos) streamStatus = 503;
            else if (errorMsg.find("529") != String::npos) streamStatus = 529;

            if (policy_.config().unattended && isRetryableStatus(streamStatus)) {
                if (onKeepAlive_) {
                    onKeepAlive_("Still waiting for API availability... (attempt " +
                        std::to_string(attempt + 1) + ")");
                }
                spdlog::info("Unattended mode: persistent retry on status {}", streamStatus);
                // Fall through to delay calculation and continue retrying
            } else {
                SPDLOG_ERROR("Stream failed (attempt {}): {}", attempt, errorMsg);
                throw std::runtime_error(errorMsg);
            }
        }

        int delayMs = policy_.calculateDelay(error, attempt);
        SPDLOG_WARN("Stream failed, retrying in {}ms (attempt {}{}): {}",
                   delayMs, attempt,
                   policy_.config().unattended ? " [unattended]" :
                   "/" + std::to_string(policy_.config().maxRetries),
                   errorMsg);

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

    // Capture previous values before switching
    String previousModel = client_->getModelName();
    lastFallbackFromModel_ = previousModel;
    String previousBaseUrl;  // No getter for base URL currently

    spdlog::warn("Model fallback triggered: switching from {} to {}",
        previousModel, fallbackModel_);

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

    // Notify callback about the fallback
    if (onFallback_) {
        FallbackInfo info;
        info.fromModel = previousModel;
        info.toModel = fallbackModel_;
        info.fromBaseUrl = previousBaseUrl;
        info.toBaseUrl = fallbackBaseUrl_;
        onFallback_(info);
    }

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
