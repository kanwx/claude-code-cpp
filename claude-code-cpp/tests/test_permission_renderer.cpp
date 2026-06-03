#ifdef HAS_FTXUI

#include <catch2/catch_test_macros.hpp>
#include <claude/ui/PermissionRendererRegistry.hpp>
#include <claude/ui/permissions/DefaultPermissionRenderer.hpp>

using namespace claude::ui;

TEST_CASE("PermissionRendererRegistry singleton", "[perm_renderer]") {
    auto& r1 = PermissionRendererRegistry::instance();
    auto& r2 = PermissionRendererRegistry::instance();
    REQUIRE(&r1 == &r2);
}

TEST_CASE("PermissionRendererRegistry returns fallback for unknown tool", "[perm_renderer]") {
    auto& registry = PermissionRendererRegistry::instance();
    auto* renderer = registry.getRenderer("NonExistentTool");
    REQUIRE(renderer != nullptr);
    REQUIRE(dynamic_cast<DefaultPermissionRenderer*>(renderer) != nullptr);
}

TEST_CASE("PermissionRendererRegistry returns registered renderer", "[perm_renderer]") {
    auto& registry = PermissionRendererRegistry::instance();
    auto custom = std::make_unique<DefaultPermissionRenderer>();
    auto* raw = custom.get();
    registry.registerRenderer("TestPermTool", std::move(custom));
    auto* result = registry.getRenderer("TestPermTool");
    REQUIRE(result == raw);
}

TEST_CASE("PermissionRendererRegistry fallback is never null", "[perm_renderer]") {
    auto& registry = PermissionRendererRegistry::instance();
    REQUIRE(registry.getFallbackRenderer() != nullptr);
}

#endif // HAS_FTXUI
