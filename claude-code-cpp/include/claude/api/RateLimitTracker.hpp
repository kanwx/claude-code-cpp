#pragma once

#include "../core/Types.hpp"
#include <httplib.h>
#include <chrono>
#include <mutex>
#include <spdlog/spdlog.h>

namespace claude {

/// API 速率限制信息 —— 从响应头提取
struct RateLimitInfo {
    // 请求限制
    int requestsLimit = 0;          // anthropic-ratelimit-unified-limit
    int requestsRemaining = 0;      // anthropic-ratelimit-unified-remaining
    std::chrono::system_clock::time_point requestsResetTime;  // anthropic-ratelimit-unified-reset

    // Token 限制
    int tokensLimit = 0;            // anthropic-ratelimit-tokens-limit
    int tokensRemaining = 0;        // anthropic-ratelimit-tokens-remaining
    std::chrono::system_clock::time_point tokensResetTime;    // anthropic-ratelimit-tokens-reset

    // 重试信息
    int retryAfter = 0;             // retry-after (秒)
    bool isOverloaded = false;      // 529/429 状态码

    // 提取时间
    std::chrono::system_clock::time_point lastUpdated;

    /// 从响应头解析速率限制信息
    static RateLimitInfo fromHeaders(const std::map<String, String>& headers);

    /// 是否接近请求限制 (剩余 < 10%)
    bool isRequestLimitLow() const {
        return requestsLimit > 0 &&
               requestsRemaining < requestsLimit / 10;
    }

    /// 是否接近 Token 限制 (剩余 < 10%)
    bool isTokenLimitLow() const {
        return tokensLimit > 0 &&
               tokensRemaining < tokensLimit / 10;
    }

    /// 是否已超出请求限制
    bool isRequestLimitExceeded() const {
        return requestsLimit > 0 && requestsRemaining <= 0;
    }

    /// 是否已超出 Token 限制
    bool isTokenLimitExceeded() const {
        return tokensLimit > 0 && tokensRemaining <= 0;
    }

    /// 生成速率限制警告消息
    String warningMessage() const;
};

/// 速率限制追踪器 —— 追踪 API 使用情况和配额
///
/// 功能：
/// - 从每次 API 响应更新速率限制信息
/// - 实时显示使用量警告
/// - 生成速率限制消息供 UI 展示
class RateLimitTracker {
public:
    RateLimitTracker() = default;

    // ========== 更新 ==========

    /// 从 API 响应头更新速率限制信息
    void updateFromHeaders(const std::map<String, String>& headers);

    /// Update from httplib::Headers (adapter — converts to map then delegates)
    void updateFromHttpHeaders(const httplib::Headers& headers);

    /// 记录速率限制错误 (429/529)
    void recordRateLimitError(int statusCode, const String& body = "");

    /// 记录成功请求
    void recordSuccess();

    // ========== 查询 ==========

    /// 获取速率限制信息副本
    RateLimitInfo getInfo() const {
        std::lock_guard lock(mutex_);
        return info_;
    }

    /// 获取速率限制信息
    const RateLimitInfo& info() const {
        return info_;
    }

    /// 是否应该显示警告
    bool shouldShowWarning() const;

    /// 生成使用量摘要
    String usageSummary() const;

    /// Get a human-readable rate limit status message.
    /// Returns empty string if no warning is needed.
    String statusMessage() const;

    /// 连续错误次数
    int consecutiveErrors() const { return consecutiveErrors_; }

    /// 总请求数
    int totalRequests() const { return totalRequests_; }

    /// 总错误数
    int totalErrors() const { return totalErrors_; }

    // ========== 重置 ==========

    void reset() {
        std::lock_guard lock(mutex_);
        info_ = {};
        consecutiveErrors_ = 0;
        totalRequests_ = 0;
        totalErrors_ = 0;
        lastWarningShown_ = {};
    }

private:
    mutable std::mutex mutex_;
    RateLimitInfo info_;
    int consecutiveErrors_ = 0;
    int totalRequests_ = 0;
    int totalErrors_ = 0;
    std::chrono::steady_clock::time_point lastWarningShown_;

    static constexpr auto WARNING_COOLDOWN = std::chrono::minutes(5);
};

} // namespace claude
