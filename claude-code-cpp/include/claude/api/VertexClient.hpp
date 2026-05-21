#pragma once

#include "ApiClient.hpp"
#include "../core/Types.hpp"
#include <chrono>
#include <memory>

namespace httplib { class Client; }

namespace claude {

/// Google Vertex AI API client.
///
/// Uses Google OAuth2 with Application Default Credentials (ADC):
/// 1. GOOGLE_APPLICATION_CREDENTIALS env var (service account JSON)
/// 2. gcloud ADC (~/.config/gcloud/application_default_credentials.json)
/// 3. Compute Engine metadata server
///
/// Base URL: https://{region}-aiplatform.googleapis.com
/// Endpoint: /v1/projects/{project}/locations/{region}/publishers/anthropic/models/{model}:streamRawPredict
class VertexClient : public ApiClient {
public:
    VertexClient();
    VertexClient(const String& region, const String& project);
    ~VertexClient();

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

    // ========== 信息 ==========

    String getProviderName() const override { return "vertex"; }
    String getModelName() const override { return model_; }

    // ========== Vertex 专用 ==========

    void setProjectId(const String& projectId);
    void setRegion(const String& region);

private:
    String region_;
    String projectId_;
    String model_;
    int maxTokens_ = 8192;
    double temperature_ = -1;  // -1 = not set (use API default)

    // Google OAuth2
    String accessToken_;
    std::chrono::steady_clock::time_point tokenExpiry_;

    std::unique_ptr<httplib::Client> httpClient_;

    bool loadCredentials();
    bool refreshToken();
    String buildRequestPath() const;
    Json buildVertexRequest(const Json& messages, const Json& tools);
};

} // namespace claude
