#include <claude/api/RetryPolicy.hpp>
#include <thread>
#include <random>
#include <algorithm>

namespace claude {

// ============================================================================
// 延迟计算
// ============================================================================

int RetryPolicy::calculateDelay(const ApiError& error, int attempt) const {
    // Rate limit: 使用 Retry-After 或固定延迟
    if (error.type == ApiErrorType::RateLimit) {
        if (error.retryAfter > 0) {
            return error.retryAfter * 1000;
        }
        return calculateBackoffDelay(attempt, config_.rateLimitInitialDelayMs);
    }

    // 网络错误: 使用较短的基础延迟
    if (error.type == ApiErrorType::NetworkError ||
        error.type == ApiErrorType::Timeout) {
        return calculateBackoffDelay(attempt, config_.networkErrorInitialDelayMs);
    }

    // 服务器错误: 使用默认延迟
    return calculateBackoffDelay(attempt, config_.initialDelayMs);
}

bool RetryPolicy::shouldRetry(const ApiError& error, int attempt) const {
    // 超过最大重试次数
    if (attempt >= config_.maxRetries) {
        return false;
    }

    // 根据错误类型判断
    return error.isRetryable();
}

// ============================================================================
// 错误分类
// ============================================================================

ApiError RetryPolicy::classifyError(int statusCode, const String& body) {
    ApiError error;
    error.statusCode = statusCode;
    error.message = body;

    // 解析 Retry-After (简化处理，从 body 中提取)
    // 实际应从响应头获取

    switch (statusCode) {
        case 200:
        case 201:
        case 202:
            error.type = ApiErrorType::None;
            break;

        case 400:
            error.type = ApiErrorType::InvalidRequest;
            // 检查是否是内容过滤
            if (body.find("content_filter") != String::npos ||
                body.find("content policy") != String::npos) {
                error.type = ApiErrorType::ContentFiltered;
            }
            break;

        case 401:
            error.type = ApiErrorType::AuthError;
            break;

        case 403:
            error.type = ApiErrorType::AuthError;
            break;

        case 404:
            error.type = ApiErrorType::ClientError;
            break;

        case 429:
            error.type = ApiErrorType::RateLimit;
            // 尝试从 body 解析 retry-after
            // Anthropic: "error": {"type": "error", "message": "..."}
            // OpenAI: "error": {"message": "...", "type": "rate_limit_exceeded"}
            {
                auto pos = body.find("retry_after");
                if (pos != String::npos) {
                    try {
                        auto start = body.find(':', pos);
                        if (start != String::npos) {
                            error.retryAfter = std::stoi(body.substr(start + 1));
                        }
                    } catch (...) {}
                }
            }
            break;

        case 500:
        case 502:
        case 503:
        case 504:
            error.type = ApiErrorType::ServerError;
            // 503 可能包含 Retry-After
            if (statusCode == 503) {
                auto pos = body.find("retry_after");
                if (pos != String::npos) {
                    try {
                        auto start = body.find(':', pos);
                        if (start != String::npos) {
                            error.retryAfter = std::stoi(body.substr(start + 1));
                        }
                    } catch (...) {}
                }
            }
            break;

        case 413: {
            error.type = ApiErrorType::PromptTooLong;
            // Try to extract token counts from error body
            auto gtPos = body.find(" tokens > ");
            if (gtPos != String::npos) {
                try {
                    auto numEnd = body.rfind(' ', gtPos - 1);
                    if (numEnd == String::npos) numEnd = 0;
                    else numEnd++;
                    long actualTokens = std::stol(body.substr(numEnd, gtPos - numEnd));
                    auto maxStart = gtPos + 11;
                    auto maxEnd = body.find(' ', maxStart);
                    long maxTokens = std::stol(body.substr(maxStart, maxEnd - maxStart));
                    error.tokenGap = actualTokens - maxTokens;
                } catch (...) {}
            }
            error.message = body;
            break;
        }

        default:
            if (statusCode >= 400 && statusCode < 500) {
                error.type = ApiErrorType::ClientError;
            } else if (statusCode >= 500) {
                error.type = ApiErrorType::ServerError;
            } else {
                error.type = ApiErrorType::Unknown;
            }
    }

    return error;
}

ApiError RetryPolicy::classifyNetworkError(const String& errorMsg) {
    ApiError error;
    error.message = errorMsg;

    // 根据错误消息判断类型
    if (errorMsg.find("timeout") != String::npos ||
        errorMsg.find("Timeout") != String::npos ||
        errorMsg.find("timed out") != String::npos) {
        error.type = ApiErrorType::Timeout;
    }
    else if (errorMsg.find("connection") != String::npos ||
             errorMsg.find("Connection") != String::npos ||
             errorMsg.find("network") != String::npos ||
             errorMsg.find("Network") != String::npos ||
             errorMsg.find("socket") != String::npos) {
        error.type = ApiErrorType::NetworkError;
    }
    else {
        error.type = ApiErrorType::NetworkError; // 默认网络错误
    }

    return error;
}

// ============================================================================
// 辅助方法
// ============================================================================

int RetryPolicy::calculateBackoffDelay(int attempt, int baseDelayMs) const {
    // 指数退避: base * multiplier^(attempt-1)
    double delay = baseDelayMs;
    for (int i = 1; i < attempt; ++i) {
        delay *= config_.backoffMultiplier;
    }

    // 限制最大延迟
    delay = std::min(delay, static_cast<double>(config_.maxDelayMs));

    // 添加抖动
    return addJitter(static_cast<int>(delay));
}

int RetryPolicy::addJitter(int delayMs) const {
    if (config_.jitterFactor <= 0) {
        return delayMs;
    }

    // 随机抖动: delay * (1 - jitter) 到 delay * (1 + jitter)
    static std::random_device rd;
    static std::mt19937 gen(rd());

    double minFactor = 1.0 - config_.jitterFactor;
    double maxFactor = 1.0 + config_.jitterFactor;

    std::uniform_real_distribution<> dist(minFactor, maxFactor);
    return static_cast<int>(delayMs * dist(gen));
}

void RetryPolicy::sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace claude
