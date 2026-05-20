#pragma once


#include "../core/Types.hpp"

#include "ApiClient.hpp"
#include "RetryPolicy.hpp"
#include <memory>
#include <atomic>

namespace claude {

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

    std::unique_ptr<ApiClient> client_;
    RetryPolicy policy_;

    // 回退配置
    String fallbackModel_;
    String fallbackBaseUrl_;
    String fallbackApiKey_;
    std::atomic<bool> fallbackActive_{false};
    std::atomic<int> consecutiveOverloadErrors_{0};

    static constexpr int OVERLOAD_THRESHOLD = 2;  // 连续 2 次 529 → 回退
};

} // namespace claude
