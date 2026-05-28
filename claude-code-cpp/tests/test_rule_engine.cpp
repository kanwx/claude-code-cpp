#include <catch2/catch_test_macros.hpp>
#include <claude/permission/RuleEngine.hpp>
#include <claude/permission/PermissionSettings.hpp>
#include <claude/permission/PermissionTypes.hpp>
#include <thread>
#include <vector>

using namespace claude;

TEST_CASE("RuleEngine default evaluation allows read-only", "[rule_engine]")
{
    PermissionSettings settings;
    RuleEngine engine(settings);

    Json input = {{"file_path", "/tmp/test.txt"}};
    auto decision = engine.evaluate("Read", input, /*isReadOnly=*/true);

    REQUIRE(decision.isAllowed());
}

TEST_CASE("RuleEngine deny rule", "[rule_engine]")
{
    PermissionSettings settings;
    settings.addRule(PermissionRule::forTool("Bash", PermissionBehavior::Deny));
    RuleEngine engine(settings);

    Json input = {{"command", "rm -rf /"}};
    auto decision = engine.evaluate("Bash", input, /*isReadOnly=*/false);

    REQUIRE(decision.isDenied());
}

TEST_CASE("RuleEngine allow rule", "[rule_engine]")
{
    PermissionSettings settings;
    settings.addRule(PermissionRule::forTool("Read", PermissionBehavior::Allow));
    RuleEngine engine(settings);

    Json input = {{"file_path", "/tmp/test.txt"}};
    auto decision = engine.evaluate("Read", input, /*isReadOnly=*/true);

    REQUIRE(decision.isAllowed());
}

TEST_CASE("RuleEngine concurrent evaluation", "[rule_engine]")
{
    PermissionSettings settings;
    settings.addRule(PermissionRule::forTool("Read", PermissionBehavior::Allow));
    settings.addRule(PermissionRule::forTool("Bash", PermissionBehavior::Deny));
    RuleEngine engine(settings);

    constexpr int kThreadCount = 4;
    std::vector<std::thread> threads;
    std::atomic<int> errorCount{0};

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&engine, &errorCount, i]() {
            try {
                for (int j = 0; j < 100; ++j) {
                    if (i % 2 == 0) {
                        Json input = {{"file_path", "/tmp/test.txt"}};
                        auto decision = engine.evaluate("Read", input, true);
                        if (!decision.isAllowed()) errorCount.fetch_add(1);
                    } else {
                        Json input = {{"command", "ls"}};
                        auto decision = engine.evaluate("Bash", input, false);
                        if (!decision.isDenied()) errorCount.fetch_add(1);
                    }
                }
            } catch (...) {
                errorCount.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(errorCount.load() == 0);
}
