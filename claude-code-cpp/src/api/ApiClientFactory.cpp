#include <claude/api/ApiClientFactory.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/BedrockClient.hpp>
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
            String region = getBedrockRegion();
            auto client = std::make_unique<BedrockClient>(region);
            client->setModel(getDefaultSonnetModel(APIProvider::Bedrock));
            spdlog::info("Created Bedrock client (region: {})", region);
            return client;
        }
        case APIProvider::Vertex: {
            String region = getVertexRegionForModel("claude-sonnet-4-20250514");
            const char* envProject = std::getenv("GOOGLE_CLOUD_PROJECT");
            if (!envProject || !envProject[0]) envProject = std::getenv("GCLOUD_PROJECT");
            String project = (envProject && envProject[0]) ? envProject : "";
            auto client = std::make_unique<VertexClient>(region, project);
            client->setModel(getDefaultSonnetModel(APIProvider::Vertex));
            spdlog::info("Created Vertex client (region: {}, project: {})",
                          region, project.empty() ? "not set" : project);
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
