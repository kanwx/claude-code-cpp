#pragma once

#include "ApiClient.hpp"
#include "RateLimitTracker.hpp"
#include "../core/Types.hpp"
#include "../stream/TypedStreamEvent.hpp"
#include "../utils/CircuitBreaker.hpp"
#include "../core/Cache.hpp"
#include <httplib.h>
#include <atomic>
#include <memory>
#include <chrono>
#include <shared_mutex>

namespace claude {

/// Protocol used to communicate with the API endpoint.
///
/// Determines where the system prompt is placed in the request:
///   Anthropic:         top-level "system" field, removed from messages
///   OpenAICompatible:  first message in the "messages" array (role:"system")
///
/// Derived automatically from base URL (setBaseUrl), but callers may
/// override via setApiProtocol() for Anthropic-compatible proxies that
/// use a custom base URL.
enum class ApiProtocol {
    OpenAICompatible,  // default for custom base URLs (DeepSeek, etc.)
    Anthropic,         // official api.anthropic.com
};

/// Streaming response accumulator state
struct StreamingState {
    // Content block accumulation
    Json contentBlocks = Json::array();
    int currentBlockIndex = -1;
    String currentBlockType;       // "text", "tool_use", "server_tool_use", "thinking"
    String currentBlockId;         // tool_use id
    String currentBlockName;       // tool_use name
    String accumulatedText;        // text/thinking content
    String accumulatedInputJson;   // tool_use input_json delta accumulation

    // Usage tracking (from message_start + message_delta)
    Usage usage;
    String model;                  // from message_start
    String stopReason;             // from message_delta

    // Fallback tracking
    bool didFallBackToNonStreaming = false;

    // Stream idle watchdog
    std::chrono::steady_clock::time_point lastChunkTime;
    int stallCount = 0;
    double totalStallTimeMs = 0.0;

    /// Reset state for a new streaming request
    void reset() {
        contentBlocks = Json::array();
        currentBlockIndex = -1;
        currentBlockType.clear();
        currentBlockId.clear();
        currentBlockName.clear();
        accumulatedText.clear();
        accumulatedInputJson.clear();
        usage = Usage{};
        model.clear();
        stopReason.clear();
        didFallBackToNonStreaming = false;
        lastChunkTime = std::chrono::steady_clock::now();
        stallCount = 0;
        totalStallTimeMs = 0.0;
    }

    /// Check if there are partially-completed content blocks (stream interrupted)
    bool hasPartialBlocks() const {
        return currentBlockIndex >= 0 && !currentBlockType.empty();
    }

    /// Finalize any partially-completed blocks (e.g., on stream abort).
    /// Partial tool_use blocks get their accumulated input_json as-is;
    /// partial text/thinking/redacted_thinking blocks get their accumulated text.
    void finalizePartialBlocks() {
        if (!hasPartialBlocks()) return;

        Json partialBlock;
        if (currentBlockType == "tool_use" || currentBlockType == "server_tool_use") {
            Json inputJson;
            try {
                inputJson = Json::parse(accumulatedInputJson);
            } catch (...) {
                inputJson = Json::object();
            }
            partialBlock = {
                {"type", currentBlockType},
                {"id", currentBlockId},
                {"name", currentBlockName},
                {"input", inputJson},
                {"partial", true}
            };
        } else if (currentBlockType == "thinking") {
            partialBlock = {
                {"type", "thinking"},
                {"thinking", accumulatedText},
                {"partial", true}
            };
        } else if (currentBlockType == "redacted_thinking") {
            partialBlock = {
                {"type", "redacted_thinking"},
                {"data", accumulatedText},
                {"partial", true}
            };
        } else if (currentBlockType == "text") {
            partialBlock = {
                {"type", "text"},
                {"text", accumulatedText}
            };
        } else {
            partialBlock = {
                {"type", currentBlockType},
                {"partial", true}
            };
        }

        while (static_cast<int>(contentBlocks.size()) <= currentBlockIndex) {
            contentBlocks.push_back(Json::object());
        }
        contentBlocks[currentBlockIndex] = partialBlock;

        currentBlockIndex = -1;
        currentBlockType.clear();
        currentBlockId.clear();
        currentBlockName.clear();
        accumulatedText.clear();
        accumulatedInputJson.clear();
    }
};

/// Quota status extracted from response headers
struct QuotaStatus {
    bool isFreeTier = false;
    long requestsLimit = 0;
    long requestsRemaining = 0;
    long tokensLimit = 0;
    long tokensRemaining = 0;
};

/// Anthropic API 客户端
class AnthropicClient : public ApiClient {
public:
    AnthropicClient();
    explicit AnthropicClient(const String& apiKey);
    ~AnthropicClient();

