#include <claude/api/AnthropicClient.hpp>
#include <claude/api/ApiDebugTracker.hpp>
#include <claude/api/BetaHeaders.hpp>
#include <claude/api/MessageRepair.hpp>
#include <claude/core/PromptTooLongException.hpp>
#include <claude/stream/TypedStreamEvent.hpp>
#include <claude/utils/ConnectionPool.hpp>
#include <claude/utils/SseParser.hpp>
#include <claude/utils/SystemPrompt.hpp>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <cstdlib>
#include <chrono>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace claude {

using namespace std::chrono_literals;

// ============================================================================
// Construction / configuration
// ============================================================================

AnthropicClient::AnthropicClient() {
    // Use connection pool for HTTP connection reuse
    httpClient_ = ConnectionPool::instance().getConnection(baseUrl_);
    pooledBaseUrl_ = baseUrl_;
    betaHeaders_ = api::BetaHeaders::getDefault();
}

AnthropicClient::AnthropicClient(const String& apiKey) : AnthropicClient() {
    setApiKey(apiKey);
}

AnthropicClient::~AnthropicClient() {
    if (!pooledBaseUrl_.empty()) {
        ConnectionPool::instance().releaseConnection(pooledBaseUrl_);
    }
}

void AnthropicClient::setApiKey(const String& key) {
    std::unique_lock lock(configMutex_);
    apiKey_ = key;
}

void AnthropicClient::setBaseUrl(const String& url) {
    std::unique_lock lock(configMutex_);
    // Release old pooled connection before switching
    if (!pooledBaseUrl_.empty()) {
        ConnectionPool::instance().releaseConnection(pooledBaseUrl_);
        pooledBaseUrl_.clear();
    }

    // Parse URL to extract scheme, host, port, and path prefix.
    // httplib::Client(scheme_host_port) discards the path component of its
    // constructor argument (it only passes host:port to the internal ClientImpl).
    // We must preserve the path prefix ourselves and prepend it to request paths.
    basePath_.clear();
    String hostOnly = url;
    auto schemeEnd = url.find("://");
    if (schemeEnd != String::npos) {
        auto pathStart = url.find('/', schemeEnd + 3);
        if (pathStart != String::npos) {
            basePath_ = url.substr(pathStart);          // e.g., "/anthropic" or "/v1"
            hostOnly = url.substr(0, pathStart);         // e.g., "https://api.deepseek.com"
        }
    }

    baseUrl_ = hostOnly;

    // Get a pooled client for the new base URL (reuses existing connections)
    httpClient_ = ConnectionPool::instance().getConnection(baseUrl_);
    pooledBaseUrl_ = baseUrl_;

    // Check if this is the official Anthropic API or a custom server
    isCustomBaseUrl_ = (url.find("api.anthropic.com") == String::npos);

    // Derive protocol from base URL (only when user hasn't set it explicitly).
    // TODO: an Anthropic-compatible proxy at a custom base URL would need
    // ApiProtocol::Anthropic. Currently derived automatically; callers can
    // override via setApiProtocol().
    if (!apiProtocolExplicit_) {
        apiProtocol_ = isCustomBaseUrl_
            ? ApiProtocol::OpenAICompatible
            : ApiProtocol::Anthropic;
    }

    if (isCustomBaseUrl_) {
        spdlog::debug("Using custom base URL {} - api_protocol={}", url,
                      apiProtocol_ == ApiProtocol::Anthropic ? "anthropic" : "openai-compatible");
    }
}

void AnthropicClient::setModel(const String& model) {
    std::unique_lock lock(configMutex_);
    model_ = model;
}

void AnthropicClient::setMaxTokens(int maxTokens) {
    std::unique_lock lock(configMutex_);
    maxTokens_ = maxTokens;
}

void AnthropicClient::setTemperature(double temp) {
    std::unique_lock lock(configMutex_);
    temperature_ = temp;
}

void AnthropicClient::setThinkingEnabled(bool enabled) {
    std::unique_lock lock(configMutex_);
    thinkingEnabled_ = enabled;
    if (enabled) {
        betaHeaders_ = api::BetaHeaders::merge(betaHeaders_, api::BetaHeaders::forExtendedThinking());
    }
}

