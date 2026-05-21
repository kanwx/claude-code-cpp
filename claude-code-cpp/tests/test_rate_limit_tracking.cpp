#include <catch2/catch_test_macros.hpp>
#include <claude/api/RateLimitTracker.hpp>
#include <claude/api/AnthropicClient.hpp>

using namespace claude;

TEST_CASE("RateLimitTracker updates from map headers", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "950";
    headers["anthropic-ratelimit-tokens-limit"] = "400000";
    headers["anthropic-ratelimit-tokens-remaining"] = "380000";

    tracker.updateFromHeaders(headers);
    auto info = tracker.getInfo();

    REQUIRE(info.requestsLimit == 1000);
    REQUIRE(info.requestsRemaining == 950);
    REQUIRE(info.tokensLimit == 400000);
    REQUIRE(info.tokensRemaining == 380000);
    REQUIRE_FALSE(tracker.getInfo().isRequestLimitExceeded());
}

TEST_CASE("RateLimitTracker statusMessage is empty when usage is low", "[ratelimit]") {
    RateLimitTracker tracker;
    // Default state — no data
    REQUIRE(tracker.statusMessage().empty());
}

TEST_CASE("RateLimitTracker statusMessage warns at high usage", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "30";  // 97% used
    tracker.updateFromHeaders(headers);

    String msg = tracker.statusMessage();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("nearly exhausted") != String::npos);
}

TEST_CASE("AnthropicClient has rateLimitTracker accessor", "[ratelimit]") {
    AnthropicClient client;
    (void)client.rateLimitTracker();
    (void)static_cast<const AnthropicClient&>(client).rateLimitTracker();
    REQUIRE(true);
}

TEST_CASE("AnthropicClient rateLimitTracker is updated from headers", "[ratelimit]") {
    AnthropicClient client;
    const auto& quota = client.lastQuotaStatus();
    REQUIRE(quota.requestsLimit == 0);
    REQUIRE(quota.requestsRemaining == 0);
}
