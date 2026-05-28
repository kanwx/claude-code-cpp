#include <catch2/catch_test_macros.hpp>
#include <claude/core/HookManager.hpp>
#include <atomic>
#include <thread>
#include <vector>

using namespace claude;

TEST_CASE("HookManager registration and execution", "[hook]") {
    HookManager mgr;
    std::atomic<int> counter{0};

    mgr.registerHook(HookType::PreToolUse, [&](HookContext&) -> HookResult {
        counter++;
        return HookResult::ok();
    });

    auto ctx = HookContext::forToolUse(HookType::PreToolUse, "Read", Json::object());
    auto result = mgr.execute(HookType::PreToolUse, ctx);

    REQUIRE(counter == 1);
    REQUIRE(result.shouldContinue());
}

TEST_CASE("HookManager abort hook", "[hook]") {
    HookManager mgr;

    mgr.registerHook(HookType::PreToolUse, [&](HookContext&) -> HookResult {
        return HookResult::abort("test");
    });

    auto ctx = HookContext::forToolUse(HookType::PreToolUse, "Write", Json::object());
    auto result = mgr.execute(HookType::PreToolUse, ctx);

    REQUIRE(result.shouldAbort());
    REQUIRE(result.reason == "test");
}

TEST_CASE("HookManager hasHooks", "[hook]") {
    HookManager mgr;

    REQUIRE_FALSE(mgr.hasHooks(HookType::PreToolUse));

    mgr.registerHook(HookType::PreToolUse, [&](HookContext&) -> HookResult {
        return HookResult::ok();
    });

    REQUIRE(mgr.hasHooks(HookType::PreToolUse));
}

TEST_CASE("HookManager clearHooks", "[hook]") {
    HookManager mgr;

    mgr.registerHook(HookType::PreToolUse, [&](HookContext&) -> HookResult {
        return HookResult::ok();
    });

    REQUIRE(mgr.hasHooks(HookType::PreToolUse));

    mgr.clearHooks(HookType::PreToolUse);

    REQUIRE_FALSE(mgr.hasHooks(HookType::PreToolUse));
}

TEST_CASE("HookManager concurrent access", "[hook][concurrency]") {
    HookManager mgr;
    constexpr int numThreads = 8;
    constexpr int iterations = 100;
    std::atomic<int> totalExecutions{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&mgr, &totalExecutions, iterations]() {
            for (int i = 0; i < iterations; ++i) {
                mgr.registerHook(HookType::PreToolUse, [&](HookContext&) -> HookResult {
                    totalExecutions++;
                    return HookResult::ok();
                });

                auto ctx = HookContext::forToolUse(HookType::PreToolUse, "Read", Json::object());
                mgr.execute(HookType::PreToolUse, ctx);

                mgr.hasHooks(HookType::PreToolUse);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // No crash means success; totalExecutions > 0 proves hooks actually ran
    REQUIRE(totalExecutions > 0);
}