void AnthropicClient::setThinkingBudget(int budget) {
    std::unique_lock lock(configMutex_);
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

    // System prompt placement depends on provider protocol:
    //   Anthropic (api.anthropic.com):   top-level req["system"], not in messages
    //   OpenAI-compatible (custom URL):  inside messages as role:"system", no top-level
    String systemContent;
    Json systemBlocks;
    bool hasSystemBlocks = false;
    Json regularMessages = Json::array();

    for (const auto& msg : messages) {
        if (msg.contains("role") && msg["role"] == "system") {
            if (msg.contains("content")) {
                if (apiProtocol_ == ApiProtocol::OpenAICompatible) {
                    // OpenAI-compatible: system stays in messages
                    regularMessages.push_back(msg);
                } else {
                    // Anthropic: extract system to place at top level
                    if (msg["content"].is_array()) {
                        systemBlocks = msg["content"];
                        hasSystemBlocks = true;
                    } else {
                        systemContent = msg["content"].get<String>();
                    }
                }
            }
        } else {
            regularMessages.push_back(msg);
        }
    }

    // Place system prompt — top-level only for Anthropic protocol
    if (apiProtocol_ == ApiProtocol::Anthropic) {
        if (hasSystemBlocks && !systemBlocks.empty()) {
            req["system"] = systemBlocks;
        } else if (!systemContent.empty()) {
            auto blocks = buildSystemPromptBlocks(
                systemContent,
                promptCachingEnabled_,
                false,  // skipGlobalCache
                querySource_
            );

            Json systemJson = Json::array();
            for (const auto& block : blocks) {
                systemJson.push_back(block.toJson());
            }
            req["system"] = systemJson;
        }
    }
    // For custom base URLs, system is already in regularMessages — no top-level field

    req["messages"] = regularMessages;

    // ========== Convert from internal format to Anthropic content-block format ==========
    // The internal buildApiRequest() produces OpenAI-compatible messages:
    //   assistant: {role:"assistant", content:"...", tool_calls:[{type:"function",function:{name,args}}]}
    //   tool:      {role:"tool", tool_call_id:"...", content:"..."}
    // Anthropic requires content-block format:
    //   assistant: {role:"assistant", content:[{type:"text"},{type:"tool_use",id,name,input},{type:"thinking"},...]}
    //   tool:      {role:"user", content:[{type:"tool_result",tool_use_id,content}]}
    // We must convert before sending.

    Json convertedMessages = Json::array();
    Json pendingToolResults;  // Accumulate consecutive tool results into one user message

    for (size_t i = 0; i < req["messages"].size(); i++) {
        auto& msg = req["messages"][i];
        String role = msg.value("role", "");

        // Flush accumulated tool results as a single user message
        auto flushToolResults = [&]() {
            if (!pendingToolResults.empty()) {
                Json userMsg;
                userMsg["role"] = "user";
                userMsg["content"] = pendingToolResults;
                convertedMessages.push_back(userMsg);
                pendingToolResults = Json::array();
            }
        };

        if (role == "user") {
            flushToolResults();

            // Already in Anthropic content-block format? Pass through.
            if (msg.contains("content") && msg["content"].is_array()) {
                convertedMessages.push_back(msg);
            } else if (msg.contains("content") && msg["content"].is_string()) {
                // Convert string content to content-block format
                String text = msg["content"].get<String>();
                Json contentBlocks = Json::array();
                contentBlocks.push_back({{"type", "text"}, {"text", text}});
                msg["content"] = contentBlocks;
                convertedMessages.push_back(msg);
            } else {
                // No content — wrap in content-block array for safety
                msg["content"] = Json::array();
                convertedMessages.push_back(msg);
            }
        }
        else if (role == "assistant") {
            flushToolResults();

            // Already in Anthropic content-block format? Pass through.
            // buildAnthropicApiMessages pre-converts assistant messages to
            // content-block arrays that include thinking, text, and tool_use blocks.
            if (msg.contains("content") && msg["content"].is_array()) {
                convertedMessages.push_back(msg);
            } else {
                // Convert from internal (OpenAI-compatible) format
                Json contentBlocks = Json::array();

                // Text content
                String textContent = msg.value("content", "");
                if (!textContent.empty()) {
                    contentBlocks.push_back({{"type", "text"}, {"text", textContent}});
                }

                // Thinking blocks (if present in the message)
                if (msg.contains("thinking") && msg["thinking"].is_string()) {
                    String thinkingContent = msg["thinking"].get<String>();
                    if (!thinkingContent.empty()) {
                        Json thinkingBlock;
                        thinkingBlock["type"] = "thinking";
                        thinkingBlock["thinking"] = thinkingContent;
                        if (msg.contains("signature") && msg["signature"].is_string()) {
                            thinkingBlock["signature"] = msg["signature"].get<String>();
                        }
                        contentBlocks.push_back(thinkingBlock);
                    }
                }

                // Redacted thinking blocks (if present)
                if (msg.contains("redacted_thinking") && msg["redacted_thinking"].is_array()) {
                    for (const auto& rt : msg["redacted_thinking"]) {
                        contentBlocks.push_back(rt);
                    }
                }

                // Tool use blocks — convert from OpenAI format
                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    for (const auto& tc : msg["tool_calls"]) {
                        Json toolUseBlock;
                        toolUseBlock["type"] = "tool_use";
                        toolUseBlock["id"] = tc.value("id", "call_0");

                        if (tc.contains("function")) {
                            toolUseBlock["name"] = tc["function"].value("name", "unknown");
                            String args = tc["function"].value("arguments", "{}");
                            try {
                                toolUseBlock["input"] = Json::parse(args);
                            } catch (...) {
                                toolUseBlock["input"] = Json::object();
                            }
                        } else {
                            toolUseBlock["name"] = "unknown";
                            toolUseBlock["input"] = Json::object();
                        }

                        contentBlocks.push_back(toolUseBlock);
                    }
                }

                // If no content blocks at all, add empty text
                if (contentBlocks.empty()) {
                    contentBlocks.push_back({{"type", "text"}, {"text", ""}});
                }

                Json assistantMsg;
                assistantMsg["role"] = "assistant";
                assistantMsg["content"] = contentBlocks;
                convertedMessages.push_back(assistantMsg);
            }
        }
        else if (role == "tool") {
            // Convert OpenAI tool result to Anthropic tool_result content block
            // Accumulate consecutive tool results into a single user message
            Json toolResultBlock;
            toolResultBlock["type"] = "tool_result";
            toolResultBlock["tool_use_id"] = msg.value("tool_call_id", "call_0");

            // Content can be string or array
            if (msg.contains("content")) {
                if (msg["content"].is_string()) {
                    toolResultBlock["content"] = msg["content"].get<String>();
                } else {
                    toolResultBlock["content"] = msg["content"];
                }
            } else {
                toolResultBlock["content"] = "";
            }

            // Propagate error flag
            if (msg.contains("is_error") && msg["is_error"].is_boolean() && msg["is_error"].get<bool>()) {
                toolResultBlock["is_error"] = true;
            }

            pendingToolResults.push_back(toolResultBlock);
        }
        else {
            // Unknown role — pass through
            flushToolResults();
            convertedMessages.push_back(msg);
        }
    }

    // Flush any remaining tool results
    if (!pendingToolResults.empty()) {
        Json userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = pendingToolResults;
        convertedMessages.push_back(userMsg);
    }

    // Repair tool_use/tool_result pairing before sending to API.
    // Compaction (AutoCompact, MicroCompact, etc.) can create orphaned tool_use
    // blocks without matching tool_result blocks, which the Anthropic API rejects
    // with a 400 error. MessageRepair scans the entire history and inserts
    // synthetic error tool_results for any unmatched tool_use blocks.
    convertedMessages = MessageRepair::repair(convertedMessages);

    req["messages"] = convertedMessages;

    // Add message-level cache breakpoint for Anthropic prompt caching
    // Rule: one cache_control marker per request, on the second-to-last message.
    // Use convertedMessages (final message array) for sizing, not regularMessages
    // which may differ due to tool-result merging.
    size_t finalCount = convertedMessages.size();
    if (promptCachingEnabled_ && finalCount >= 2) {
        size_t markerIdx = finalCount - 2;
        auto& markerMsg = req["messages"][markerIdx];

        String contentType = markerMsg.value("role", "");
        if (contentType == "user" || contentType == "assistant") {
            auto& content = markerMsg["content"];
            // Only add cache_control if content is already a content-block array
            if (content.is_array() && !content.empty()) {
                // Add cache_control to the last block (preserves existing blocks)
                content[content.size() - 1]["cache_control"] = {{"type", "ephemeral"}};
            }
        }
    }

    if (!tools.empty()) {
        // Add cache_control breakpoint on the last tool definition.
        // Tool definitions are static across turns, so caching them saves
        // significant input tokens on repeated requests.
        if (promptCachingEnabled_ && tools.size() > 0) {
            Json cachedTools = tools;
            cachedTools[cachedTools.size() - 1]["cache_control"] = {{"type", "ephemeral"}};
            req["tools"] = cachedTools;
        } else {
            req["tools"] = tools;
        }
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
    std::shared_lock lock(configMutex_);

    // Circuit breaker check
    if (!circuitBreaker_.allowCall()) {
        return std::unexpected("Circuit breaker open — too many recent failures");
    }

    // Cache lookup (only for non-streaming calls with caching enabled)
    String cacheKey;
    if (cacheEnabled_) {
        String path = isCustomBaseUrl_ ? (basePath_ + "/messages") : "/v1/messages";
        cacheKey = apiCache_.makeKey(baseUrl_ + path, buildRequest(messages, tools).dump());
        auto cached = apiCache_.getCached(cacheKey);
        if (cached) {
            try {
                spdlog::debug("API cache hit for key={}", cacheKey.substr(0, 32));
                return Json::parse(*cached);
            } catch (...) {}
        }
    }

    Json req = buildRequest(messages, tools);
    String body = req.dump();

    httplib::Headers headers = buildHttpHeaders();

    String path = isCustomBaseUrl_ ? (basePath_ + "/messages") : "/v1/messages";

    // Debug tracking
    auto callStart = std::chrono::steady_clock::now();

    auto res = httpClient_->Post(path.c_str(), headers, body, "application/json");

    auto callEnd = std::chrono::steady_clock::now();

    if (!res) {
        circuitBreaker_.recordFailure();
        auto err = httplib::to_string(res.error());

        ApiDebugTracker::instance().recordCall({
            "call", model_, getProviderName(), callStart, callEnd,
            0, 0, 0, false, err
        });

        return std::unexpected("HTTP request failed: " + err);
    }

    // Extract quota info from headers
    if (res) {
        extractQuotaFromHeaders(res->headers);
    }

    if (res->status != 200) {
        circuitBreaker_.recordFailure();
        String errMsg = "API error: " + std::to_string(res->status) + " - " + res->body.substr(0, 200);

        ApiDebugTracker::instance().recordCall({
            "call", model_, getProviderName(), callStart, callEnd,
            0, 0, res->status, false, errMsg
        });

        return std::unexpected(errMsg);
    }

    // Success — record in circuit breaker and cache
    circuitBreaker_.recordSuccess();

    if (cacheEnabled_ && !cacheKey.empty()) {
        apiCache_.cacheResponse(cacheKey, res->body);
    }

    // Parse response and extract token counts for debug tracking
    try {
        auto response = Json::parse(res->body);
        int inputTokens = 0, outputTokens = 0;
        if (response.contains("usage") && response["usage"].is_object()) {
            const auto& u = response["usage"];
            if (u.contains("input_tokens") && u["input_tokens"].is_number())
                inputTokens = u["input_tokens"].get<int>();
            if (u.contains("output_tokens") && u["output_tokens"].is_number())
                outputTokens = u["output_tokens"].get<int>();
        }

        ApiDebugTracker::instance().recordCall({
            "call", model_, getProviderName(), callStart, callEnd,
            inputTokens, outputTokens, 200, true, ""
        });

        return response;
    } catch (const Json::parse_error& e) {
        return std::unexpected("JSON parse error: " + String(e.what()));
    }
}

