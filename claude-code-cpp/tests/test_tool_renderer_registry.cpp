#include <catch2/catch_test_macros.hpp>

#ifdef HAS_FTXUI

#include <claude/ui/ToolRendererRegistry.hpp>
#include <claude/ui/UiMessageTypes.hpp>

using namespace claude::ui;
using claude::ToolUseBlock;
using claude::ToolResultBlock;

/// A minimal custom renderer for testing registration
class StubRenderer : public IToolRenderer {
public:
    ftxui::Element renderToolUse(const ToolUseBlock&, const RenderContext&) override {
        return ftxui::text("stub-use");
    }
    std::string renderToolUseAnsi(const ToolUseBlock&) override { return "stub-use-ansi"; }

    ftxui::Element renderToolResult(const ToolResultBlock&, const ToolUseBlock&,
                                     const RenderContext&) override {
        return ftxui::text("stub-result");
    }
    std::string renderToolResultAnsi(const ToolResultBlock&, const ToolUseBlock&) override {
        return "stub-result-ansi";
    }

    ftxui::Element renderToolError(const ToolResultBlock&, const ToolUseBlock&,
                                    const RenderContext&) override {
        return ftxui::text("stub-error");
    }
    std::string renderToolErrorAnsi(const ToolResultBlock&, const ToolUseBlock&) override {
        return "stub-error-ansi";
    }

    ftxui::Element renderToolRejected(const ToolUseBlock&, const RenderContext&) override {
        return ftxui::text("stub-rejected");
    }
    std::string renderToolRejectedAnsi(const ToolUseBlock&) override { return "stub-rejected-ansi"; }

    ftxui::Element renderToolCanceled(const ToolUseBlock&, const RenderContext&) override {
        return ftxui::text("stub-canceled");
    }
    std::string renderToolCanceledAnsi(const ToolUseBlock&) override { return "stub-canceled-ansi"; }

    ftxui::Element renderToolProgress(const ToolUseBlock&, const std::string&,
                                       const RenderContext&) override {
        return ftxui::text("stub-progress");
    }

    ftxui::Element renderToolQueued(const ToolUseBlock&, const RenderContext&) override {
        return ftxui::text("stub-queued");
    }

    ftxui::Element renderGroupedToolUse(const std::vector<ToolUseBlock>&,
                                         const RenderContext&) override {
        return ftxui::text("stub-grouped");
    }

    std::string getToolUseSummary(const ToolUseBlock&) override { return "stub-summary"; }
    std::string userFacingName(const ToolUseBlock&) override { return "StubTool"; }

    bool isCollapsible() const override { return true; }
    bool isResultTruncatable(const ToolResultBlock&) const override { return true; }
};

TEST_CASE("ToolRendererRegistry singleton returns same instance", "[tool_renderer_registry]") {
    auto& a = ToolRendererRegistry::instance();
    auto& b = ToolRendererRegistry::instance();
    REQUIRE(&a == &b);
}

TEST_CASE("ToolRendererRegistry returns fallback for unknown tool", "[tool_renderer_registry]") {
    auto& registry = ToolRendererRegistry::instance();
    auto* renderer = registry.getRenderer("NonExistentTool12345");
    REQUIRE(renderer != nullptr);
    // The fallback should be a DefaultToolRenderer
    auto* fallback = registry.getFallbackRenderer();
    REQUIRE(renderer == fallback);
}

TEST_CASE("ToolRendererRegistry returns registered renderer for known tool", "[tool_renderer_registry]") {
    auto& registry = ToolRendererRegistry::instance();
    auto stub = std::make_unique<StubRenderer>();
    auto* stubPtr = stub.get();

    registry.registerRenderer("StubTool", std::move(stub));
    auto* renderer = registry.getRenderer("StubTool");
    REQUIRE(renderer == stubPtr);

    // Verify it's the stub (not the fallback)
    REQUIRE(renderer->isCollapsible() == true);
    REQUIRE(renderer->isResultTruncatable(ToolResultBlock{}) == true);
}

TEST_CASE("ToolRendererRegistry fallback is never null", "[tool_renderer_registry]") {
    auto& registry = ToolRendererRegistry::instance();
    REQUIRE(registry.getFallbackRenderer() != nullptr);
}

#else

// When FTXUI is not available, provide a trivial passing test
TEST_CASE("ToolRendererRegistry requires HAS_FTXUI", "[tool_renderer_registry]") {
    REQUIRE(true);
}

#endif // HAS_FTXUI
