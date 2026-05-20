#include <claude/api/AnthropicClient.hpp>
#include <claude/api/MessageRepair.hpp>
#include <claude/utils/SseParser.hpp>
#include <claude/utils/SystemPrompt.hpp>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <cstdlib>
#include <chrono>

namespace claude {

using namespace std::chrono_literals;

// ============================================================================
// Construction / configuration
// ============================================================================

AnthropicClient::AnthropicClient() {
    httpClient_ = std::make_unique<httplib::Client>(baseUrl_);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);
    betaHeaders_ = api::BetaHeaders::getDefault();
}

AnthropicClient::AnthropicClient(const String& apiKey) : AnthropicClient() {
    setApiKey(apiKey);
}

AnthropicClient::~AnthropicClient() = default;

void AnthropicClient::setApiKey(const String& key) {
    apiKey_ = key;
}

void AnthropicClient::setBaseUrl(const String& url) {
    baseUrl_ = url;
    httpClient_ = std::make_unique<httplib::Client>(baseUrl_);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);

    // Check if this is the official Anthropic API or a custom server
    isCustomBaseUrl_ = (url.find("api.anthropic.com") == String::npos);

    if (isCustomBaseUrl_) {
        spdlog::info("Using custom base URL {} - will use OpenAI-compatible format", url);
    }
}

void AnthropicClient::setModel(const String& model) {
    model_ = model;
}

void AnthropicClient::setMaxTokens(int maxTokens) {
    maxTokens_ = maxTokens;
}

void AnthropicClient::setTemperature(double temp) {
    temperature_ = temp;
}

void AnthropicClient::setThinkingEnabled(bool enabled) {
    thinkingEnabled_ = enabled;
    if (enabled) {
        betaHeaders_ = api::BetaHeaders::merge(betaHeaders_, api::BetaHeaders::forExtendedThinking());
    }
}

void AnthropicClient::setThinkingBudget(int budget) {
    thinkingBudget_ = budget;
}

// ============================================================================
// Request building
// ============================================================================

Json AnthropicClient::buildRequest(const Json& messages, const Json& tools) {
    Json req = {
        {"model", model_},
        {"max_tokens", maxTokens_},
        {"messages", Json::array()}
    };

    // Anthropic format: system is a top-level param, not in messages
    String systemContent;
    Json regularMessages = Json::array();

    for (const auto& msg : messages) {
        if (msg.contains("role") && msg["role"] == "system") {
            if (msg.contains("content")) {
                systemContent = msg["content"].get<String>();
            }
        } else {
            regularMessages.push_back(msg);
        }
    }

    // Set system message (with caching blocks)
    if (!systemContent.empty()) {
        auto systemBlocks = buildSystemPromptBlocks(
            systemContent,
            promptCachingEnabled_,
            false,  // skipGlobalCache
            querySource_
        );

        Json systemJson = Json::array();
        for (const auto& block : systemBlocks) {
            systemJson.push_back(block.toJson());
        }
        req["system"] = systemJson;
    }

    req["messages"] = regularMessages;

    // Add message-level cache breakpoint for Anthropic prompt caching
    // Rule: one cache_control marker per request, on the second-to-last message
    // This enables cache hits on the prefix while the latest user message changes
    if (promptCachingEnabled_ && regularMessages.size() >= 2) {
        size_t markerIdx = regularMessages.size() - 2;
        auto& markerMsg = req["messages"][markerIdx];

        // Convert content to content-block format and add cache_control
        String contentType = markerMsg.value("role", "");
        if (contentType == "user" || contentType == "assistant") {
            String textContent = markerMsg.value("content", "");
            if (!textContent.empty()) {
                Json contentBlocks = Json::array();
                Json block;
                block["type"] = "text";
                block["text"] = textContent;
                block["cache_control"] = {{"type", "ephemeral"}};
                contentBlocks.push_back(block);
                req["messages"][markerIdx]["content"] = contentBlocks;
            }
        }
    }

    if (!tools.empty()) {
        req["tools"] = tools;
    }

    if (temperature_ >= 0) {
        req["temperature"] = temperature_;
    }

    if (thinkingEnabled_) {
        req["thinking"] = {
            {"type", "enabled"},
            {"budget_tokens", thinkingBudget_}
        };
    }

    return req;
}

