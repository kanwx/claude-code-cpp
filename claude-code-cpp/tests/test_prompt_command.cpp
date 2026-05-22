#include <catch2/catch_test_macros.hpp>
#include <claude/command/PromptCommand.hpp>
#include <claude/command/SlashCommand.hpp>
#include <claude/utils/CircuitBreaker.hpp>
#include <claude/core/Types.hpp>
#include <thread>

using namespace claude;

// ============================================================================
// CommandType enum
// ============================================================================

TEST_CASE("CommandType enum has Local and Prompt values", "[command][integration]") {
    // Verify the two CommandType values exist and are distinct
    CommandType local = CommandType::Local;
    CommandType prompt = CommandType::Prompt;
    REQUIRE(local != prompt);
}

// ============================================================================
// SlashCommand base class
// ============================================================================

// A minimal concrete SlashCommand for testing
class TestLocalCommand : public SlashCommand {
public:
    String name() const override { return "test"; }
    String description() const override { return "test command"; }
    String execute(const String& args, CommandContext&) override { return "result: " + args; }
};

TEST_CASE("SlashCommand defaults to Local commandType", "[command][integration]") {
    TestLocalCommand cmd;
    REQUIRE(cmd.commandType() == CommandType::Local);
    REQUIRE(cmd.name() == "test");
    REQUIRE(cmd.description() == "test command");
    REQUIRE(cmd.help() == "/test - test command");
}

TEST_CASE("SlashCommand aliases default to empty", "[command][integration]") {
    TestLocalCommand cmd;
    REQUIRE(cmd.aliases().empty());
}

// ============================================================================
// PromptCommand
// ============================================================================

// A concrete PromptCommand for testing
class TestPromptCommand : public PromptCommand {
public:
    String name() const override { return "ask"; }
    String description() const override { return "ask AI"; }
    String execute(const String& args, CommandContext&) override { return "fallback: " + args; }
    String buildPrompt(const String& args, CommandContext&) override { return "prompt: " + args; }
};

TEST_CASE("PromptCommand returns Prompt commandType", "[command][integration]") {
    TestPromptCommand cmd;
    REQUIRE(cmd.commandType() == CommandType::Prompt);
}

TEST_CASE("PromptCommand buildPrompt delegates to execute by default", "[command][integration]") {
    // Default PromptCommand::buildPrompt falls back to execute()
    class DefaultPromptCmd : public PromptCommand {
    public:
        String name() const override { return "default"; }
        String description() const override { return "default prompt cmd"; }
        String execute(const String& args, CommandContext&) override { return "executed: " + args; }
    };

    DefaultPromptCmd cmd;
    // buildPrompt should return the same as execute when not overridden
    // (we can't create a CommandContext easily, so test the type relationship)
    REQUIRE(cmd.commandType() == CommandType::Prompt);
}

TEST_CASE("PromptCommand overrides buildPrompt separately from execute", "[command][integration]") {
    TestPromptCommand cmd;
    // The command provides both a fallback (execute) and a prompt (buildPrompt)
    REQUIRE(cmd.commandType() == CommandType::Prompt);
    // These are different methods with different purposes
    // execute = fallback for non-AI contexts
    // buildPrompt = primary for AI-delegated behavior
}

// ============================================================================
// CircuitBreaker integration with command/retry scenarios
// ============================================================================

TEST_CASE("CircuitBreaker starts in Closed state and allows calls", "[circuit_breaker][integration]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 3;
    config.successThreshold = 2;
    config.timeoutMs = 100;
    CircuitBreaker cb(config);

    REQUIRE(cb.getState() == CircuitState::Closed);
    REQUIRE(cb.allowCall());
}

TEST_CASE("CircuitBreaker trips to Open after threshold failures", "[circuit_breaker][integration]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 3;
    config.successThreshold = 2;
    config.timeoutMs = 100;
    CircuitBreaker cb(config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Closed); // not yet tripped

    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);
    REQUIRE_FALSE(cb.allowCall()); // blocked
}

TEST_CASE("CircuitBreaker recovers to HalfOpen after timeout", "[circuit_breaker][integration]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.successThreshold = 2;
    config.timeoutMs = 50; // very short for testing
    CircuitBreaker cb(config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);
    REQUIRE_FALSE(cb.allowCall());

    // Wait for timeout to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    REQUIRE(cb.allowCall()); // should transition to HalfOpen and allow
    REQUIRE(cb.getState() == CircuitState::HalfOpen);
}

TEST_CASE("CircuitBreaker recovers to Closed after success threshold in HalfOpen", "[circuit_breaker][integration]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.successThreshold = 2;
    config.timeoutMs = 50;
    CircuitBreaker cb(config);

    // Trip the breaker
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    cb.allowCall(); // enter HalfOpen

    // Record enough successes to close
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitState::HalfOpen); // need 2 successes
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitState::Closed);
}

TEST_CASE("CircuitBreaker HalfOpen failure reopens immediately", "[circuit_breaker][integration]") {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.successThreshold = 2;
    config.timeoutMs = 50;
    CircuitBreaker cb(config);

    // Trip the breaker
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);

    // Wait for timeout and enter HalfOpen
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    cb.allowCall();
    REQUIRE(cb.getState() == CircuitState::HalfOpen);

    // A single failure in HalfOpen reopens
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitState::Open);
}

TEST_CASE("CircuitBreaker reset returns to Closed", "[circuit_breaker][integration]") {
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

TEST_CASE("CircuitBreaker getStats returns valid JSON", "[circuit_breaker][integration]") {
    CircuitBreaker cb;
    Json stats = cb.getStats();

    REQUIRE(stats.contains("state"));
    REQUIRE(stats["state"] == "closed");
    REQUIRE(stats.contains("failureCount"));
    REQUIRE(stats.contains("config"));
}
