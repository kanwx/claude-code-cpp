#include <catch2/catch_test_macros.hpp>
#include <claude/core/AgentLoop.hpp>

using namespace claude;

TEST_CASE("AgentLoop stop hook fires on end_turn", "[agentloop]") {
    AgentLoop::StopHookResult result;
    result.shouldContinue = false;
    REQUIRE_FALSE(result.shouldContinue);
}

TEST_CASE("AgentLoop StopHookResult defaults to no continuation", "[agentloop]") {
    AgentLoop::StopHookResult result;
    REQUIRE_FALSE(result.shouldContinue);
    REQUIRE(result.reason.empty());
}

TEST_CASE("AgentLoop StopHookResult can force continuation", "[agentloop]") {
    AgentLoop::StopHookResult result;
    result.shouldContinue = true;
    result.reason = "Work not yet complete";
    REQUIRE(result.shouldContinue);
    REQUIRE(result.reason == "Work not yet complete");
}

TEST_CASE("AgentLoop OnStopHook callback type is invocable", "[agentloop]") {
    bool called = false;
    AgentLoop::OnStopHook hook = [&called]() -> AgentLoop::StopHookResult {
        called = true;
        return {true, "test reason"};
    };
    auto result = hook();
    REQUIRE(called);
    REQUIRE(result.shouldContinue);
    REQUIRE(result.reason == "test reason");
}