httplib::Headers AnthropicClient::buildHttpHeaders() {
    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };
    if (!apiKey_.empty()) {
        headers.emplace("x-api-key", apiKey_);
    }
    headers.emplace("anthropic-version", "2023-06-01");
    if (!betaHeaders_.empty()) {
        String betaValue = api::BetaHeaders::buildHeaderString(betaHeaders_);
        if (!betaValue.empty()) {
            headers.emplace("anthropic-beta", betaValue);
        }
    }
    return headers;
}

// ============================================================================
// Non-streaming call
// ============================================================================

std::expected<Json, String> AnthropicClient::call(const Json& messages, const Json& tools) {
    Json req = buildRequest(messages, tools);
    String body = req.dump();

    httplib::Headers headers = buildHttpHeaders();

    String path = isCustomBaseUrl_ ? "/messages" : "/v1/messages";

    auto res = httpClient_->Post(path.c_str(), headers, body, "application/json");

    if (!res) {
        auto err = httplib::to_string(res.error());
        return std::unexpected("HTTP request failed: " + err);
    }

    // Extract quota info from headers
    if (res) {
        extractQuotaFromHeaders(res->headers);
    }

    if (res->status != 200) {
        return std::unexpected("API error: " + std::to_string(res->status) + " - " + res->body);
    }

    try {
        return Json::parse(res->body);
    } catch (const Json::parse_error& e) {
        return std::unexpected("JSON parse error: " + String(e.what()));
    }
}

// ============================================================================
// SSE event processing into StreamingState
// ============================================================================