    // ========== 配置 ==========

    void setApiKey(const String& key) override;
    void setBaseUrl(const String& url) override;
    void setModel(const String& model) override;
    void setMaxTokens(int maxTokens) override;
    void setTemperature(double temp) override;

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

    /// Enhanced streaming with full state accumulation and fallback.
    /// Returns the accumulated StreamingState on success.
    Result<StreamingState> streamWithState(
        const Json& messages,
        const Json& tools,
        std::function<void(const Json& chunk)> onChunk = nullptr
    );

    // ========== Typed streaming ==========

    /// Callback invoked for each typed stream event during streaming.
    /// Set this before calling streamWithState() to receive TypedStreamEvent
    /// emission alongside the raw onChunk callback.
    std::function<void(TypedStreamEvent&&)> onTypedEvent;

    /// Convert a non-streaming API response into a sequence of TypedStreamEvents.
    /// Used by the fallback path to emit typed events from a non-streaming response.
    static std::vector<TypedStreamEvent> convertNonStreamingResponse(const Json& response);

    // ========== 扩展功能 ==========

    /// 设置是否启用 extended thinking
    void setThinkingEnabled(bool enabled);

    /// 设置 thinking budget
    void setThinkingBudget(int budget);

    /// 设置是否启用 prompt caching
    void setPromptCachingEnabled(bool enabled) {
        std::unique_lock lock(configMutex_);
        promptCachingEnabled_ = enabled;
    }

    /// Explicitly set API protocol (overrides base-URL inference).
    ///
    /// Use ApiProtocol::Anthropic for Anthropic-compatible proxies that use
    /// a custom base URL but expect the system prompt at the top-level
    /// "system" field. The default (derived from setBaseUrl) is
    /// Anthropic for api.anthropic.com, OpenAICompatible otherwise.
    void setApiProtocol(ApiProtocol proto) {
        std::unique_lock lock(configMutex_);
        apiProtocol_ = proto;
        apiProtocolExplicit_ = true;
    }

    ApiProtocol getApiProtocol() const {
        std::shared_lock lock(configMutex_);
        return apiProtocol_;
    }

    /// 设置查询来源 (影响缓存 TTL)
    void setQuerySource(const String& source) {
        std::unique_lock lock(configMutex_);
        querySource_ = source;
    }

    // ========== Abort ==========

    void abort() override {
        aborted_.store(true, std::memory_order_release);
        if (httpClient_) httpClient_->stop();
    }
    bool isAborted() const override { return aborted_.load(std::memory_order_acquire); }
    void resetAbort() override { aborted_.store(false, std::memory_order_release); }

    /// Set stream idle timeout (seconds). 0 disables watchdog.
    void setStreamIdleTimeout(int seconds) {
        std::unique_lock lock(configMutex_);
        streamIdleTimeoutSec_ = seconds;
    }

    void setBetaHeaders(const std::vector<String>& betas) {
        std::unique_lock lock(configMutex_);
        betaHeaders_ = betas;
    }
    const std::vector<String>& getBetaHeaders() const {
        std::shared_lock lock(configMutex_);
        return betaHeaders_;
    }