// ============================================================================
// SSE event processing into StreamingState
// ============================================================================

void AnthropicClient::processSseEvent(const Json& event, StreamingState& state) {
    String type = event.is_object() ? event.value("type", "") : "";

    // ------ message_start ------
    if (type == "message_start") {
        if (event.contains("message") && event["message"].is_object()) {
            const auto& msg = event["message"];
            state.model = msg.is_object() ? msg.value("model", "") : "";

            // Extract usage from message_start
            if (msg.contains("usage") && msg["usage"].is_object()) {
                const auto& u = msg["usage"];
                // Only set if non-zero; message_start has the authoritative initial values
                long cacheRead = u.contains("cache_read_input_tokens") && u["cache_read_input_tokens"].is_number()
                    ? u["cache_read_input_tokens"].get<long>() : 0;
                long cacheCreation = u.contains("cache_creation_input_tokens") && u["cache_creation_input_tokens"].is_number()
                    ? u["cache_creation_input_tokens"].get<long>() : 0;
                long cacheDeleted = u.contains("cache_deleted_input_tokens") && u["cache_deleted_input_tokens"].is_number()
                    ? u["cache_deleted_input_tokens"].get<long>() : 0;

                state.usage.promptTokens = u.contains("input_tokens") && u["input_tokens"].is_number()
                    ? u["input_tokens"].get<long>() : 0;
                state.usage.completionTokens = 0; // will be filled by message_delta
                state.usage.cacheReadTokens = cacheRead;
                state.usage.cacheCreationTokens = cacheCreation;
                state.usage.cacheDeletedInputTokens = cacheDeleted;

                // Cache breakdown from cache_creation sub-object
                if (u.contains("cache_creation") && u["cache_creation"].is_object()) {
                    const auto& cc = u["cache_creation"];
                    state.usage.cacheEphemeral5m = cc.contains("ephemeral_5m_input_tokens") && cc["ephemeral_5m_input_tokens"].is_number()
                        ? cc["ephemeral_5m_input_tokens"].get<long>() : 0;
                    state.usage.cacheEphemeral1h = cc.contains("ephemeral_1h_input_tokens") && cc["ephemeral_1h_input_tokens"].is_number()
                        ? cc["ephemeral_1h_input_tokens"].get<long>() : 0;
                }
            }
        }
    }

    // ------ content_block_start ------
    else if (type == "content_block_start") {
        if (event.contains("content_block") && event["content_block"].is_object()) {
            const auto& block = event["content_block"];
            int index = event.contains("index") && event["index"].is_number()
                ? event["index"].get<int>() : -1;
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
            // For redacted_thinking blocks, capture the encrypted data
            else if (blockType == "redacted_thinking") {
                state.currentBlockType = "redacted_thinking";
                state.accumulatedText.clear();
                if (block.contains("data") && block["data"].is_string()) {
                    state.accumulatedText = block["data"].get<String>();
                }
            }
            // For text blocks, capture initial text
            if (blockType == "text" && block.contains("text")) {
                state.accumulatedText = block.value("text", "");
            }
        }
    }

    // ------ content_block_delta ------
    else if (type == "content_block_delta") {
        if (!event.contains("delta") || !event["delta"].is_object()) return;

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
        else if (deltaType == "redacted_thinking_delta" && state.currentBlockType == "redacted_thinking") {
            if (delta.contains("data") && delta["data"].is_string()) {
                state.accumulatedText += delta["data"].get<String>();
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
        else if (state.currentBlockType == "redacted_thinking") {
            completedBlock = {
                {"type", "redacted_thinking"},
                {"data", state.accumulatedText}
            };
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
        if (event.contains("delta") && event["delta"].is_object()) {
            const auto& delta = event["delta"];
            state.stopReason = delta.value("stop_reason", "");
        }

        // Usage in message_delta provides output tokens
        // IMPORTANT: guard against overwriting real values with 0.
        // message_delta only contains output_token_count; the input/cache
        // tokens from message_start are authoritative.
        if (event.contains("usage") && event["usage"].is_object()) {
            const auto& u = event["usage"];

            // completionTokens in message_delta is authoritative
            long outputTokens = u.contains("output_tokens") && u["output_tokens"].is_number()
                ? u["output_tokens"].get<long>() : 0;
            state.usage.completionTokens = outputTokens;
            state.usage.totalTokens = state.usage.promptTokens + outputTokens;

            // Cache tokens: only overwrite if the value is non-zero.
            long cacheRead = u.contains("cache_read_input_tokens") && u["cache_read_input_tokens"].is_number()
                ? u["cache_read_input_tokens"].get<long>() : 0;
            long cacheCreation = u.contains("cache_creation_input_tokens") && u["cache_creation_input_tokens"].is_number()
                ? u["cache_creation_input_tokens"].get<long>() : 0;
            long cacheDeleted = u.contains("cache_deleted_input_tokens") && u["cache_deleted_input_tokens"].is_number()
                ? u["cache_deleted_input_tokens"].get<long>() : 0;

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
        String errMsg = "unknown streaming error";
        if (event.contains("error")) {
            if (event["error"].is_object()) {
                const auto& error = event["error"];
                // Detect overloaded_error (context window exceeded) and throw
                // PromptTooLongException so AgentLoop can trigger reactive compact
                String errorType = error.value("type", "");
                if (errorType == "overloaded_error") {
                    int actual = error.value("tokens_used", 0);
                    int max = error.value("context_window", 0);
                    throw PromptTooLongException(
                        error.value("message", "Context window exceeded"), actual, max);
                }
                if (error.contains("message") && error["message"].is_string()) {
                    errMsg = error["message"].get<String>();
                }
            } else if (event["error"].is_string()) {
                errMsg = event["error"].get<String>();
            }
        }
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
    // Update unified rate limit tracker
    rateLimitTracker_.updateFromHttpHeaders(headers);

    // Legacy QuotaStatus (kept for backward compat)
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
    spdlog::debug("Falling back to non-streaming request after streaming failure");

    auto callResult = call(messages, tools);
    if (!callResult.has_value()) {
        return Result<StreamingState>::err(callResult.error());
    }

    const Json& response = callResult.value();

    StreamingState state;
    state.didFallBackToNonStreaming = true;

    // Extract model
    state.model = response.is_object() ? response.value("model", "") : "";

    // Extract stop_reason
    state.stopReason = response.is_object() ? response.value("stop_reason", "") : "";

    // Extract usage
    if (response.contains("usage") && response["usage"].is_object()) {
        const auto& u = response["usage"];
        auto getLong = [&](const char* key) -> long {
            return u.contains(key) && u[key].is_number() ? u[key].get<long>() : 0;
        };
        state.usage.promptTokens = getLong("input_tokens");
        state.usage.completionTokens = getLong("output_tokens");
        state.usage.totalTokens = state.usage.promptTokens + state.usage.completionTokens;
        state.usage.cacheReadTokens = getLong("cache_read_input_tokens");
        state.usage.cacheCreationTokens = getLong("cache_creation_input_tokens");
        state.usage.cacheDeletedInputTokens = getLong("cache_deleted_input_tokens");

        if (u.contains("cache_creation") && u["cache_creation"].is_object()) {
            const auto& cc = u["cache_creation"];
            auto getCcLong = [&](const char* key) -> long {
                return cc.contains(key) && cc[key].is_number() ? cc[key].get<long>() : 0;
            };
            state.usage.cacheEphemeral5m = getCcLong("ephemeral_5m_input_tokens");
            state.usage.cacheEphemeral1h = getCcLong("ephemeral_1h_input_tokens");
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
    // Delegate to streamWithState, discarding the state return.
    // streamWithState handles all locking and lastDidFallBack_ internally.
    auto result = streamWithState(messages, tools, std::move(onChunk));

    if (result.isErr()) {
        spdlog::error("Stream failed (and fallback failed or disabled): {}", result.error());
        throw std::runtime_error("Stream failed: " + result.error());
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
    // ---- Reset fallback flag under unique_lock ----
    {
        std::unique_lock lock(configMutex_);
        lastDidFallBack_ = false;
    }

    // ---- Snapshot all config fields under shared_lock ----
    // The streaming lambdas outlive the lock, so they must use local copies
    // rather than reading this->apiKey_, this->model_, etc.
    String apiKey, baseUrl, model, basePath;
    int maxTokens;
    double temperature;
    bool thinkingEnabled, promptCachingEnabled, isCustomBaseUrl, cacheEnabled;
    int thinkingBudget, streamIdleTimeoutSec;
    std::vector<String> betaHeaders;
    std::shared_ptr<httplib::Client> httpClient;
    ApiProtocol apiProtocol;

    {
        std::shared_lock lock(configMutex_);
        apiKey = apiKey_;
        baseUrl = baseUrl_;
        basePath = basePath_;
        model = model_;
        maxTokens = maxTokens_;
        temperature = temperature_;
        thinkingEnabled = thinkingEnabled_;
        thinkingBudget = thinkingBudget_;
        promptCachingEnabled = promptCachingEnabled_;
        isCustomBaseUrl = isCustomBaseUrl_;
        apiProtocol = apiProtocol_;
        cacheEnabled = cacheEnabled_;
        streamIdleTimeoutSec = streamIdleTimeoutSec_;
        betaHeaders = betaHeaders_;
        httpClient = httpClient_;
    }

    // ---- Build request using snapshot values ----
    // buildRequest/buildHttpHeaders read config members directly; since we hold
    // no lock now, we must avoid calling them. Instead, inline the logic using
    // the snapshot copies. However, buildRequest is complex; the simpler and
    // safe approach is to build the request *inside* the lock scope above.
    // For clarity, we build it under a second shared_lock acquisition.
    Json req;
    httplib::Headers headers;
    String path = isCustomBaseUrl ? (basePath + "/messages") : "/v1/messages";
    {
        std::shared_lock lock(configMutex_);
        req = buildRequest(messages, tools);
        req["stream"] = true;
        headers = buildHttpHeaders();
    }

    String body = req.dump();

    spdlog::debug("Streaming to {}{} (model: {})", baseUrl, path, model);
    spdlog::debug("Request body size: {} bytes", body.size());

    // ---- CLAUDE_CODE_DEBUG_PROMPT=1: prompt fingerprint summary ----
    if (std::getenv("CLAUDE_CODE_DEBUG_PROMPT")) {
        // Extract system text + location (used by both hash and summary)
        String sysText;
        String sysLocation = "none";
        size_t sysChars = 0;
        size_t sysBlocks = 0;
        if (req.contains("system")) {
            sysLocation = "top_level";
            if (req["system"].is_array()) {
                sysBlocks = req["system"].size();
                for (auto& b : req["system"]) {
                    if (b.contains("text")) sysChars += b["text"].get<String>().size();
                    sysText += b.value("text", "");
                }
            } else {
                sysText = req["system"].get<String>();
                sysChars = sysText.size();
            }
        } else {
            // Check if system is inside messages (OpenAI-compatible format)
            for (auto& m : req["messages"]) {
                if (m.contains("role") && m["role"] == "system") {
                    sysLocation = "in_messages";
                    if (m["content"].is_array()) {
                        sysBlocks = m["content"].size();
                        for (auto& b : m["content"]) {
                            if (b.contains("text")) sysChars += b["text"].get<String>().size();
                            sysText += b.value("text", "");
                        }
                    } else {
                        sysText = m["content"].get<String>();
                        sysChars = sysText.size();
                    }
                    break;
                }
            }
        }

        // Compute stable hash for output-efficiency section (uses sysText from above)
        auto oeHash = [&]() -> String {
            auto pos = sysText.find("# Output efficiency");
            if (pos == String::npos) return "none";
            auto section = sysText.substr(pos, std::min(size_t(2048), sysText.size() - pos));
            auto h = std::hash<String>{}(section);
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%016zx", h);
            return buf;
        };

        // Check key v3 signatures
        auto has = [&](const String& s) { return sysText.find(s) != String::npos; };
        bool v3_critical = has("CRITICAL: Do NOT narrate your intent before tool calls");
        bool v3_banned   = has("BANNED before/between tool calls");
        bool v3_chinese  = has("继续读取");
        bool v3_cleanup  = has("After your tool calls complete");
        bool v3_final    = has("FINAL EXECUTION RULE");
        bool v3_1_error  = has("ERROR CORRECTION RULE");

        // Check conflicting instructions
        bool conflict_launched = has("briefly tell the user what you launched");
        bool conflict_updates  = has("give short updates");
        bool conflict_explain  = has("explain your next step");
        bool conflict_narrate  = has("narrate your progress");

        size_t msgCount = 0;
        String firstRole = "?";
        if (req.contains("messages") && req["messages"].is_array()) {
            msgCount = req["messages"].size();
            if (msgCount > 0 && req["messages"][0].contains("role"))
                firstRole = req["messages"][0]["role"].get<String>();
        }

        // Get binary path
        String binaryPath = "?";
        char exePath[1024] = {};
#if defined(__APPLE__)
        uint32_t size = sizeof(exePath);
        if (_NSGetExecutablePath(exePath, &size) == 0) {
            char resolved[1024];
            if (realpath(exePath, resolved)) binaryPath = resolved;
            else binaryPath = exePath;
        }
#elif defined(__linux__)
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len > 0) { exePath[len] = '\0'; binaryPath = exePath; }
#endif

        fprintf(stderr, "\n══ PROMPT FINGERPRINT ══\n");
        fprintf(stderr, "binary=%s\n", binaryPath.c_str());
        fprintf(stderr, "provider=%s\n",
                baseUrl.find("deepseek") != String::npos ? "deepseek" :
                (baseUrl.find("anthropic") != String::npos ? "anthropic" : "other"));
        fprintf(stderr, "model=%s\n", model.c_str());
        fprintf(stderr, "is_custom_base_url=%s\n", isCustomBaseUrl ? "true" : "false");
        fprintf(stderr, "api_protocol=%s\n",
                apiProtocol == ApiProtocol::Anthropic ? "anthropic" : "openai-compatible");
        fprintf(stderr, "prompt_version=v3.1\n");
        fprintf(stderr, "has_system=%s\n", sysLocation != "none" ? "true" : "false");
        fprintf(stderr, "system_location=%s\n", sysLocation.c_str());
        fprintf(stderr, "system_blocks=%zu\n", sysBlocks);
        fprintf(stderr, "system_chars=%zu\n", sysChars);
        fprintf(stderr, "message_count=%zu\n", msgCount);
        fprintf(stderr, "first_message_role=%s\n", firstRole.c_str());
        fprintf(stderr, "oe_section_hash=%s\n", oeHash().c_str());
        fprintf(stderr, "v3_critical=%s\n", v3_critical ? "true" : "false");
        fprintf(stderr, "v3_banned=%s\n", v3_banned ? "true" : "false");
        fprintf(stderr, "v3_chinese=%s\n", v3_chinese ? "true" : "false");
        fprintf(stderr, "v3_cleanup=%s\n", v3_cleanup ? "true" : "false");
        fprintf(stderr, "v3_final_execution_rule=%s\n", v3_final ? "true" : "false");
        fprintf(stderr, "v3_1_error_correction=%s\n", v3_1_error ? "true" : "false");
        fprintf(stderr, "conflict_launched=%s\n", conflict_launched ? "true" : "false");
        fprintf(stderr, "conflict_updates=%s\n", conflict_updates ? "true" : "false");
        fprintf(stderr, "conflict_explain=%s\n", conflict_explain ? "true" : "false");
        fprintf(stderr, "conflict_narrate=%s\n", conflict_narrate ? "true" : "false");
        fprintf(stderr, "══════════════════════\n\n");
    }

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

            // Emit typed stream events if callback is registered
            if (onTypedEvent) {
                String sseType = json->is_object() ? json->value("type", "") : "";

                if (sseType == "message_start") {
                    onTypedEvent(TypedStreamEvent{
                        .type = StreamEventType::StreamStart,
                        .usage = UsageInfo{
                            .promptTokens = state.usage.promptTokens,
                            .completionTokens = state.usage.completionTokens,
                            .cacheReadTokens = state.usage.cacheReadTokens,
                            .cacheCreationTokens = state.usage.cacheCreationTokens
                        }
                    });
                }
                else if (sseType == "content_block_start") {
                    String blockType = state.currentBlockType;
                    if (blockType == "tool_use" || blockType == "server_tool_use") {
                        onTypedEvent(TypedStreamEvent{
                            .type = StreamEventType::ToolUseStart,
                            .blockIndex = state.currentBlockIndex,
                            .toolCall = ToolCall{
                                .id = state.currentBlockId,
                                .name = state.currentBlockName,
                                .arguments = {}
                            }
                        });
                    }
                    else if (blockType == "text") {
                        onTypedEvent(TypedStreamEvent{
                            .type = StreamEventType::TextBlockStart,
                            .blockIndex = state.currentBlockIndex
                        });
                    }
                    else if (blockType == "thinking") {
                        onTypedEvent(TypedStreamEvent{
                            .type = StreamEventType::ThinkingBlockStart,
                            .blockIndex = state.currentBlockIndex
                        });
                    }
                }
                else if (sseType == "content_block_delta") {
                    if (json->contains("delta") && (*json)["delta"].is_object()) {
                        const auto& delta = (*json)["delta"];
                        String deltaType = delta.value("type", "");
                        if (deltaType == "text_delta") {
                            onTypedEvent(TypedStreamEvent{
                                .type = StreamEventType::TextDelta,
                                .text = delta.value("text", ""),
                                .blockIndex = state.currentBlockIndex
                            });
                        }
                        else if (deltaType == "thinking_delta") {
                            onTypedEvent(TypedStreamEvent{
                                .type = StreamEventType::ThinkingDelta,
                                .text = delta.value("thinking", ""),
                                .blockIndex = state.currentBlockIndex
                            });
                        }
                        else if (deltaType == "input_json_delta") {
                            onTypedEvent(TypedStreamEvent{
                                .type = StreamEventType::InputJsonDelta,
                                .text = delta.value("partial_json", ""),
                                .blockIndex = state.currentBlockIndex
                            });
                        }
                    }
                }
                else if (sseType == "content_block_stop") {
                    int index = json->value("index", state.currentBlockIndex);
                    // NOTE: processSseEvent resets currentBlockType on content_block_stop,
                    // so check state.contentBlocks[index]["type"] instead.
                    String blockType;
                    if (index >= 0 && static_cast<size_t>(index) < state.contentBlocks.size()) {
                        blockType = state.contentBlocks[index].value("type", "");
                    }
                    if (blockType == "tool_use" || blockType == "server_tool_use") {
                        // Parse the input to produce the arguments string
                        String argsStr = "{}";
                        if (state.contentBlocks[index].contains("input")) {
                            argsStr = state.contentBlocks[index]["input"].dump();
                        }
                        onTypedEvent(TypedStreamEvent{
                            .type = StreamEventType::ToolUseComplete,
                            .blockIndex = index,
                            .toolCall = ToolCall{
                                .id = state.contentBlocks[index].value("id", ""),
                                .name = state.contentBlocks[index].value("name", ""),
                                .arguments = std::move(argsStr)
                            }
                        });
                    }
                    else if (blockType == "text") {
                        onTypedEvent(TypedStreamEvent{
                            .type = StreamEventType::TextBlockStop,
                            .blockIndex = index
                        });
                    }
                    else if (blockType == "thinking") {
                        onTypedEvent(TypedStreamEvent{
                            .type = StreamEventType::ThinkingBlockStop,
                            .blockIndex = index
                        });
                    }
                }
                else if (sseType == "message_delta") {
                    onTypedEvent(TypedStreamEvent{
                        .type = StreamEventType::UsageUpdate,
                        .usage = UsageInfo{
                            .promptTokens = state.usage.promptTokens,
                            .completionTokens = state.usage.completionTokens,
                            .cacheReadTokens = state.usage.cacheReadTokens,
                            .cacheCreationTokens = state.usage.cacheCreationTokens
                        }
                    });
                }
                else if (sseType == "message_stop") {
                    onTypedEvent(TypedStreamEvent{
                        .type = StreamEventType::StreamEnd,
                        .stopReason = state.stopReason
                    });
                }
                else if (sseType == "error") {
                    String errMsg = "unknown streaming error";
                    if (json->contains("error")) {
                        if ((*json)["error"].is_object() && (*json)["error"].contains("message")
                            && (*json)["error"]["message"].is_string()) {
                            errMsg = (*json)["error"]["message"].get<String>();
                        } else if ((*json)["error"].is_string()) {
                            errMsg = (*json)["error"].get<String>();
                        }
                    }
                    onTypedEvent(TypedStreamEvent{
                        .type = StreamEventType::Error,
                        .error = std::move(errMsg)
                    });
                }
            }

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

    // Capture streamIdleTimeoutSec by value (already snapshot'd above)
    int idleTimeout = streamIdleTimeoutSec;

    auto res = httpClient->Post(path.c_str(), headers, body, "application/json",
        [&](const char* data, size_t len) {
            totalReceived += len;

            // Stall detection: check time since last chunk
            if (idleTimeout > 0 && state.currentBlockIndex >= 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration<double, std::milli>(now - state.lastChunkTime).count();
                double timeoutMs = static_cast<double>(idleTimeout) * 1000.0;

                if (elapsed > timeoutMs) {
                    state.stallCount++;
                    state.totalStallTimeMs += elapsed;
                    spdlog::warn("Stream stall detected: {:.0f}ms since last chunk (stall #{})",
                                  elapsed, state.stallCount);
                    // Reset the timer so we don't double-count
                    state.lastChunkTime = now;
                }
            }

            // Check for user-requested abort (ESC / Ctrl+C)
            if (aborted_.load(std::memory_order_acquire)) {
                spdlog::debug("Anthropic stream aborted by user");
                return false;
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
        // Finalize any partially-completed blocks before fallback
        state.finalizePartialBlocks();

        if (isNonStreamingFallbackDisabled()) {
            spdlog::warn("Streaming failed ({}) but non-streaming fallback is disabled via env",
                          fallbackReason);
            return Result<StreamingState>::err(
                "Streaming failed: " + fallbackReason + " (fallback disabled)");
        }

        auto fallbackResult = fallbackToNonStreaming(messages, tools, onChunk);
        if (fallbackResult.ok()) {
            std::unique_lock lock(configMutex_);
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

    // Cache break detection: warn if hit rate is suspiciously low
    long cacheTotal = state.usage.cacheReadTokens + state.usage.cacheCreationTokens;
    if (cacheTotal > 10000) {
        double hitRate = static_cast<double>(state.usage.cacheReadTokens) / cacheTotal;
        if (hitRate < 0.3) {
            spdlog::warn("Cache break suspected: hit rate {:.1f}% (read={}, creation={}, total={})",
                hitRate * 100, state.usage.cacheReadTokens, state.usage.cacheCreationTokens, cacheTotal);
        }
    }

    return state;
}

// ============================================================================
// Convert non-streaming response to TypedStreamEvents
// ============================================================================

std::vector<TypedStreamEvent> AnthropicClient::convertNonStreamingResponse(const Json& response) {
    std::vector<TypedStreamEvent> events;
    events.push_back(TypedStreamEvent{.type = StreamEventType::StreamStart});

    if (response.contains("usage") && response["usage"].is_object()) {
        const auto& u = response["usage"];
        events.back().usage = UsageInfo{
            .promptTokens = u.value("input_tokens", int64_t(0)),
            .completionTokens = u.value("output_tokens", int64_t(0)),
            .cacheReadTokens = u.value("cache_read_input_tokens", int64_t(0)),
            .cacheCreationTokens = u.value("cache_creation_input_tokens", int64_t(0))
        };
    }

    if (response.contains("content") && response["content"].is_array()) {
        for (auto& block : response["content"]) {
            String blockType = block.value("type", "");
            if (blockType == "text") {
                String text = block.value("text", "");
                size_t pos = 0;
                size_t last = 0;
                while ((pos = text.find("\n\n", last)) != String::npos) {
                    String chunk = text.substr(last, pos - last);
                    if (!chunk.empty()) {
                        events.push_back(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = std::move(chunk)});
                    }
                    last = pos + 2;
                }
                if (last < text.size()) {
                    events.push_back(TypedStreamEvent{.type = StreamEventType::TextDelta, .text = text.substr(last)});
                }
            } else if (blockType == "thinking") {
                events.push_back(TypedStreamEvent{
                    .type = StreamEventType::ThinkingDelta,
                    .text = block.value("thinking", "")
                });
            } else if (blockType == "tool_use") {
                events.push_back(TypedStreamEvent{
                    .type = StreamEventType::ToolUseComplete,
                    .toolCall = ToolCall{
                        .id = block.value("id", ""),
                        .name = block.value("name", ""),
                        .arguments = block.contains("input") ? block["input"].dump() : "{}"
                    }
                });
            }
        }
    }

    events.push_back(TypedStreamEvent{
        .type = StreamEventType::StreamEnd,
        .stopReason = response.value("stop_reason", "")
    });
    return events;
}

} // namespace claude