void AnthropicClient::processSseEvent(const Json& event, StreamingState& state) {
    String type = event.value("type", "");

    // ------ message_start ------
    if (type == "message_start") {
        if (event.contains("message")) {
            const auto& msg = event["message"];
            state.model = msg.value("model", "");

            // Extract usage from message_start
            if (msg.contains("usage")) {
                const auto& u = msg["usage"];
                // Only set if non-zero; message_start has the authoritative initial values
                long cacheRead = u.value("cache_read_input_tokens", 0);
                long cacheCreation = u.value("cache_creation_input_tokens", 0);
                long cacheDeleted = u.value("cache_deleted_input_tokens", 0);

                state.usage.promptTokens = u.value("input_tokens", 0);
                state.usage.completionTokens = 0; // will be filled by message_delta
                state.usage.cacheReadTokens = cacheRead;
                state.usage.cacheCreationTokens = cacheCreation;
                state.usage.cacheDeletedInputTokens = cacheDeleted;

                // Cache breakdown from cache_creation sub-object
                if (u.contains("cache_creation") && u["cache_creation"].is_object()) {
                    const auto& cc = u["cache_creation"];
                    state.usage.cacheEphemeral5m = cc.value("ephemeral_5m_input_tokens", 0);
                    state.usage.cacheEphemeral1h = cc.value("ephemeral_1h_input_tokens", 0);
                }
            }
        }
    }

    // ------ content_block_start ------
    else if (type == "content_block_start") {
        if (event.contains("content_block")) {
            const auto& block = event["content_block"];
            int index = event.value("index", -1);
            String blockType = block.value("type", "");

            state.currentBlockIndex = index;
            state.currentBlockType = blockType;
            state.accumulatedText.clear();
            state.accumulatedInputJson.clear();

            // Handle tool_use and server_tool_use the same way
            if (blockType == "tool_use" || blockType == "server_tool_use") {
                state.currentBlockId = block.value("id", "");
                state.currentBlockName = block.value("name", "");
                state.accumulatedInputJson = "{}"; // initialize with empty object

                // Track advisor tool calls from server_tool_use
                if (blockType == "server_tool_use" && state.currentBlockName == "advisor") {
                    spdlog::debug("Detected advisor server_tool_use block (id={})", state.currentBlockId);
                }
            } else {
                state.currentBlockId.clear();
                state.currentBlockName.clear();
            }

            // For thinking blocks, capture the initial thinking content
            if (blockType == "thinking" && block.contains("thinking")) {
                state.accumulatedText = block.value("thinking", "");
            }
            // For text blocks, capture initial text
            if (blockType == "text" && block.contains("text")) {
                state.accumulatedText = block.value("text", "");
            }
        }
    }

    // ------ content_block_delta ------
    else if (type == "content_block_delta") {
        if (!event.contains("delta")) return;

        const auto& delta = event["delta"];
        String deltaType = delta.value("type", "");

        if (deltaType == "text_delta") {
            state.accumulatedText += delta.value("text", "");
        }
        else if (deltaType == "thinking_delta") {
            state.accumulatedText += delta.value("thinking", "");
        }
        else if (deltaType == "input_json_delta") {
            // Accumulate partial JSON for tool_use / server_tool_use input
            String partial = delta.value("partial_json", "");
            if (!partial.empty()) {
                // The first delta replaces the initial "{}"
                if (state.accumulatedInputJson == "{}") {
                    state.accumulatedInputJson = partial;
                } else {
                    state.accumulatedInputJson += partial;
                }
            }
        }
        else if (deltaType == "signature_delta") {
            // Extended thinking signature — accumulate but don't need separate field
            // These are handled at block finalization
        }
    }

    // ------ content_block_stop ------
    else if (type == "content_block_stop") {
        int index = event.value("index", state.currentBlockIndex);

        // Finalize and append the completed content block
        Json completedBlock;

        if (state.currentBlockType == "text") {
            completedBlock = {
                {"type", "text"},
                {"text", state.accumulatedText}
            };
        }
        else if (state.currentBlockType == "thinking") {
            completedBlock = {
                {"type", "thinking"},
                {"thinking", state.accumulatedText}
            };
            // Signature would be captured from the final signature_delta
            // if present; for now we include what we have
        }
        else if (state.currentBlockType == "tool_use" || state.currentBlockType == "server_tool_use") {
            // Parse accumulated input JSON
            Json inputObj;
            if (!state.accumulatedInputJson.empty()) {
                try {
                    inputObj = Json::parse(state.accumulatedInputJson);
                } catch (const Json::parse_error& e) {
                    spdlog::warn("Failed to parse tool_use input_json: {} (raw: '{}')",
                                  e.what(), state.accumulatedInputJson);
                    inputObj = Json::object();
                }
            }

            // server_tool_use is normalized to tool_use in the response
            // but we preserve the original type for downstream consumers
            completedBlock = {
                {"type", state.currentBlockType},
                {"id", state.currentBlockId},
                {"name", state.currentBlockName},
                {"input", inputObj}
            };
        }
        else {
            // Unknown block type — preserve as generic object
            completedBlock = {{"type", state.currentBlockType}};
        }

        // Ensure the array is large enough
        while (static_cast<int>(state.contentBlocks.size()) <= index) {
            state.contentBlocks.push_back(Json::object());
        }
        state.contentBlocks[index] = completedBlock;

        // Reset accumulation state
        state.currentBlockIndex = -1;
        state.currentBlockType.clear();
        state.currentBlockId.clear();
        state.currentBlockName.clear();
        state.accumulatedText.clear();
        state.accumulatedInputJson.clear();
    }

    // ------ message_delta ------
    else if (type == "message_delta") {
        if (event.contains("delta")) {
            const auto& delta = event["delta"];
            state.stopReason = delta.value("stop_reason", "");
        }

        // Usage in message_delta provides output tokens
        // IMPORTANT: guard against overwriting real values with 0.
        // message_delta only contains output_token_count; the input/cache
        // tokens from message_start are authoritative.
        if (event.contains("usage")) {
            const auto& u = event["usage"];

            // completionTokens in message_delta is authoritative
            long outputTokens = u.value("output_tokens", 0);
            state.usage.completionTokens = outputTokens;
            state.usage.totalTokens = state.usage.promptTokens + outputTokens;

            // Cache tokens: only overwrite if the value is non-zero.
            // The API may send 0 in message_delta for fields already set
            // in message_start, and we must not clobber the real values.
            long cacheRead = u.value("cache_read_input_tokens", 0);
            long cacheCreation = u.value("cache_creation_input_tokens", 0);
            long cacheDeleted = u.value("cache_deleted_input_tokens", 0);

            if (cacheRead > 0) {
                state.usage.cacheReadTokens = cacheRead;
            }
            if (cacheCreation > 0) {
                state.usage.cacheCreationTokens = cacheCreation;
            }
            if (cacheDeleted > 0) {
                state.usage.cacheDeletedInputTokens = cacheDeleted;
            }

            // Cache breakdown in message_delta
            if (u.contains("cache_creation") && u["cache_creation"].is_object()) {
                const auto& cc = u["cache_creation"];
                long e5m = cc.value("ephemeral_5m_input_tokens", 0);
                long e1h = cc.value("ephemeral_1h_input_tokens", 0);
                if (e5m > 0) state.usage.cacheEphemeral5m = e5m;
                if (e1h > 0) state.usage.cacheEphemeral1h = e1h;
            }
        }
    }

    // ------ message_stop ------
    else if (type == "message_stop") {
        // Stream complete — nothing extra to do
    }

    // ------ ping ------
    else if (type == "ping") {
        // Keep-alive, ignore
    }

    // ------ error ------
    else if (type == "error") {
        String errMsg = event.value("error", Json::object()).value("message", "unknown streaming error");
        spdlog::error("Streaming error event: {}", errMsg);
    }

    // Update last chunk time for stall detection
    state.lastChunkTime = std::chrono::steady_clock::now();
}

