#include <catch2/catch_test_macros.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <thread>
#include <vector>

using namespace claude;

TEST_CASE("AnthropicClient construction and config", "[anthropic][config]")
{
    AnthropicClient client("test-api-key");

    SECTION("provider name is anthropic")
    {
        REQUIRE(client.getProviderName() == "anthropic");
    }

    SECTION("setters do not crash")
    {
        REQUIRE_NOTHROW(client.setModel("claude-opus-4-20250514"));
        REQUIRE_NOTHROW(client.setBaseUrl("https://custom.api.com/v1"));
        REQUIRE_NOTHROW(client.setApiKey("replacement-key"));
        REQUIRE_NOTHROW(client.setMaxTokens(8192));
        REQUIRE_NOTHROW(client.setTemperature(0.7));
    }

    SECTION("model name reflects setModel")
    {
        client.setModel("claude-opus-4-20250514");
        REQUIRE(client.getModelName() == "claude-opus-4-20250514");
    }
}

TEST_CASE("AnthropicClient config concurrent access", "[anthropic][thread-safety]")
{
    AnthropicClient client("test-api-key");

    constexpr int kThreads = 4;
    constexpr int kIterations = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&client, t]() {
            for (int i = 0; i < kIterations; ++i) {
                client.setModel("model-thread-" + std::to_string(t) + "-iter-" + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // If we reach here without crash or data-race sanitizer error, the test passes.
    // Verify the object is still in a valid state.
    REQUIRE_NOTHROW(client.getModelName());
    REQUIRE(client.getProviderName() == "anthropic");
}

TEST_CASE("AnthropicClient fallback state", "[anthropic][fallback]")
{
    AnthropicClient client("test-api-key");

    SECTION("initial didFallBackToNonStreaming is false")
    {
        REQUIRE_FALSE(client.didFallBackToNonStreaming());
    }
}