    // ========== 信息 ==========

    bool isNativeSseParser() const override { return true; }

    String getProviderName() const override { return "anthropic"; }
    String getModelName() const override {
        std::shared_lock lock(configMutex_);
        return model_;
    }

    /// Get last quota status from response headers
    const QuotaStatus& lastQuotaStatus() const { return lastQuotaStatus_; }

    /// Get the rate limit tracker (updated on every API response)
    RateLimitTracker& rateLimitTracker() { return rateLimitTracker_; }
    const RateLimitTracker& rateLimitTracker() const { return rateLimitTracker_; }

    /// Circuit breaker access
    CircuitBreaker& circuitBreaker() { return circuitBreaker_; }
    const CircuitBreaker& circuitBreaker() const { return circuitBreaker_; }

    /// Enable/disable API response caching
    void setCacheEnabled(bool enabled) {
        std::unique_lock lock(configMutex_);
        cacheEnabled_ = enabled;
    }
    bool isCacheEnabled() const {
        std::shared_lock lock(configMutex_);
        return cacheEnabled_;
    }

    /// Whether the last stream() fell back to non-streaming
    bool didFallBackToNonStreaming() const {
        std::shared_lock lock(configMutex_);
        return lastDidFallBack_;
    }

    // 请求构建 (public — pure JSON transform, testable)
    Json buildRequest(const Json& messages, const Json& tools);

private:
    String apiKey_;
    String baseUrl_ = "https://api.anthropic.com/v1";
    String basePath_;  // path prefix extracted from baseUrl (e.g. "/anthropic"); cleared for official Anthropic
    String model_ = "claude-sonnet-4-20250514";
    int maxTokens_ = 16384;
    double temperature_ = -1;  // -1 = not set (use API default)
    bool thinkingEnabled_ = false;
    int thinkingBudget_ = 10000;
    bool promptCachingEnabled_ = true;  // 默认启用
    bool isCustomBaseUrl_ = false;      // 是否为自定义 base URL
    ApiProtocol apiProtocol_ = ApiProtocol::Anthropic;  // default base URL is api.anthropic.com
    bool apiProtocolExplicit_ = false;  // true if user called setApiProtocol()
    String querySource_;
    std::vector<String> betaHeaders_;

    std::shared_ptr<httplib::Client> httpClient_;
    String pooledBaseUrl_;  // tracks which baseUrl is checked out from ConnectionPool

    // Streaming fallback & watchdog config
    int streamIdleTimeoutSec_ = 30;     // seconds before stall detection
    bool lastDidFallBack_ = false;      // tracks last stream fallback
    std::atomic<bool> aborted_{false};  // stream abort flag

    // Quota tracking
    QuotaStatus lastQuotaStatus_;
    RateLimitTracker rateLimitTracker_;

    // Resilience: circuit breaker + API response cache
    CircuitBreaker circuitBreaker_;
    ApiCache apiCache_;
    bool cacheEnabled_ = false;
    mutable std::shared_mutex configMutex_;  // guards model/baseUrl/apiKey and all setters
    httplib::Headers buildHttpHeaders();

    // Internal: process a single SSE event into StreamingState
    void processSseEvent(const Json& event, StreamingState& state);

    // Internal: finalize accumulated content blocks into a response Json
    Json finalizeStreamingState(const StreamingState& state);

    // Internal: parse quota status from response headers
    void extractQuotaFromHeaders(const httplib::Headers& headers);

    // Internal: check env var for disabling non-streaming fallback
    static bool isNonStreamingFallbackDisabled();

    // Internal: attempt non-streaming call and convert to streaming-like chunks
    Result<StreamingState> fallbackToNonStreaming(
        const Json& messages,
        const Json& tools,
        std::function<void(const Json& chunk)> onChunk
    );
};

} // namespace claude
