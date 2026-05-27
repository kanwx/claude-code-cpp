#include <catch2/catch_test_macros.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/impl/BashTool.hpp>
#include <thread>
#include <vector>

using namespace claude;

TEST_CASE("ToolRegistry basic operations", "[tool_registry]") {
    ToolRegistry registry;
    REQUIRE(registry.empty());
    REQUIRE(registry.size() == 0);

    registry.registerTool(std::make_unique<BashTool>());
    REQUIRE(!registry.empty());
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.has("Bash"));
    REQUIRE(registry.findByName("Bash") != nullptr);
    REQUIRE(!registry.has("NonExistent"));
}

TEST_CASE("ToolRegistry concurrent reads", "[tool_registry][concurrent]") {
    ToolRegistry registry;
    registry.registerBuiltinTools();
    const size_t N = 100;

    // Concurrent reads should not crash or deadlock
    std::vector<std::thread> readers;
    for (size_t i = 0; i < 8; ++i) {
        readers.emplace_back([&registry]() {
            for (size_t j = 0; j < N; ++j) {
                REQUIRE(registry.has("Bash"));
                registry.findByName("Bash");
                registry.getToolNames();
                registry.size();
            }
        });
    }
    for (auto& t : readers) t.join();
}

TEST_CASE("ToolRegistry concurrent writes", "[tool_registry][concurrent]") {
    ToolRegistry registry;
    registry.registerBuiltinTools();
    const size_t N = 100;

    // Concurrent writes to discoveredTools_ should not corrupt state
    std::vector<std::thread> writers;
    for (size_t i = 0; i < 4; ++i) {
        writers.emplace_back([&registry, i]() {
            for (size_t j = 0; j < N; ++j) {
                registry.markDiscovered("tool_" + std::to_string(i) + "_" + std::to_string(j));
            }
        });
    }
    for (auto& t : writers) t.join();

    // All discoveries should be recorded (set is deduplicated by name)
    const auto discovered = registry.getDiscoveredTools();
    REQUIRE(discovered.size() == 4 * N);
}

TEST_CASE("ToolRegistry concurrent read-write mix", "[tool_registry][concurrent]") {
    ToolRegistry registry;
    registry.registerBuiltinTools();
    const size_t N = 100;

    // Mix readers and writers to stress-test shared_mutex
    std::vector<std::thread> threads;

    // 4 readers
    for (size_t i = 0; i < 4; ++i) {
        threads.emplace_back([&registry]() {
            for (size_t j = 0; j < N; ++j) {
                registry.has("Bash");
                registry.findByName("Bash");
                registry.getToolNames();
                registry.toToolDefinitions();
                registry.size();
            }
        });
    }

    // 2 writers
    for (size_t i = 0; i < 2; ++i) {
        threads.emplace_back([&registry, i]() {
            for (size_t j = 0; j < N; ++j) {
                registry.markDiscovered("rw_tool_" + std::to_string(i) + "_" + std::to_string(j));
            }
        });
    }

    for (auto& t : threads) t.join();
}

TEST_CASE("ToolRegistry tool definitions serialization", "[tool_registry]") {
    ToolRegistry registry;
    registry.registerBuiltinTools();
    auto defs = registry.toToolDefinitions();
    REQUIRE(defs.size() > 0);
    for (const auto& d : defs) {
        REQUIRE_FALSE(d.name.empty());
        REQUIRE_FALSE(d.description.empty());
    }
}

TEST_CASE("ToolRegistry discovered tools tracking", "[tool_registry]") {
    ToolRegistry registry;
    registry.registerBuiltinTools();

    REQUIRE(registry.getDiscoveredTools().empty());
    REQUIRE_FALSE(registry.isDiscovered("Bash"));

    registry.markDiscovered("Bash");
    REQUIRE(registry.isDiscovered("Bash"));

    registry.markDiscovered(std::vector<String>{"Read", "Write"});
    REQUIRE(registry.isDiscovered("Read"));
    REQUIRE(registry.isDiscovered("Write"));

    registry.clearDiscovered();
    REQUIRE_FALSE(registry.isDiscovered("Bash"));
    REQUIRE(registry.getDiscoveredTools().empty());
}
