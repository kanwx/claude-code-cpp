#pragma once

#include "ApiClient.hpp"
#include "../core/Types.hpp"
#include "../utils/CircuitBreaker.hpp"
#include <memory>
#include <atomic>

namespace httplib { class Client; }

namespace claude {

/// OpenAI 兼容 API 客户端
class OpenAIClient : public ApiClient {
public:
    OpenAIClient();
    explicit OpenAIClient(const String& apiKey);
    ~OpenAIClient();

    // ========== 配置 ==========

    void setApiKey(const String& key) override;
    void setBaseUrl(const String& url) override;
    void setModel(const String& model) override;
    void setMaxTokens(int maxTokens) override;
    void setTemperature(double temp) override;
    void setStreamTimeout(int seconds);

    // ========== 断路器配置 ==========

    void setCircuitBreakerConfig(const CircuitBreakerConfig& config);
    Json getCircuitBreakerStats() const;
    void resetCircuitBreaker();

    // ========== 调用 ==========

    std::expected<Json, String> call(
        const Json& messages,
        const Json& tools
    ) override;

    void stream(
        const Json& messages,
        const Json& tools,
        std::function<void(const Json& chunk)> onChunk
    ) override;

    // ========== 中断 ==========

    void abort() override { aborted_.store(true, std::memory_order_release); }
    bool isAborted() const override { return aborted_.load(std::memory_order_acquire); }
    void resetAbort() override { aborted_.store(false, std::memory_order_release); }

    // ========== 信息 ==========

    String getProviderName() const override { return "openai"; }
    String getModelName() const override { return model_; }

    // 请求构建 (public — pure JSON transform, testable)
    Json buildRequest(const Json& messages, const Json& tools);

private:
    String apiKey_;
    String baseUrl_ = "https://api.openai.com/v1";
    String model_ = "gpt-4o";
    int maxTokens_ = 16384;
    int streamTimeoutSeconds_ = 600;  // Wall-clock total timeout for streaming
    double temperature_ = 1.0;

    std::unique_ptr<httplib::Client> httpClient_;
    CircuitBreaker circuitBreaker_;
    std::atomic<bool> aborted_{false};
};

} // namespace claude
