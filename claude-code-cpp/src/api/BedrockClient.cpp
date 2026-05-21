#include <claude/api/BedrockClient.hpp>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace claude {

// ============================================================================
// Construction / configuration
// ============================================================================

BedrockClient::BedrockClient() {
    const char* envRegion = std::getenv("AWS_REGION");
    if (!envRegion || !envRegion[0]) envRegion = std::getenv("AWS_DEFAULT_REGION");
    region_ = (envRegion && envRegion[0]) ? envRegion : "us-east-1";

    loadAwsCredentials();

    String url = "https://bedrock-runtime." + region_ + ".amazonaws.com";
    httpClient_ = std::make_unique<httplib::Client>(url);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);

    // Default model: Bedrock-specific Sonnet identifier
    model_ = "us.anthropic.claude-sonnet-4-20250514-v1:0";
}

BedrockClient::BedrockClient(const String& region) : region_(region) {
    loadAwsCredentials();

    String url = "https://bedrock-runtime." + region_ + ".amazonaws.com";
    httpClient_ = std::make_unique<httplib::Client>(url);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);

    model_ = "us.anthropic.claude-sonnet-4-20250514-v1:0";
}

BedrockClient::~BedrockClient() = default;

void BedrockClient::setApiKey(const String&) {
    spdlog::debug("BedrockClient: setApiKey ignored (uses AWS credentials)");
}

void BedrockClient::setBaseUrl(const String& url) {
    httpClient_ = std::make_unique<httplib::Client>(url);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);
}

void BedrockClient::setModel(const String& model) { model_ = model; }
void BedrockClient::setMaxTokens(int maxTokens) { maxTokens_ = maxTokens; }
void BedrockClient::setTemperature(double temp) { temperature_ = temp; }
void BedrockClient::setRegion(const String& region) { region_ = region; }

// ============================================================================
// AWS credentials
// ============================================================================

void BedrockClient::loadAwsCredentials() {
    const char* accessKey = std::getenv("AWS_ACCESS_KEY_ID");
    const char* secretKey = std::getenv("AWS_SECRET_ACCESS_KEY");
    const char* sessionToken = std::getenv("AWS_SESSION_TOKEN");

    if (accessKey && accessKey[0]) awsAccessKeyId_ = accessKey;
    if (secretKey && secretKey[0]) awsSecretAccessKey_ = secretKey;
    if (sessionToken && sessionToken[0]) awsSessionToken_ = sessionToken;

    if (awsAccessKeyId_.empty() || awsSecretAccessKey_.empty()) {
        spdlog::warn("BedrockClient: AWS credentials not found. "
                      "Set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY.");
    }
}

// ============================================================================
// Request signing
// ============================================================================

void BedrockClient::signRequest(const String& method, const String& path,
                                const String& body,
                                httplib::Headers& headers,
                                const String& timestamp) {
    headers.emplace("host", "bedrock-runtime." + region_ + ".amazonaws.com");
    headers.emplace("x-amz-date", timestamp);
    headers.emplace("content-type", "application/json");

    if (!awsSessionToken_.empty()) {
        headers.emplace("x-amz-security-token", awsSessionToken_);
    }

    // NOTE: Full AWS SigV4 signing requires HMAC-SHA256 computation.
    // This is a placeholder — production use needs a proper signing library
    // (e.g., AWS SDK for C++ or a standalone SigV4 implementation).
    // For now, log a warning and add a placeholder Authorization header.
    spdlog::warn("BedrockClient: AWS SigV4 signing not yet implemented. "
                  "Requests will fail authentication. Integrate AWS SDK for proper signing.");

    String dateStamp = timestamp.substr(0, 8);
    headers.emplace("Authorization",
        "AWS4-HMAC-SHA256 Credential=" + awsAccessKeyId_ + "/" + dateStamp +
        "/" + region_ + "/bedrock/aws4_request, SignedHeaders=host;x-amz-date, Signature=placeholder");
}

// ============================================================================
// Request building
// ============================================================================

String BedrockClient::buildModelPath() const {
    // URL-encode colons in model IDs (e.g., "us.anthropic.claude-sonnet-4-20250514-v1:0")
    String encoded = model_;
    size_t pos = 0;
    while ((pos = encoded.find(':', pos)) != String::npos) {
        encoded.replace(pos, 1, "%3A");
        pos += 3;
    }
    return "/model/" + encoded + "/invoke";
}

Json BedrockClient::convertToBedrockFormat(const Json& messages, const Json& tools) {
    Json req;
    req["anthropic_version"] = "bedrock-2023-05-31";
    req["max_tokens"] = maxTokens_;

    // Bedrock format: system messages go to top-level "system" field
    Json convertedMessages = Json::array();
    for (const auto& msg : messages) {
        if (msg.contains("role") && msg["role"] == "system") {
            if (msg["content"].is_string()) {
                req["system"] = msg["content"];
            } else if (msg["content"].is_array()) {
                req["system"] = msg["content"];
            }
            continue;
        }
        convertedMessages.push_back(msg);
    }

    req["messages"] = convertedMessages;

    if (!tools.empty()) {
        req["tools"] = tools;
    }

    if (temperature_ >= 0) {
        req["temperature"] = temperature_;
    }

    return req;
}

// ============================================================================
// Non-streaming call
// ============================================================================

std::expected<Json, String> BedrockClient::call(
    const Json& messages, const Json& tools) {
    Json req = convertToBedrockFormat(messages, tools);
    String body = req.dump();
    String path = buildModelPath();

    // Build timestamp for SigV4
    httplib::Headers headers;
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time_t_now, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    String timestamp = oss.str();

    signRequest("POST", path, body, headers, timestamp);

    spdlog::debug("BedrockClient: POST {} (model: {})", path, model_);

    auto res = httpClient_->Post(path.c_str(), headers, body, "application/json");

    if (!res) {
        return std::unexpected("Bedrock HTTP request failed: " + httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        return std::unexpected("Bedrock API error: " + std::to_string(res->status) + " - " + res->body);
    }

    try {
        return Json::parse(res->body);
    } catch (const Json::parse_error& e) {
        return std::unexpected("Bedrock JSON parse error: " + String(e.what()));
    }
}

// ============================================================================
// Streaming (falls back to non-streaming)
// ============================================================================

void BedrockClient::stream(
    const Json& messages, const Json& tools,
    std::function<void(const Json& chunk)> onChunk) {
    // Bedrock streaming uses a binary event-stream format (not SSE).
    // Full support requires parsing the event-stream protocol.
    // Fall back to non-streaming call for now.
    spdlog::debug("BedrockClient: stream() falling back to non-streaming call");
    auto result = call(messages, tools);
    if (result && onChunk) {
        onChunk(*result);
    } else if (!result) {
        throw std::runtime_error(result.error());
    }
}

} // namespace claude
