#pragma once


#include "../core/Types.hpp"
#include "../core/PromptTooLongException.hpp"

#include "ApiClient.hpp"
#include "RetryPolicy.hpp"
#include "../utils/CircuitBreaker.hpp"
#include "../core/Cache.hpp"
#include <memory>
#include <atomic>

namespace claude {

/// Exception thrown when model fallback is triggered.
/// AgentLoop catches this to inject tombstone messages and system warnings.
class FallbackTriggered : public std::runtime_error {
public:
    FallbackTriggered(const String& fromModel, const String& toModel, const Json& strippedMessages)
        : std::runtime_error("Model fallback triggered: " + fromModel + " -> " + toModel)
        , fromModel(fromModel)
        , toModel(toModel)
        , strippedMessages(strippedMessages) {}

    String fromModel;
    String toModel;
    Json strippedMessages;  // Messages after stripping thinking/signature blocks
};

/// 带重试功能和模型回退的 API 客户端包装器
///
/// 模型回退行为 (匹配原版 TS):
/// - 跟踪连续 529/overload 错误
/// - 连续 2 次 529 → 切换到回退模型
/// - 切换时剥离 thinking/signature blocks
/// - 成功响应后重置计数器
class RetryableClient {
public:
    /// 创建重试客户端
    explicit RetryableClient(std::unique_ptr<ApiClient> client)
        : client_(std::move(client)) {}

    /// 设置重试策略
    void setRetryPolicy(const RetryPolicy& policy) {
        policy_ = policy;
    }

    /// 设置重试配置
    void setRetryConfig(const RetryConfig& config) {
        policy_.setConfig(config);
    }

    /// 获取底层客户端
    ApiClient* client() { return client_.get(); }
    const ApiClient* client() const { return client_.get(); }

    // ========== 配置代理 ==========

    void setApiKey(const String& key) { client_->setApiKey(key); }
    void setBaseUrl(const String& url) { client_->setBaseUrl(url); }
    void setModel(const String& model) { client_->setModel(model); }
    void setMaxTokens(int maxTokens) { client_->setMaxTokens(maxTokens); }

    // ========== 模型回退 ==========

    /// 设置回退模型 (连续 529 时切换)
    void setFallbackModel(const String& model) { fallbackModel_ = model; }

    /// 设置回退 base URL (可选，跨 provider 回退)
    void setFallbackBaseUrl(const String& url) { fallbackBaseUrl_ = url; }

    /// 设置回退 API key (可选，跨 provider 回退)
    void setFallbackApiKey(const String& key) { fallbackApiKey_ = key; }

    /// 是否已回退到备用模型
    bool isFallbackActive() const { return fallbackActive_; }

    // ========== 带重试的调用 ==========

    /// 带重试的阻塞调用
    /// @param messages 消息列表
    /// @param tools 工具列表
    /// @param onRetry 重试回调 (可选)
    /// @return 结果或错误
    std::expected<Json, String> callWithRetry(
        const Json& messages,
        const Json& tools,
        std::function<void(const ApiError& error, int attempt, int delayMs)> onRetry = nullptr
    );

    /// 带重试的流式调用
    /// @param messages 消息列表
    /// @param tools 工具列表
    /// @param onChunk 数据块回调
    /// @param onRetry 重试回调 (可选)
    void streamWithRetry(
        const Json& messages,
        const Json& tools,
        std::function<void(const Json& chunk)> onChunk,
        std::function<void(const ApiError& error, int attempt, int delayMs)> onRetry = nullptr
    );

    /// Strip thinking/signature/redacted_thinking blocks from messages.
    /// Used before model fallback to prevent cross-provider incompatibility.
    static Json stripThinkingBlocks(const Json& messages);

    /// Number of consecutive 529/overload errors before triggering fallback.
    static constexpr int OVERLOAD_THRESHOLD = 3;  // Match TS MAX_529_RETRIES

    /// Information about a model fallback event.
    struct FallbackInfo {
        String fromModel;
        String toModel;
        String fromBaseUrl;
        String toBaseUrl;
    };

    /// Callback type for fallback events.
    using OnFallback = std::function<void(const FallbackInfo&)>;

    /// Set the fallback event callback.
    /// Called when the client switches to the fallback model.
    void setOnFallback(OnFallback callback) {
        onFallback_ = std::move(callback);
    }

    // ========== Unattended/Persistent Retry Mode ==========

    /// Set keep-alive callback for unattended mode.
    /// Called periodically during persistent retries to inform the user
    /// that the client is still waiting.
    void setOnKeepAlive(std::function<void(const String& message)> callback) {
        onKeepAlive_ = std::move(callback);
    }

    /// Enable or disable unattended mode.
    /// In unattended mode, 429/529 errors trigger persistent retry
    /// instead of giving up after max retries.
    void setUnattendedMode(bool enabled) {
        policy_.config().unattended = enabled;
    }

    bool isUnattendedMode() const {
        return policy_.config().unattended;
    }

    // ========== Circuit Breaker ==========

    /// Get the circuit breaker instance
    CircuitBreaker& circuitBreaker() { return *circuitBreaker_; }
    const CircuitBreaker& circuitBreaker() const { return *circuitBreaker_; }

    /// Set circuit breaker config
    void setCircuitBreakerConfig(const CircuitBreakerConfig& config) {
        circuitBreaker_ = std::make_unique<CircuitBreaker>(config);
    }

    // ========== Response Cache ==========

    /// Enable or disable response caching for non-streaming calls.
    /// Cached responses are keyed by model + message hash.
    /// Cache is NOT used for streaming calls or tool-use responses.
    void setCacheEnabled(bool enabled) { cacheEnabled_ = enabled; }
    bool isCacheEnabled() const { return cacheEnabled_; }

    /// Get the API cache instance
    ApiCache& responseCache() { return *apiCache_; }

    /// Invalidate all cached responses
    void invalidateCache() { apiCache_ = std::make_unique<ApiCache>(); }

    // ========== 直接代理 ==========

    std::expected<Json, String> call(const Json& messages, const Json& tools) {
        return client_->call(messages, tools);
    }

    void stream(const Json& messages, const Json& tools,
                std::function<void(const Json& chunk)> onChunk) {
        client_->stream(messages, tools, onChunk);
    }

    String getProviderName() const { return client_->getProviderName(); }
    String getModelName() const { return client_->getModelName(); }

private:
    /// 尝试回退到备用模型
    /// 返回 true 如果成功切换
    bool attemptFallback();

    /// Check if an HTTP status code is retryable (rate-limit / server overload).
    /// Used by unattended mode to decide whether to keep retrying indefinitely.
    static bool isRetryableStatus(int status);

    std::unique_ptr<ApiClient> client_;
    RetryPolicy policy_;
    std::unique_ptr<CircuitBreaker> circuitBreaker_{std::make_unique<CircuitBreaker>()};
    std::unique_ptr<ApiCache> apiCache_{std::make_unique<ApiCache>()};
    bool cacheEnabled_ = false;

    // 回退配置
    String fallbackModel_;
    String fallbackBaseUrl_;
    String fallbackApiKey_;
    std::atomic<bool> fallbackActive_{false};
    std::atomic<int> consecutiveOverloadErrors_{0};
    OnFallback onFallback_;
    std::function<void(const String&)> onKeepAlive_;
    String lastFallbackFromModel_;  // Set by attemptFallback
};

} // namespace claude