// ============================================================================
// Finalize streaming state into a response Json
// ============================================================================

Json AnthropicClient::finalizeStreamingState(const StreamingState& state) {
    // Build a response that mimics the non-streaming message format
    Json response;
    response["id"] = "msg_stream_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    response["type"] = "message";
    response["role"] = "assistant";
    response["content"] = state.contentBlocks;
    response["model"] = state.model;
    response["stop_reason"] = state.stopReason;

    Json usageObj;
    usageObj["input_tokens"] = state.usage.promptTokens;
    usageObj["output_tokens"] = state.usage.completionTokens;
    usageObj["cache_read_input_tokens"] = state.usage.cacheReadTokens;
    usageObj["cache_creation_input_tokens"] = state.usage.cacheCreationTokens;
    usageObj["cache_deleted_input_tokens"] = state.usage.cacheDeletedInputTokens;

    if (state.usage.cacheEphemeral5m > 0 || state.usage.cacheEphemeral1h > 0) {
        usageObj["cache_creation"] = {
            {"ephemeral_5m_input_tokens", state.usage.cacheEphemeral5m},
            {"ephemeral_1h_input_tokens", state.usage.cacheEphemeral1h}
        };
    }

    response["usage"] = usageObj;
    return response;
}

// ============================================================================
// Quota extraction from headers
// ============================================================================

void AnthropicClient::extractQuotaFromHeaders(const httplib::Headers& headers) {
    auto getLong = [&](const char* key) -> long {
        auto it = headers.find(key);
        if (it != headers.end()) {
            try { return std::stol(it->second); }
            catch (...) { return 0; }
        }
        return 0;
    };

    lastQuotaStatus_.requestsLimit = getLong("anthropic-ratelimit-unified-limit");
    lastQuotaStatus_.requestsRemaining = getLong("anthropic-ratelimit-unified-remaining");
    lastQuotaStatus_.tokensLimit = getLong("anthropic-ratelimit-tokens-limit");
    lastQuotaStatus_.tokensRemaining = getLong("anthropic-ratelimit-tokens-remaining");

    // Detect free tier
    auto quotaIt = headers.find("x-quota-limit");
    if (quotaIt != headers.end()) {
        lastQuotaStatus_.isFreeTier = true;
    }
}

