#include <catch2/catch_test_macros.hpp>
#include <claude/api/BetaHeaders.hpp>
#include <claude/api/AnthropicClient.hpp>

using namespace claude::api;
using claude::AnthropicClient;

TEST_CASE("BetaHeaders default list", "[beta_headers]") {
    auto betas = BetaHeaders::getDefault();
    REQUIRE_FALSE(betas.empty());
    REQUIRE(std::find(betas.begin(), betas.end(),
                      BetaHeaders::PROMPT_CACHING) != betas.end());
}

TEST_CASE("BetaHeaders buildHeaderString", "[beta_headers]") {
    std::vector<claude::String> betas = {
        "prompt-caching-2024-07-31",
        "max-tokens-3-5-2024-07-31"
    };
    REQUIRE(BetaHeaders::buildHeaderString(betas) ==
            "prompt-caching-2024-07-31,max-tokens-3-5-2024-07-31");
}

TEST_CASE("BetaHeaders empty list returns empty string", "[beta_headers]") {
    std::vector<claude::String> betas;
    REQUIRE(BetaHeaders::buildHeaderString(betas) == "");
}

TEST_CASE("BetaHeaders for extended thinking", "[beta_headers]") {
    auto betas = BetaHeaders::forExtendedThinking();
    REQUIRE(std::find(betas.begin(), betas.end(),
                      BetaHeaders::REDACTED_THINKING) != betas.end());
    // Also contains defaults
    REQUIRE(std::find(betas.begin(), betas.end(),
                      BetaHeaders::PROMPT_CACHING) != betas.end());
}

TEST_CASE("BetaHeaders for context management", "[beta_headers]") {
    auto betas = BetaHeaders::forContextManagement();
    REQUIRE(std::find(betas.begin(), betas.end(),
                      BetaHeaders::CONTEXT_MANAGEMENT) != betas.end());
    // Also contains defaults
    REQUIRE(std::find(betas.begin(), betas.end(),
                      BetaHeaders::PROMPT_CACHING) != betas.end());
}

TEST_CASE("AnthropicClient has beta headers member", "[beta]") {
    AnthropicClient client("test-key");
    REQUIRE_FALSE(client.getBetaHeaders().empty());
}

TEST_CASE("AnthropicClient setBetaHeaders", "[beta]") {
    AnthropicClient client("test-key");
    std::vector<claude::String> custom = {"custom-beta-2025-01-01"};
    client.setBetaHeaders(custom);
    REQUIRE(client.getBetaHeaders().size() == 1);
    REQUIRE(client.getBetaHeaders()[0] == "custom-beta-2025-01-01");
}
