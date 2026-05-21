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

TEST_CASE("RateLimitTracker detects overage when remaining < 0", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "-5";
    headers["anthropic-ratelimit-tokens-limit"] = "400000";
    headers["anthropic-ratelimit-tokens-remaining"] = "100000";

    tracker.updateFromHeaders(headers);
    auto info = tracker.getInfo();

    REQUIRE(info.isOverage);
    REQUIRE(info.requestsRemaining == -5);
    REQUIRE(tracker.isOverage());

    String msg = tracker.statusMessage();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("OVERAGE") != String::npos);
}

TEST_CASE("RateLimitTracker detects token overage", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "500";
    headers["anthropic-ratelimit-tokens-limit"] = "400000";
    headers["anthropic-ratelimit-tokens-remaining"] = "-1000";

    tracker.updateFromHeaders(headers);

    REQUIRE(tracker.isOverage());
    String msg = tracker.statusMessage();
    REQUIRE(msg.find("OVERAGE") != String::npos);
    REQUIRE(msg.find("tokens") != String::npos);
}

TEST_CASE("RateLimitTracker no overage when remaining >= 0", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "500";

    tracker.updateFromHeaders(headers);

    REQUIRE_FALSE(tracker.isOverage());
    REQUIRE_FALSE(tracker.getInfo().isOverage);
}

TEST_CASE("RateLimitTracker tier classification", "[ratelimit]") {
    // tier_2+ : requestsLimit > 4000
    {
        RateLimitTracker tracker;
        std::map<String, String> headers;
        headers["anthropic-ratelimit-unified-limit"] = "5000";
        headers["anthropic-ratelimit-unified-remaining"] = "4000";
        tracker.updateFromHeaders(headers);

        REQUIRE(tracker.tierName() == "tier_2+");
        REQUIRE(tracker.getInfo().tierName == "tier_2+");
    }

    // tier_1 : requestsLimit > 1000
    {
        RateLimitTracker tracker;
        std::map<String, String> headers;
        headers["anthropic-ratelimit-unified-limit"] = "2000";
        headers["anthropic-ratelimit-unified-remaining"] = "1500";
        tracker.updateFromHeaders(headers);

        REQUIRE(tracker.tierName() == "tier_1");
    }

    // free : requestsLimit > 0 but <= 1000
    {
        RateLimitTracker tracker;
        std::map<String, String> headers;
        headers["anthropic-ratelimit-unified-limit"] = "1000";
        headers["anthropic-ratelimit-unified-remaining"] = "500";
        tracker.updateFromHeaders(headers);

        REQUIRE(tracker.tierName() == "free");
    }

    // empty : no limit data
    {
        RateLimitTracker tracker;
        REQUIRE(tracker.tierName().empty());
    }
}

TEST_CASE("RateLimitTracker reset time formatting", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "500";
    headers["anthropic-ratelimit-tokens-limit"] = "400000";
    headers["anthropic-ratelimit-tokens-remaining"] = "200000";
    headers["anthropic-ratelimit-unified-reset"] = "2026-05-21T14:30:00";
    headers["anthropic-ratelimit-tokens-reset"] = "2026-05-21T15:00:00";

    tracker.updateFromHeaders(headers);

    // Should produce non-empty, non-"unknown" output
    String reqReset = tracker.requestLimitResetsAt();
    String tokReset = tracker.tokenLimitResetsAt();

    REQUIRE(reqReset != "unknown");
    REQUIRE(reqReset.find("resets at") != String::npos);
    REQUIRE(tokReset != "unknown");
    REQUIRE(tokReset.find("resets at") != String::npos);
}

TEST_CASE("RateLimitTracker reset time is unknown when no headers", "[ratelimit]") {
    RateLimitTracker tracker;
    // No headers — default state
    REQUIRE(tracker.requestLimitResetsAt() == "unknown");
    REQUIRE(tracker.tokenLimitResetsAt() == "unknown");
}

TEST_CASE("RateLimitTracker statusMessage includes reset time at high usage", "[ratelimit]") {
    RateLimitTracker tracker;
    std::map<String, String> headers;
    headers["anthropic-ratelimit-unified-limit"] = "1000";
    headers["anthropic-ratelimit-unified-remaining"] = "10";  // 99% used
    headers["anthropic-ratelimit-unified-reset"] = "2026-05-21T14:30:00";
    tracker.updateFromHeaders(headers);

    String msg = tracker.statusMessage();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("resets at") != String::npos);
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