// ============================================================================
// Env var check
// ============================================================================

bool AnthropicClient::isNonStreamingFallbackDisabled() {
    return std::getenv("CLAUDE_CODE_DISABLE_NONSTREAMING_FALLBACK") != nullptr;
}

// ============================================================================
// Fallback: non-streaming call -> convert to streaming-like result
// ============================================================================

Result<StreamingState> AnthropicClient::fallbackToNonStreaming(
    const Json& messages,
    const Json& tools,
    std::function<void(const Json& chunk)> onChunk
) {
    spdlog::info("Falling back to non-streaming request after streaming failure");

    auto callResult = call(messages, tools);
    if (!callResult.has_value()) {
        return Result<StreamingState>::err(callResult.error());
    }

    const Json& response = callResult.value();

    StreamingState state;
    state.didFallBackToNonStreaming = true;

    // Extract model
    state.model = response.value("model", "");

    // Extract stop_reason
    state.stopReason = response.value("stop_reason", "");

    // Extract usage
    if (response.contains("usage")) {
        const auto& u = response["usage"];
        state.usage.promptTokens = u.value("input_tokens", 0);
        state.usage.completionTokens = u.value("output_tokens", 0);
        state.usage.totalTokens = state.usage.promptTokens + state.usage.completionTokens;
        state.usage.cacheReadTokens = u.value("cache_read_input_tokens", 0);
        state.usage.cacheCreationTokens = u.value("cache_creation_input_tokens", 0);
        state.usage.cacheDeletedInputTokens = u.value("cache_deleted_input_tokens", 0);

        if (u.contains("cache_creation") && u["cache_creation"].is_object()) {
            const auto& cc = u["cache_creation"];
            state.usage.cacheEphemeral5m = cc.value("ephemeral_5m_input_tokens", 0);
            state.usage.cacheEphemeral1h = cc.value("ephemeral_1h_input_tokens", 0);
        }
    }

    // Content blocks come directly from the non-streaming response
    if (response.contains("content") && response["content"].is_array()) {
        state.contentBlocks = response["content"];
    }

    // Emit the full response as a single chunk for the callback
    if (onChunk) {
        onChunk(response);
    }

    return state;
}

// ============================================================================
// Basic stream() — now with fallback support
// ============================================================================

void AnthropicClient::stream(
    const Json& messages,
    const Json& tools,
    std::function<void(const Json& chunk)> onChunk
) {
    lastDidFallBack_ = false;

    // Delegate to streamWithState, discarding the state return
    auto result = streamWithState(messages, tools, std::move(onChunk));

    if (result.isErr()) {
        spdlog::error("Stream failed (and fallback failed or disabled): {}", result.error());
    }
}

// ============================================================================
// Enhanced streaming with state accumulation and fallback
// ============================================================================

