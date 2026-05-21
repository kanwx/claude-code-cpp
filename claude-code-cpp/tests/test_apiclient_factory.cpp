// tests/test_apiclient_factory.cpp
#include <catch2/catch_test_macros.hpp>
#include <claude/api/ApiClientFactory.hpp>
#include <cstdlib>

using namespace claude;

TEST_CASE("ApiClientFactory creates AnthropicClient by default", "[factory]") {
    unsetenv("CLAUDE_CODE_USE_BEDROCK");
    unsetenv("CLAUDE_CODE_USE_VERTEX");
    unsetenv("CLAUDE_CODE_USE_FOUNDRY");

    auto client = ApiClientFactory::create();
    REQUIRE(client != nullptr);
    REQUIRE(client->getProviderName() == "anthropic");
}

TEST_CASE("ApiClientFactory detectProvider returns FirstParty by default", "[factory]") {
    unsetenv("CLAUDE_CODE_USE_BEDROCK");
    unsetenv("CLAUDE_CODE_USE_VERTEX");
    unsetenv("CLAUDE_CODE_USE_FOUNDRY");

    APIProvider provider = ApiClientFactory::detectProvider();
    REQUIRE(provider == APIProvider::FirstParty);
}

TEST_CASE("ApiClientFactory createForProvider creates correct type", "[factory]") {
    auto client = ApiClientFactory::createForProvider(APIProvider::FirstParty);
    REQUIRE(client != nullptr);
    REQUIRE(client->getProviderName() == "anthropic");
}

TEST_CASE("ApiClientFactory createForProvider with Bedrock creates BedrockClient", "[factory]") {
    auto client = ApiClientFactory::createForProvider(APIProvider::Bedrock);
    REQUIRE(client != nullptr);
    REQUIRE(client->getProviderName() == "bedrock");
    REQUIRE_FALSE(client->getModelName().empty());
}

TEST_CASE("ApiClientFactory createForProvider with Vertex creates VertexClient", "[factory]") {
    auto client = ApiClientFactory::createForProvider(APIProvider::Vertex);
    REQUIRE(client != nullptr);
    REQUIRE(client->getProviderName() == "vertex");
    REQUIRE_FALSE(client->getModelName().empty());
}

TEST_CASE("ApiClientFactory createForProvider with Foundry uses AnthropicClient as placeholder", "[factory]") {
    auto client = ApiClientFactory::createForProvider(APIProvider::Foundry);
    REQUIRE(client != nullptr);
    REQUIRE(client->getProviderName() == "anthropic");
}

TEST_CASE("ApiClientFactory providerDisplayName returns readable names", "[factory]") {
    REQUIRE(ApiClientFactory::providerDisplayName(APIProvider::FirstParty) == "Anthropic");
    REQUIRE(ApiClientFactory::providerDisplayName(APIProvider::Bedrock) == "AWS Bedrock");
    REQUIRE(ApiClientFactory::providerDisplayName(APIProvider::Vertex) == "Google Vertex AI");
    REQUIRE(ApiClientFactory::providerDisplayName(APIProvider::Foundry) == "Azure Foundry");
}

TEST_CASE("ApiClientFactory detectProvider returns Bedrock when env var set", "[factory]") {
    unsetenv("CLAUDE_CODE_USE_VERTEX");
    unsetenv("CLAUDE_CODE_USE_FOUNDRY");
    setenv("CLAUDE_CODE_USE_BEDROCK", "1", 1);

    APIProvider provider = ApiClientFactory::detectProvider();
    REQUIRE(provider == APIProvider::Bedrock);

    unsetenv("CLAUDE_CODE_USE_BEDROCK");
}

TEST_CASE("ApiClientFactory detectProvider returns Vertex when env var set", "[factory]") {
    unsetenv("CLAUDE_CODE_USE_BEDROCK");
    unsetenv("CLAUDE_CODE_USE_FOUNDRY");
    setenv("CLAUDE_CODE_USE_VERTEX", "1", 1);

    APIProvider provider = ApiClientFactory::detectProvider();
    REQUIRE(provider == APIProvider::Vertex);

    unsetenv("CLAUDE_CODE_USE_VERTEX");
}

TEST_CASE("ApiClientFactory detectProvider Bedrock takes priority over Vertex", "[factory]") {
    setenv("CLAUDE_CODE_USE_BEDROCK", "1", 1);
    setenv("CLAUDE_CODE_USE_VERTEX", "1", 1);

    APIProvider provider = ApiClientFactory::detectProvider();
    REQUIRE(provider == APIProvider::Bedrock);

    unsetenv("CLAUDE_CODE_USE_BEDROCK");
    unsetenv("CLAUDE_CODE_USE_VERTEX");
}
