#include <catch2/catch_test_macros.hpp>
#include <claude/api/RetryPolicy.hpp>

using namespace claude;

TEST_CASE("413 classified as PromptTooLong", "[retry]") {
    auto error = RetryPolicy::classifyError(413, "");
    REQUIRE(error.type == ApiErrorType::PromptTooLong);
}

TEST_CASE("413 with token count info extracted", "[retry]") {
    String body = R"({"error":{"type":"error","message":"prompt is too long: 250000 tokens > 200000 maximum context"}})";
    auto error = RetryPolicy::classifyError(413, body);
    REQUIRE(error.type == ApiErrorType::PromptTooLong);
    REQUIRE(error.tokenGap > 0);
}

TEST_CASE("413 is not retryable by default", "[retry]") {
    auto error = RetryPolicy::classifyError(413, "");
    REQUIRE_FALSE(error.isRetryable());
}

TEST_CASE("PromptTooLong error has tokenGap field", "[retry]") {
    ApiError error;
    error.type = ApiErrorType::PromptTooLong;
    error.tokenGap = 50000;
    REQUIRE(error.tokenGap == 50000);
}