Result<StreamingState> AnthropicClient::streamWithState(
    const Json& messages,
    const Json& tools,
    std::function<void(const Json& chunk)> onChunk
) {
    lastDidFallBack_ = false;

    Json req = buildRequest(messages, tools);
    req["stream"] = true;

    String body = req.dump();

    httplib::Headers headers = buildHttpHeaders();

    String path = isCustomBaseUrl_ ? "/messages" : "/v1/messages";

    spdlog::debug("Streaming to {}{} (model: {})", baseUrl_, path, model_);
    spdlog::debug("Request body size: {} bytes", body.size());

    // Initialize streaming state
    StreamingState state;
    state.reset();

    // SSE parser
    SseParser parser;
    int eventCount = 0;
    bool parseError = false;
    String parseErrorMsg;

    parser.setOnEvent([&](const SseEvent& event) {
        eventCount++;
        auto json = SseParser::extractJson(event);
        if (json) {
            // Process into streaming state
            processSseEvent(*json, state);

            // Forward to user callback
            if (onChunk) {
                onChunk(*json);
            }
        } else {
            parseError = true;
            parseErrorMsg = "SSE event data is not valid JSON";
            spdlog::warn("SSE parse error: event type='{}' data='{}'",
                          event.type, event.data.substr(0, 100));
        }
    });

    parser.setOnError([&](const String& err) {
        parseError = true;
        parseErrorMsg = err;
        spdlog::error("SSE parser error: {}", err);
    });

    // ---- Stream idle watchdog ----
    size_t totalReceived = 0;
    bool connectionFailed = false;
    bool httpError = false;
    int httpStatus = 0;

    auto res = httpClient_->Post(path.c_str(), headers, body, "application/json",
        [&](const char* data, size_t len) {
            totalReceived += len;

            // Stall detection: check time since last chunk
            if (streamIdleTimeoutSec_ > 0 && state.currentBlockIndex >= 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration<double, std::milli>(now - state.lastChunkTime).count();
                double timeoutMs = static_cast<double>(streamIdleTimeoutSec_) * 1000.0;

                if (elapsed > timeoutMs) {
                    state.stallCount++;
                    state.totalStallTimeMs += elapsed;
                    spdlog::warn("Stream stall detected: {:.0f}ms since last chunk (stall #{})",
                                  elapsed, state.stallCount);
                    // Reset the timer so we don't double-count
                    state.lastChunkTime = now;
                }
            }

            // Feed data to parser
            parser.feed(data, len);
            return true;
        }
    );

    if (!res) {
        connectionFailed = true;
        auto err = httplib::to_string(res.error());
        spdlog::error("Stream request failed: {}", err);
    } else {
        httpStatus = res->status;
        if (res->status != 200) {
            httpError = true;
            spdlog::error("Stream request failed with status {}: {}",
                           res->status, res->body.substr(0, 200));
        }

        // Extract quota from headers even on error
        extractQuotaFromHeaders(res->headers);

        spdlog::debug("Stream completed: {} events, {} bytes received", eventCount, totalReceived);
    }

    // ---- Determine if we need to fall back ----
    bool shouldFallback = false;
    String fallbackReason;

    if (connectionFailed) {
        shouldFallback = true;
        fallbackReason = "connection failed";
    } else if (httpError && (httpStatus == 404 || httpStatus >= 500)) {
        shouldFallback = true;
        fallbackReason = "HTTP " + std::to_string(httpStatus);
    } else if (parseError) {
        shouldFallback = true;
        fallbackReason = "SSE parse error: " + parseErrorMsg;
    } else if (eventCount == 0 && !connectionFailed) {
        // Empty stream — no events at all
        shouldFallback = true;
        fallbackReason = "empty stream (0 events)";
    }

    if (shouldFallback) {
        if (isNonStreamingFallbackDisabled()) {
            spdlog::warn("Streaming failed ({}) but non-streaming fallback is disabled via env",
                          fallbackReason);
            return Result<StreamingState>::err(
                "Streaming failed: " + fallbackReason + " (fallback disabled)");
        }

        auto fallbackResult = fallbackToNonStreaming(messages, tools, onChunk);
        if (fallbackResult.ok()) {
            lastDidFallBack_ = true;
            state = fallbackResult.value();
            state.usage.stallCount = state.stallCount;
            state.usage.totalStallTimeMs = state.totalStallTimeMs;
        }
        return fallbackResult;
    }

    // ---- Success path ----
    // Copy watchdog stats into usage
    state.usage.stallCount = state.stallCount;
    state.usage.totalStallTimeMs = state.totalStallTimeMs;

    return state;
}

} // namespace claude
