#pragma once

#include "ApiClient.hpp"
#include "../core/Types.hpp"
#include <memory>
#include <string>

namespace claude {

/// Factory for creating API clients based on provider configuration.
/// Reads environment variables to determine which cloud provider to use:
/// - CLAUDE_CODE_USE_BEDROCK=1 → BedrockClient
/// - CLAUDE_CODE_USE_VERTEX=1 → VertexClient
/// - CLAUDE_CODE_USE_FOUNDRY=1 → FoundryClient (falls back to AnthropicClient)
/// - None set → AnthropicClient (first-party)
class ApiClientFactory {
public:
    /// Create the appropriate API client for the current configuration.
    /// @param apiKey Optional API key override. If empty, reads from ANTHROPIC_API_KEY.
    static std::unique_ptr<ApiClient> create(const String& apiKey = "");

    /// Create an API client for a specific provider.
    /// @param provider The provider to create a client for.
    /// @param apiKey Optional API key override.
    static std::unique_ptr<ApiClient> createForProvider(
        APIProvider provider, const String& apiKey = "");

    /// Detect the current provider from environment variables.
    static APIProvider detectProvider();

    /// Get a human-readable provider name.
    static String providerDisplayName(APIProvider provider);
};

} // namespace claude
