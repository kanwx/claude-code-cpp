#pragma once

#include "../core/Types.hpp"
#include <functional>

namespace claude {

/// 错误类型分类
enum class ApiErrorType {
    None,               // 无错误
    NetworkError,       // 网络错误 (可重试)
    Timeout,            // 超时 (可重试)
    RateLimit,          // Rate limit (等待后重试)
    ServerError,        // 服务器错误 5xx (可重试)
    ClientError,        // 客户端错误 4xx (不可重试)
    AuthError,          // 认证错误 (不可重试)
    InvalidRequest,     // 无效请求 (不可重试)
    ContentFiltered,    // 内容过滤 (不可重试)
    PromptTooLong,      // 413 - context window exceeded
    Unknown             // 未知错误
};

/// API 错误信息
struct ApiError {
    ApiErrorType type = ApiErrorType::None;
    int statusCode = 0;           // HTTP 状态码
    String message;               // 错误消息
    int retryAfter = 0;           // Retry-After 头 (秒)
    long tokenGap = 0;           // Tokens over the limit for 413 errors

    /// 是否可重试
    bool isRetryable() const {
        return type == ApiErrorType::NetworkError ||
               type == ApiErrorType::Timeout ||
               type == ApiErrorType::RateLimit ||
               type == ApiErrorType::ServerError;
    }

    /// 获取建议等待时间 (毫秒)
    int getSuggestedWaitMs() const {
        if (type == ApiErrorType::RateLimit) {
            return retryAfter > 0 ? retryAfter * 1000 : 60000; // 默认 60s
        }
        return 0;
    }
};

/// 重试策略配置
struct RetryConfig {
    int maxRetries = 3;                    // 最大重试次数
    int initialDelayMs = 1000;            // 初始延迟 (毫秒)
    int maxDelayMs = 30000;               // 最大延迟 (毫秒)
    double backoffMultiplier = 2.0;       // 退避乘数
    double jitterFactor = 0.1;            // 抖动因子 (0-1)

    // 针对不同错误类型的配置
    int rateLimitInitialDelayMs = 1000;   // Rate limit 初始延迟
    int networkErrorInitialDelayMs = 500; // 网络错误初始延迟
};

/// 重试策略
class RetryPolicy {
public:
    using ShouldRetry = std::function<bool(const ApiError& error, int attempt)>;

    RetryPolicy() = default;
    explicit RetryPolicy(const RetryConfig& config) : config_(config) {}

    /// 设置配置
    void setConfig(const RetryConfig& config) { config_ = config; }

    /// 获取配置
    const RetryConfig& config() const { return config_; }

    /// 计算下次重试的等待时间 (毫秒)
    /// @param error 错误信息
    /// @param attempt 当前尝试次数 (从 1 开始)
    int calculateDelay(const ApiError& error, int attempt) const;

    /// 判断是否应该重试
    /// @param error 错误信息
    /// @param attempt 当前尝试次数
    bool shouldRetry(const ApiError& error, int attempt) const;

    /// 分类错误
    static ApiError classifyError(int statusCode, const String& body);

    /// 分类网络错误
    static ApiError classifyNetworkError(const String& errorMsg);

    /// 睡眠指定毫秒
    static void sleepMs(int ms);

private:
    RetryConfig config_;

    /// 计算指数退避延迟
    int calculateBackoffDelay(int attempt, int baseDelayMs) const;

    /// 添加抖动
    int addJitter(int delayMs) const;
};

} // namespace claude
