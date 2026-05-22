#include <catch2/catch_test_macros.hpp>
#include <claude/utils/CircuitBreaker.hpp>
#include <thread>
#include <chrono>

using namespace claude;

// ============================================================================
// CircuitBreaker state transitions
// ============================================================================

TEST_CASE("CircuitBreaker starts in Closed state", "[circuit_breaker]") {
    CircuitBreaker cb;
    REQUIRE(cb.getState() == CircuitState::Closed);
    REQUIRE(cb.allowCall());
}

TEST_CASE("CircuitBreaker stays Closed below failure threshold", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 5;
    CircuitBreaker cb(config);

    for (int i = 0; i < 4; ++i) {
        cb.recordFailure();
        REQUIRE(cb.getState() == CircuitState::Closed);
    }
    REQUIRE(cb.allowCall());
}

TEST_CASE("CircuitBreaker opens at failure threshold", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 3;
    CircuitBreaker cb(config);

    for (int i = 0; i < 3; ++i) {
        cb.recordFailure();
    }
    REQUIRE(cb.getState() == CircuitState::Open);
    REQUIRE_FALSE(cb.allowCall());
}

TEST_CASE("CircuitBreaker Open state blocks all calls", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.timeoutMs = 5000;
    CircuitBreaker cb(config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    // All calls should be blocked while Open
    for (int i = 0; i < 10; ++i) {
        REQUIRE_FALSE(cb.allowCall());
    }
}

TEST_CASE("CircuitBreaker transitions to HalfOpen after timeout", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.timeoutMs = 50; // 50ms for fast test
    CircuitBreaker cb(config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    REQUIRE(cb.allowCall());
    REQUIRE(cb.getState() == CircuitState::HalfOpen);
}

TEST_CASE("CircuitBreaker HalfOpen -> Closed after success threshold", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.successThreshold = 2;
    config.timeoutMs = 50;
    CircuitBreaker cb(config);

    // Open the breaker
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    // Wait for HalfOpen
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    REQUIRE(cb.allowCall());
    REQUIRE(cb.getState() == CircuitState::HalfOpen);

    // Record successes to close
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitState::HalfOpen);
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitState::Closed);
    REQUIRE(cb.allowCall());
}

TEST_CASE("CircuitBreaker HalfOpen -> Open on failure", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.timeoutMs = 50;
    CircuitBreaker cb(config);

    // Open the breaker
    cb.recordFailure();
    cb.recordFailure();

    // Wait for HalfOpen
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    REQUIRE(cb.allowCall());

    // Failure in HalfOpen immediately re-opens
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);
    REQUIRE_FALSE(cb.allowCall());
}

TEST_CASE("CircuitBreaker success in Closed resets failure count", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 5;
    CircuitBreaker cb(config);

    // Accumulate 4 failures (one short of threshold)
    for (int i = 0; i < 4; ++i) cb.recordFailure();

    // A success resets the counter
    cb.recordSuccess();

    // Now 4 more failures should NOT open (counter was reset)
    for (int i = 0; i < 4; ++i) cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Closed);
}

TEST_CASE("CircuitBreaker reset returns to Closed", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    CircuitBreaker cb(config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    cb.reset();
    REQUIRE(cb.getState() == CircuitState::Closed);
    REQUIRE(cb.allowCall());
}

TEST_CASE("CircuitBreaker getStats returns valid JSON", "[circuit_breaker]") {
    CircuitBreaker cb;
    auto stats = cb.getStats();
    REQUIRE(stats.is_object());
    REQUIRE(stats.contains("state"));
    REQUIRE(stats["state"].get<String>() == "closed");
    REQUIRE(stats.contains("config"));
}

TEST_CASE("CircuitBreaker getStateString", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.timeoutMs = 50;
    CircuitBreaker cb(config);

    REQUIRE(cb.getStateString() == "closed");

    cb.recordFailure();
    REQUIRE(cb.getStateString() == "open");

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    cb.allowCall();
    REQUIRE(cb.getStateString() == "half-open");
}

// ============================================================================
// HalfOpen max calls limit
// ============================================================================

TEST_CASE("CircuitBreaker HalfOpen limits concurrent calls", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.timeoutMs = 50;
    config.halfOpenMaxCalls = 2;
    CircuitBreaker cb(config);

    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // First two calls allowed in HalfOpen
    REQUIRE(cb.allowCall());
    REQUIRE(cb.allowCall());
    // Third call blocked
    REQUIRE_FALSE(cb.allowCall());
}
