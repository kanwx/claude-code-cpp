#include <claude/api/ApiClientFactory.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/VertexClient.hpp>
#include <claude/utils/Provider.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>

namespace claude {

APIProvider ApiClientFactory::detectProvider() {
    return getAPIProvider();
}

String ApiClientFactory::providerDisplayName(APIProvider provider) {
    switch (provider) {
        case APIProvider::FirstParty: return "Anthropic";
        case APIProvider::Bedrock: return "AWS Bedrock";
        case APIProvider::Vertex: return "Google Vertex AI";
        case APIProvider::Foundry: return "Azure Foundry";
    }
    return "Unknown";
}

std::unique_ptr<ApiClient> ApiClientFactory::create(const String& apiKey) {
    APIProvider provider = detectProvider();
    return createForProvider(provider, apiKey);
}

std::unique_ptr<ApiClient> ApiClientFactory::createForProvider(
    APIProvider provider, const String& apiKey) {

    String effectiveKey = apiKey;
    if (effectiveKey.empty()) {
        const char* envKey = std::getenv("ANTHROPIC_API_KEY");
        if (envKey) effectiveKey = envKey;
    }

    switch (provider) {
        case APIProvider::FirstParty: {
            auto client = std::make_unique<AnthropicClient>(effectiveKey);
            spdlog::info("Created Anthropic first-party client");
            return client;
        }
        case APIProvider::Bedrock: {
            // BedrockClient not yet implemented — use AnthropicClient with Bedrock URL
            String region = getBedrockRegion();
            String bedrockUrl = "https://bedrock-runtime." + region + ".amazonaws.com";
            auto client = std::make_unique<AnthropicClient>(effectiveKey);
            client->setBaseUrl(bedrockUrl);
            client->setModel(getDefaultSonnetModel(APIProvider::Bedrock));
            spdlog::info("Created Bedrock client (region: {}, using AnthropicClient as bridge)", region);
            return client;
        }
        case APIProvider::Vertex: {
            // VertexClient not yet implemented — use AnthropicClient with Vertex URL
            String region = getVertexRegionForModel("claude-sonnet-4-20250514");
            String vertexUrl = "https://" + region + "-aiplatform.googleapis.com/v1";
            auto client = std::make_unique<AnthropicClient>(effectiveKey);
            client->setBaseUrl(vertexUrl);
            client->setModel(getDefaultSonnetModel(APIProvider::Vertex));
            spdlog::info("Created Vertex client (region: {}, using AnthropicClient as bridge)", region);
            return client;
        }
        case APIProvider::Foundry: {
            auto client = std::make_unique<AnthropicClient>(effectiveKey);
            spdlog::info("Created Foundry client (placeholder, using AnthropicClient)");
            return client;
        }
    }

    return std::make_unique<AnthropicClient>(effectiveKey);
}

} // namespace claude
