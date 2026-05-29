#include <claude/api/VertexClient.hpp>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <cstdlib>
#include <chrono>
#include <fstream>

namespace claude {

VertexClient::VertexClient() {
    // Region: prefer CLOUD_ML_REGION, then GOOGLE_CLOUD_REGION, then default
    const char* envRegion = std::getenv("CLOUD_ML_REGION");
    if (!envRegion || !envRegion[0]) envRegion = std::getenv("GOOGLE_CLOUD_REGION");
    region_ = (envRegion && envRegion[0]) ? envRegion : "us-east5";

    // Project: prefer GOOGLE_CLOUD_PROJECT, then GCLOUD_PROJECT
    const char* envProject = std::getenv("GOOGLE_CLOUD_PROJECT");
    if (!envProject || !envProject[0]) envProject = std::getenv("GCLOUD_PROJECT");
    projectId_ = (envProject && envProject[0]) ? envProject : "";

    model_ = "claude-sonnet-4-20250514";

    String baseUrl = "https://" + region_ + "-aiplatform.googleapis.com";
    httpClient_ = std::make_unique<httplib::Client>(baseUrl);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);

    if (projectId_.empty()) {
        spdlog::warn("VertexClient: GOOGLE_CLOUD_PROJECT not set. Set it to your GCP project ID.");
    }

    loadCredentials();
}

VertexClient::VertexClient(const String& region, const String& project)
    : region_(region), projectId_(project) {
    model_ = "claude-sonnet-4-20250514";

    String baseUrl = "https://" + region_ + "-aiplatform.googleapis.com";
    httpClient_ = std::make_unique<httplib::Client>(baseUrl);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);

    loadCredentials();
}

VertexClient::~VertexClient() = default;

void VertexClient::setApiKey(const String&) {
    // Vertex uses Google OAuth2, not an API key
    spdlog::debug("VertexClient: setApiKey ignored (uses Google OAuth2)");
}

void VertexClient::setBaseUrl(const String& url) {
    httpClient_ = std::make_unique<httplib::Client>(url);
    httpClient_->set_connection_timeout(30);
    httpClient_->set_read_timeout(120);
}

void VertexClient::setModel(const String& model) { model_ = model; }
void VertexClient::setMaxTokens(int maxTokens) { maxTokens_ = maxTokens; }
void VertexClient::setTemperature(double temp) { temperature_ = temp; }
void VertexClient::setProjectId(const String& projectId) { projectId_ = projectId; }
void VertexClient::setRegion(const String& region) { region_ = region; }

bool VertexClient::loadCredentials() {
    // 1. Service account via GOOGLE_APPLICATION_CREDENTIALS
    const char* credsPath = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
    if (credsPath && credsPath[0]) {
        std::ifstream file(credsPath);
        if (file.is_open()) {
            try {
                Json creds = Json::parse(file);
                spdlog::debug("VertexClient: loaded service account from {}", credsPath);
                // Service account token refresh requires JWT signing (not yet implemented)
                // For now, just note that we found valid credentials
                return true;
            } catch (...) {
                spdlog::warn("VertexClient: failed to parse credentials file: {}", credsPath);
            }
        }
    }

    // 2. gcloud Application Default Credentials
    const char* home = std::getenv("HOME");
    if (home && home[0]) {
        String adcPath = String(home) + "/.config/gcloud/application_default_credentials.json";
        std::ifstream file(adcPath);
        if (file.is_open()) {
            try {
                Json creds = Json::parse(file);
                if (creds.contains("access_token") && creds["access_token"].is_string()) {
                    accessToken_ = creds["access_token"].get<String>();
                    spdlog::debug("VertexClient: loaded ADC access token");
                    return true;
                }
                // ADC file exists but may need refresh (not yet implemented)
                spdlog::debug("VertexClient: found ADC file (token refresh not yet implemented)");
                return true;
            } catch (...) {
                spdlog::debug("VertexClient: failed to parse ADC file");
            }
        }
    }

    // 3. Compute Engine metadata server (detected at request time)
    spdlog::warn("VertexClient: no Google credentials found locally. "
                  "Set GOOGLE_APPLICATION_CREDENTIALS or run 'gcloud auth application-default login'.");
    return false;
}

bool VertexClient::refreshToken() {
    // If we already have a token and it hasn't expired, we're fine
    if (!accessToken_.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (tokenExpiry_ > now) return true;
        // Token expired — fall through to refresh attempt
    }

    // TODO: Implement proper token refresh:
    // - Service account: JWT signing + OAuth2 grant
    // - ADC: refresh_token via https://oauth2.googleapis.com/token
    // - Compute Engine: metadata server at http://metadata.google.internal
    spdlog::warn("VertexClient: token refresh not yet implemented — re-use credentials or set access token manually");
    return !accessToken_.empty();
}

String VertexClient::buildRequestPath() const {
    return "/v1/projects/" + projectId_ +
           "/locations/" + region_ +
           "/publishers/anthropic/models/" + model_ +
           ":streamRawPredict";
}

Json VertexClient::buildVertexRequest(const Json& messages, const Json& tools) {
    Json req;
    req["anthropic_version"] = "vertex-2023-05-31";
    req["max_tokens"] = maxTokens_;

    // Extract system messages — Vertex uses top-level "system" field
    Json convertedMessages = Json::array();
    for (const auto& msg : messages) {
        if (msg.contains("role") && msg["role"] == "system") {
            req["system"] = msg["content"];
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

std::expected<Json, String> VertexClient::call(
    const Json& messages, const Json& tools) {
    if (!refreshToken()) {
        return std::unexpected("VertexClient: failed to refresh Google access token");
    }

    Json req = buildVertexRequest(messages, tools);
    String body = req.dump();
    String path = buildRequestPath();

    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + accessToken_}
    };

    spdlog::debug("VertexClient: POST {} (body {} bytes)", path, body.size());

    auto res = httpClient_->Post(path.c_str(), headers, body, "application/json");
    if (!res) {
        return std::unexpected("Vertex HTTP request failed: " + httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        spdlog::error("Vertex API error: {} - {}", res->status, res->body.substr(0, 500));
        return std::unexpected("Vertex API error: " + std::to_string(res->status) + " - " + res->body);
    }

    try {
        return Json::parse(res->body);
    } catch (const Json::parse_error& e) {
        return std::unexpected("Vertex JSON parse error: " + String(e.what()));
    }
}

void VertexClient::stream(
    const Json& messages, const Json& tools,
    std::function<void(const Json& chunk)> onChunk) {
    if (!refreshToken()) {
        throw std::runtime_error("VertexClient: failed to refresh Google access token");
    }

    // TODO: Implement true SSE streaming via Vertex streamRawPredict.
    // For now, fall back to non-streaming call and deliver as single chunk.
    auto result = call(messages, tools);
    if (result && onChunk) {
        onChunk(*result);
    } else if (!result) {
        throw std::runtime_error(result.error());
    }
}

} // namespace claude
