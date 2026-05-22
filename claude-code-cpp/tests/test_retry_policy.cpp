#include <catch2/catch_test_macros.hpp>
#include <claude/api/RetryPolicy.hpp>

using namespace claude;

// ============================================================================
// Error classification
// ============================================================================

TEST_CASE("RetryPolicy classifies rate limit errors", "[retry][integration]") {
    auto error = RetryPolicy::classifyError(429, "{}");
    REQUIRE(error.type == ApiErrorType::RateLimit);
    REQUIRE(error.isRetryable());
}

TEST_CASE("RetryPolicy classifies auth errors as non-retryable", "[retry][integration]") {
    auto error = RetryPolicy::classifyError(401, "unauthorized");
    REQUIRE(error.type == ApiErrorType::AuthError);
    REQUIRE_FALSE(error.isRetryable());
}

TEST_CASE("RetryPolicy classifies 403 as auth error", "[retry][integration]") {
    auto error = RetryPolicy::classifyError(403, "forbidden");
    REQUIRE(error.type == ApiErrorType::AuthError);
    REQUIRE_FALSE(error.isRetryable());
}

TEST_CASE("RetryPolicy classifies server errors as retryable", "[retry][integration]") {
    SECTION("503 service unavailable") {
        auto error = RetryPolicy::classifyError(503, "service unavailable");
        REQUIRE(error.type == ApiErrorType::ServerError);
        REQUIRE(error.isRetryable());
    }
    SECTION("500 internal server error") {
        auto error = RetryPolicy::classifyError(500, "internal error");
        REQUIRE(error.type == ApiErrorType::ServerError);
        REQUIRE(error.isRetryable());
    }
    SECTION("502 bad gateway") {
        auto error = RetryPolicy::classifyError(502, "bad gateway");
        REQUIRE(error.type == ApiErrorType::ServerError);
        REQUIRE(error.isRetryable());
    }
}

TEST_CASE("RetryPolicy classifies 404 as client error (non-retryable)", "[retry][integration]") {
    auto error = RetryPolicy::classifyError(404, "not found");
    REQUIRE(error.type == ApiErrorType::ClientError);
    REQUIRE_FALSE(error.isRetryable());
}

TEST_CASE("RetryPolicy classifies 413 as PromptTooLong", "[retry][integration]") {
    auto error = RetryPolicy::classifyError(413, "");
    REQUIRE(error.type == ApiErrorType::PromptTooLong);
    REQUIRE_FALSE(error.isRetryable());
}

TEST_CASE("RetryPolicy classifies 400 content filtered", "[retry][integration]") {
    auto error = RetryPolicy::classifyError(400, "content_filter violation");
    REQUIRE(error.type == ApiErrorType::ContentFiltered);
    REQUIRE_FALSE(error.isRetryable());
}

TEST_CASE("RetryPolicy classifies success codes as None", "[retry][integration]") {
    SECTION("200") {
        auto error = RetryPolicy::classifyError(200, "");
        REQUIRE(error.type == ApiErrorType::None);
    }
    SECTION("201") {
        auto error = RetryPolicy::classifyError(201, "");
        REQUIRE(error.type == ApiErrorType::None);
    }
}

// ============================================================================
// Retry-After header parsing
// ============================================================================

TEST_CASE("RetryPolicy parses Retry-After from headers", "[retry][integration]") {
    std::map<String, String> headers = {{"retry-after", "30"}};
    auto error = RetryPolicy::classifyError(429, "{}", headers);
    REQUIRE(error.retryAfter == 30);
}

TEST_CASE("RetryPolicy Retry-From headers overrides body", "[retry][integration]") {
    // Header takes precedence over body parsing
    std::map<String, String> headers = {{"retry-after", "20"}};
    auto error = RetryPolicy::classifyError(429, R"({"retry_after": 10})", headers);
    REQUIRE(error.retryAfter == 20); // header wins
}

// ============================================================================
// Network error classification
// ============================================================================

TEST_CASE("RetryPolicy detects stale connections (ECONNRESET)", "[retry][integration]") {
    auto error = RetryPolicy::classifyNetworkError("ECONNRESET: Connection reset by peer");
    REQUIRE(error.type == ApiErrorType::NetworkError);
    REQUIRE(error.isRetryable());
}

