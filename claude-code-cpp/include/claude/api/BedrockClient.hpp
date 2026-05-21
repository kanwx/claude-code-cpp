#pragma once

#include "ApiClient.hpp"
#include "../core/Types.hpp"
#include <httplib.h>
#include <memory>

namespace claude {

/// AWS Bedrock API client.
///
/// Uses AWS SigV4 request signing with credentials from environment.
/// Base URL: https://bedrock-runtime.{region}.amazonaws.com
/// Endpoint: /model/{modelId}/invoke (or /invoke-with-response-stream)
class BedrockClient : public ApiClient {
public:
    BedrockClient();
    explicit BedrockClient(const String& region);
    ~BedrockClient();

    // ========== Configuration ==========

    void setApiKey(const String& key) override;
    void setBaseUrl(const String& url) override;
    void setModel(const String& model) override;
    void setMaxTokens(int maxTokens) override;
    void setTemperature(double temp) override;

    // ========== Invocation ==========

    std::expected<Json, String> call(
        const Json& messages,
        const Json& tools
    ) override;

    void stream(
        const Json& messages,
        const Json& tools,
        std::function<void(const Json& chunk)> onChunk
    ) override;

    // ========== Info ==========

    String getProviderName() const override { return "bedrock"; }
    String getModelName() const override { return model_; }

    // ========== Bedrock-specific ==========

    void setRegion(const String& region);
    String getRegion() const { return region_; }

private:
    String region_;
    String model_;
    int maxTokens_ = 4096;
    double temperature_ = -1;  // -1 = not set (use API default)
    String awsAccessKeyId_;
    String awsSecretAccessKey_;
    String awsSessionToken_;

    std::unique_ptr<httplib::Client> httpClient_;

    void loadAwsCredentials();
    void signRequest(const String& method, const String& path, const String& body,
                     httplib::Headers& headers, const String& timestamp);
    String buildModelPath() const;
    Json convertToBedrockFormat(const Json& messages, const Json& tools);
};

} // namespace claude
