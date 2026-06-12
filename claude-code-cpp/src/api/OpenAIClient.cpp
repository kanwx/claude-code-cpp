#include <claude/api/OpenAIClient.hpp>
#include <claude/utils/SseParser.hpp>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <chrono>

namespace claude {

OpenAIClient::OpenAIClient() {
    httpClient_ = std::make_unique<httplib::Client>(baseUrl_);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);
}

OpenAIClient::OpenAIClient(const String& apiKey) : OpenAIClient() {
    setApiKey(apiKey);
}

OpenAIClient::~OpenAIClient() = default;

void OpenAIClient::setApiKey(const String& key) {
    apiKey_ = key;
}

void OpenAIClient::setBaseUrl(const String& url) {
    baseUrl_ = url;
    httpClient_ = std::make_unique<httplib::Client>(baseUrl_);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);
}

void OpenAIClient::setModel(const String& model) {
    model_ = model;
}

void OpenAIClient::setMaxTokens(int maxTokens) {
    maxTokens_ = maxTokens;
}

void OpenAIClient::setTemperature(double temp) {
    temperature_ = temp;
}

void OpenAIClient::setStreamTimeout(int seconds) {
    streamTimeoutSeconds_ = seconds;
}

void OpenAIClient::setCircuitBreakerConfig(const CircuitBreakerConfig& config) {
    circuitBreaker_.~CircuitBreaker();
    new (&circuitBreaker_) CircuitBreaker(config);
}

Json OpenAIClient::getCircuitBreakerStats() const {
    return circuitBreaker_.getStats();
}

void OpenAIClient::resetCircuitBreaker() {
    circuitBreaker_.reset();
}

Json OpenAIClient::buildRequest(const Json& messages, const Json& tools) {
    Json req = {
        {"model", model_},
        {"max_tokens", maxTokens_},
        {"temperature", temperature_},
        {"messages", messages}
    };

    // 工具已经由 AgentLoop 转换为 OpenAI 格式，直接使用
    if (!tools.empty()) {
        req["tools"] = tools;
    }

    return req;
}

std::expected<Json, String> OpenAIClient::call(const Json& messages, const Json& tools) {
    // Check circuit breaker
    if (!circuitBreaker_.allowCall()) {
        String error = "Circuit breaker is OPEN - too many failures. State: " +
            circuitBreaker_.getStateString() + ". Try again later or call resetCircuitBreaker().";
        return std::unexpected(error);
    }

    Json req = buildRequest(messages, tools);
    String body = req.dump();

    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };
    // 只有非空 API key 才添加 Authorization header
    if (!apiKey_.empty()) {
        headers.emplace("Authorization", "Bearer " + apiKey_);
    }

    auto res = httpClient_->Post("/v1/chat/completions", headers, body, "application/json");

    if (!res) {
        circuitBreaker_.recordFailure();
        return std::unexpected("HTTP request failed: " + httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        circuitBreaker_.recordFailure();
        return std::unexpected("API error: " + std::to_string(res->status) + " - " + res->body);
    }

    try {
        Json response = Json::parse(res->body);
        // 转换为统一格式
        Json result;
        const auto& choice = response["choices"][0];
        const auto& msg = choice["message"];

        result["content"] = msg.value("content", "");

        if (msg.contains("tool_calls")) {
            result["tool_calls"] = msg["tool_calls"];
        }

        if (response.contains("usage")) {
            result["usage"] = response["usage"];
        }

        circuitBreaker_.recordSuccess();
        return result;
    } catch (const Json::parse_error& e) {
        circuitBreaker_.recordFailure();
        return std::unexpected("JSON parse error: " + String(e.what()));
    }
}

void OpenAIClient::stream(
    const Json& messages,
    const Json& tools,
    std::function<void(const Json& chunk)> onChunk
) {
    // Check circuit breaker
    if (!circuitBreaker_.allowCall()) {
        Json errorChunk = {
            {"type", "error"},
            {"error", "Circuit breaker is OPEN - too many failures"}
        };
        onChunk(errorChunk);
        return;
    }

    Json req = buildRequest(messages, tools);
    req["stream"] = true;

    String body = req.dump();

    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };
    // 只有非空 API key 才添加 Authorization header
    if (!apiKey_.empty()) {
        headers.emplace("Authorization", "Bearer " + apiKey_);
    }

    // 创建 SSE 解析器
    SseParser parser;
    bool success = false;
    bool hasError = false;
    String error;

    parser.setOnEvent([&](const SseEvent& event) {
        auto json = SseParser::extractJson(event);
        if (json) {
            // 直接传递原始 chunk（保持 OpenAI 格式）
            Json& chunk = *json;
            if (chunk.contains("choices") && !chunk["choices"].empty()) {
                success = true;

                // Check for refusal finish_reason — the model declined to respond
                const auto& choice = chunk["choices"][0];
                if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                    String finishReason = choice["finish_reason"].get<String>();
                    if (finishReason == "refusal") {
                        spdlog::warn("OpenAI stream ended with refusal finish_reason");
                        Json errorChunk = {
                            {"type", "error"},
                            {"error", "Model refused to respond"}
                        };
                        onChunk(errorChunk);
                        return;
                    }
                }
            }
            onChunk(chunk);
        } else {
            spdlog::debug("SSE event data is not valid JSON: {}", event.data.substr(0, 100));
        }
    });

    parser.setOnError([&](const String& err) {
        hasError = true;
        error = err;
    });

    spdlog::debug("OpenAI stream request to {}/v1/chat/completions, body size={}", baseUrl_, body.size());

    // Wall-clock timeout: httplib's read_timeout resets per data chunk,
    // so a model sending occasional keep-alive chunks never triggers it.
    // We track total elapsed time and abort by returning false from the
    // content receiver callback.
    auto streamStart = std::chrono::steady_clock::now();

    // 使用 httplib 的 Post 方法，正确处理流式响应
    auto res = httpClient_->Post(
        "/v1/chat/completions",
        headers,
        body,
        "application/json",
        [&](const char* data, size_t len) -> bool {
            // Check user-requested abort (ESC / Ctrl+C)
            if (aborted_.load(std::memory_order_acquire)) {
                spdlog::debug("OpenAI stream aborted by user");
                return false;
            }

            // Check wall-clock timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - streamStart).count();
            if (streamTimeoutSeconds_ > 0 && elapsed >= streamTimeoutSeconds_) {
                spdlog::warn("OpenAI stream wall-clock timeout after {}s, aborting", elapsed);
                return false;
            }

            spdlog::debug("SSE raw chunk ({} bytes): {}", len,
                std::string(data, std::min(len, (size_t)200)));
            parser.feed(data, len);
            return true;
        }
    );

    // Record result
    if (res && res->status == 200 && success) {
        circuitBreaker_.recordSuccess();
        spdlog::debug("OpenAI stream completed successfully");
    } else {
        circuitBreaker_.recordFailure();
        if (res) {
            spdlog::error("OpenAI stream failed: HTTP {} body={}", res->status,
                res->body.substr(0, 200));
        } else {
            spdlog::error("OpenAI stream failed: {}", httplib::to_string(res.error()));
        }
    }
}

} // namespace claude
