#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <claude/services/CostTracker.hpp>

using namespace claude;

TEST_CASE("CostTracker calculates cost with cache tokens", "[cost]") {
    auto cost = CostTracker::calculateCostWithCache(
        "claude-sonnet-4-6", 1000, 500, 800, 200, 0);
    REQUIRE(cost > 0.0);
}

TEST_CASE("CostTracker cache savings with no cache", "[cost]") {
    CostTracker& tracker = CostTracker::instance();
    tracker.reset();

    tracker.recordUsageWithCache("claude-sonnet-4-6", 1000, 500, 0, 0, 0, 0.006);

    auto savings = tracker.getCacheSavings();
    REQUIRE(savings.totalCacheReadTokens == 0);
    REQUIRE(savings.savingsPercent == 0.0);
}

TEST_CASE("CostTracker cache savings with cache reads", "[cost]") {
    CostTracker& tracker = CostTracker::instance();
    tracker.reset();

    // 500 input tokens + 500 cache read tokens = 1000 total input, 50% from cache
    tracker.recordUsageWithCache("claude-sonnet-4-6", 500, 100, 500, 0, 0, 0.003);

    auto savings = tracker.getCacheSavings();
    REQUIRE(savings.totalCacheReadTokens == 500);
    REQUIRE(savings.totalInputTokens == 1000); // input + cache read
    REQUIRE_THAT(savings.savingsPercent, Catch::Matchers::WithinAbs(50.0, 1.0));

    // Cache read at $0.30/Mtok vs full input at $3.00/Mtok = $0.00135 saved
    REQUIRE(savings.cacheReadCostSaved > 0.0);
}

TEST_CASE("CostTracker cache savings with cache writes", "[cost]") {
    CostTracker& tracker = CostTracker::instance();
    tracker.reset();

    tracker.recordUsageWithCache("claude-sonnet-4-6", 500, 100, 0, 300, 0, 0.003);

    auto savings = tracker.getCacheSavings();
    REQUIRE(savings.totalCacheWriteTokens == 300);
    REQUIRE(savings.cacheWriteCost > 0.0);
}

TEST_CASE("CostTracker formatCacheSavings returns empty with no usage", "[cost]") {
    CostTracker& tracker = CostTracker::instance();
    tracker.reset();
    REQUIRE(tracker.formatCacheSavings().empty());
}

TEST_CASE("CostTracker formatCacheSavings shows savings", "[cost]") {
    CostTracker& tracker = CostTracker::instance();
    tracker.reset();

    tracker.recordUsageWithCache("claude-sonnet-4-6", 500, 100, 800, 200, 0, 0.005);

    auto formatted = tracker.formatCacheSavings();
    REQUIRE_FALSE(formatted.empty());
    REQUIRE(formatted.find("cache") != String::npos);
}

TEST_CASE("CostTracker canonicalizeModelName", "[cost]") {
    REQUIRE(CostTracker::canonicalizeModelName("claude-sonnet-4-6-20250514") == "claude-sonnet-4-6");
    REQUIRE(CostTracker::canonicalizeModelName("claude-opus-4-5-20250415") == "claude-opus-4-5");
    REQUIRE(CostTracker::canonicalizeModelName("sonnet") == "sonnet");
}

TEST_CASE("CostTracker formatCost", "[cost]") {
    auto high = CostTracker::formatCost(1.50);
    REQUIRE(high.find("$1.50") != String::npos);

    auto low = CostTracker::formatCost(0.005);
    REQUIRE(low.find("$0.005") != String::npos);
}