TEST_CASE("RetryPolicy detects broken pipe (EPIPE)", "[retry][integration]") {
    auto error = RetryPolicy::classifyNetworkError("EPIPE: Broken pipe");
    REQUIRE(error.type == ApiErrorType::NetworkError);
    REQUIRE(error.isRetryable());
}

TEST_CASE("RetryPolicy detects timeout errors", "[retry][integration]") {
    auto error = RetryPolicy::classifyNetworkError("Connection timed out after 30000ms");
    REQUIRE(error.type == ApiErrorType::Timeout);
    REQUIRE(error.isRetryable());
}

// ============================================================================
// Backoff calculation
// ============================================================================

TEST_CASE("RetryPolicy calculates exponential backoff delay", "[retry][integration]") {
    RetryPolicy policy;
    ApiError error;
    error.type = ApiErrorType::ServerError;

    int delay1 = policy.calculateDelay(error, 1);
    int delay2 = policy.calculateDelay(error, 2);

    // With default config (backoffMultiplier=2.0, jitterFactor=0.1):
    // attempt 1: 1000ms base, attempt 2: 2000ms base (before jitter)
    // With jitter, delay2 should be roughly 2x delay1, but jitter makes exact comparison unreliable
    // So we verify the general relationship: delay2 is significantly larger
    REQUIRE(delay2 > delay1);
}

TEST_CASE("RetryPolicy rate limit uses Retry-After when available", "[retry][integration]") {
    RetryPolicy policy;
    ApiError error;
    error.type = ApiErrorType::RateLimit;
    error.retryAfter = 30; // 30 seconds

    int delay = policy.calculateDelay(error, 1);
    REQUIRE(delay == 30000); // 30 seconds in ms
}

TEST_CASE("RetryPolicy network error uses shorter base delay", "[retry][integration]") {
    RetryPolicy policy;
    ApiError networkError;
    networkError.type = ApiErrorType::NetworkError;

    ApiError serverError;
    serverError.type = ApiErrorType::ServerError;

    // At same attempt number, network errors should use shorter base delay (500ms vs 1000ms)
    int networkDelay = policy.calculateDelay(networkError, 1);
    int serverDelay = policy.calculateDelay(serverError, 1);

    // Network delay should be roughly half of server delay (before jitter)
    // With jitter, we verify it's less
    REQUIRE(networkDelay < serverDelay);
}

// ============================================================================
// shouldRetry logic
// ============================================================================

TEST_CASE("RetryPolicy shouldRetry respects max retries", "[retry][integration]") {
    RetryPolicy policy;
    ApiError retryableError;
    retryableError.type = ApiErrorType::ServerError;

    // Default maxRetries=3, attempts 0..2 should retry
    REQUIRE(policy.shouldRetry(retryableError, 0));
    REQUIRE(policy.shouldRetry(retryableError, 1));
    REQUIRE(policy.shouldRetry(retryableError, 2));
    REQUIRE_FALSE(policy.shouldRetry(retryableError, 3)); // at max
}

TEST_CASE("RetryPolicy shouldRetry rejects non-retryable errors", "[retry][integration]") {
    RetryPolicy policy;
    ApiError authError;
    authError.type = ApiErrorType::AuthError;

    REQUIRE_FALSE(policy.shouldRetry(authError, 0));
    REQUIRE_FALSE(policy.shouldRetry(authError, 1));
}

// ============================================================================
// Full retry flow simulation
// ============================================================================

TEST_CASE("RetryPolicy full retry flow for server error", "[retry][integration]") {
    RetryPolicy policy;
    auto error = RetryPolicy::classifyError(503, "service unavailable");

    REQUIRE(error.isRetryable());

    int totalDelay = 0;
    int attempt = 0;
    while (policy.shouldRetry(error, attempt)) {
        totalDelay += policy.calculateDelay(error, attempt + 1);
        attempt++;
    }

    // Should have attempted maxRetries times (3 by default)
    REQUIRE(attempt == 3);
    // Total delay should be positive and accumulated
    REQUIRE(totalDelay > 0);
}
