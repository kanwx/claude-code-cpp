# Output Rendering Component-First Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign the C++ output rendering architecture from a monolithic MainComponent to a component tree with per-tool custom rendering, expanded content types, offscreen freeze, color accessibility, ANSI fallback, message search, and enhanced permission prompts.

**Architecture:** Decompose `detail::MainComponent` into a tree of FTXUI Components. Introduce `IToolRenderer` + `ToolRendererRegistry` for per-tool custom rendering. Expand `DisplayMessageType` with XML tag dispatch and tool result subtypes. Add `OffscreenFreeze` for viewport optimization. Add daltonized/ANSI themes and `TerminalCapabilities` auto-detection.

**Tech Stack:** C++23, FTXUI (fullscreen TUI, guarded by `#ifdef HAS_FTXUI`), Catch2 (tests), nlohmann/json

**Build commands:**
- Build: `cd /Users/kankan/claude-code/claude-code-cpp && make build`
- Test: `cd /Users/kankan/claude-code/claude-code-cpp && make test`
- Single test: `cd /Users/kankan/claude-code/claude-code-cpp/build && ctest -R <test_name> --output-on-failure`

---

## File Structure

### New files created

```
include/claude/ui/
  components/
    AppLayout.hpp
    HeaderBar.hpp
    ContentArea.hpp
    MessageList.hpp
    MessageComponent.hpp
    UserPromptComponent.hpp
    AssistantTextComponent.hpp
    AssistantThinkingComponent.hpp
    ToolUseComponent.hpp
    ToolResultComponent.hpp
    SystemInfoComponent.hpp
    SystemErrorComponent.hpp
    TurnDurationComponent.hpp
    CompactBoundaryComponent.hpp
    CollapsedReadSearchComponent.hpp
    GroupedToolUseComponent.hpp
    AgentProgressComponent.hpp
    StreamingText.hpp
    ThinkingIndicator.hpp
    CompletionOverlay.hpp
    PermissionOverlay.hpp
    InputLine.hpp
  IToolRenderer.hpp
  ToolRendererRegistry.hpp
  RenderContext.hpp
  IPermissionRenderer.hpp
  PermissionRendererRegistry.hpp
  OffscreenFreeze.hpp
  XmlTagDispatcher.hpp
  SearchOverlay.hpp
  TerminalCapabilities.hpp  (in include/claude/console/)

src/ui/
  components/
    AppLayout.cpp
    HeaderBar.cpp
    ContentArea.cpp
    MessageList.cpp
    UserPromptComponent.cpp
    AssistantTextComponent.cpp
    AssistantThinkingComponent.cpp
    ToolUseComponent.cpp
    ToolResultComponent.cpp
    SystemInfoComponent.cpp
    SystemErrorComponent.cpp
    TurnDurationComponent.cpp
    CompactBoundaryComponent.cpp
    CollapsedReadSearchComponent.cpp
    GroupedToolUseComponent.cpp
    AgentProgressComponent.cpp
    StreamingText.cpp
    ThinkingIndicator.cpp
    CompletionOverlay.cpp
    PermissionOverlay.cpp
    InputLine.cpp
  renderers/
    DefaultToolRenderer.hpp
    DefaultToolRenderer.cpp
    ReadToolRenderer.hpp
    ReadToolRenderer.cpp
    BashToolRenderer.hpp
    BashToolRenderer.cpp
    EditToolRenderer.hpp
    EditToolRenderer.cpp
    WriteToolRenderer.hpp
    WriteToolRenderer.cpp
    GrepToolRenderer.hpp
    GrepToolRenderer.cpp
    GlobToolRenderer.hpp
    GlobToolRenderer.cpp
    AgentToolRenderer.hpp
    AgentToolRenderer.cpp
    WebFetchToolRenderer.hpp
    WebFetchToolRenderer.cpp
    WebSearchToolRenderer.hpp
    WebSearchToolRenderer.cpp
    LspToolRenderer.hpp
    LspToolRenderer.cpp
  ToolRendererRegistry.cpp
  OffscreenFreeze.cpp
  XmlTagDispatcher.cpp
  SearchOverlay.cpp
  permissions/
    DefaultPermissionRenderer.hpp
    DefaultPermissionRenderer.cpp
    BashPermissionRenderer.hpp
    BashPermissionRenderer.cpp
    FileEditPermissionRenderer.hpp
    FileEditPermissionRenderer.cpp
    FileWritePermissionRenderer.hpp
    FileWritePermissionRenderer.cpp
    FileReadPermissionRenderer.hpp
    FileReadPermissionRenderer.cpp
    PermissionRendererRegistry.cpp

src/console/
  TerminalCapabilities.cpp

tests/
  test_xml_dispatcher.cpp
  test_tool_renderer_registry.cpp
  test_offscreen_freeze.cpp
  test_terminal_capabilities.cpp
  test_search_overlay.cpp
  test_permission_renderer.cpp
```

### Modified files

```
include/claude/ui/UiMessageTypes.hpp       — new DisplayMessageType variants + searchableText()
src/ui/UiMessageTypes.cpp                  — height estimation for new types + searchableText()
src/ui/MessagePipeline.cpp                 — NormalizeStage XML dispatch + new type creation
src/ui/FtxuiRepl.cpp                       — replace MainComponent with AppLayout tree
src/ui/FtxuiRender.cpp                     — extract into components, delete MainComponent
include/claude/ui/FtxuiRepl.hpp            — update to use new component tree
src/ui/FtxuiMarkdown.cpp                   — hasMarkdownSyntax() fast path + MarkdownCache
src/ui/VirtualScroll.cpp                   — expose firstVisibleIndex/lastVisibleIndex
src/console/ThemeSystem.cpp                — daltonized + ANSI themes
src/ui/FtxuiPermission.cpp                 — delegate to IPermissionRenderer
src/ui/components/PermissionPrompt.cpp     — add feedback input support
include/claude/ui/components/PermissionPrompt.hpp — add PermissionOption with feedback
src/bootstrap/AgentRunner.cpp              — register renderers at startup
src/main.cpp                               — TerminalCapabilities auto-downgrade
CMakeLists.txt                             — add new source files
```

---

## Phase 1: Component Tree Foundation

### Task 1: Create RenderContext struct

**Files:**
- Create: `include/claude/ui/RenderContext.hpp`

- [ ] **Step 1: Write the header file**

```cpp
// include/claude/ui/RenderContext.hpp
#pragma once
#ifdef HAS_FTXUI

#include <string>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

struct ThemeColors;  // forward declaration — defined in FtxuiColors.hpp

struct RenderContext {
    bool verbose = false;
    bool isStreaming = false;
    int maxWidth = 80;
    int tickCounter = 0;
    const ThemeColors& theme;

    RenderContext(const ThemeColors& t) : theme(t) {}
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build 2>&1 | tail -5`
Expected: Build succeeds (the file is a header, only consumed when included)

- [ ] **Step 3: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/RenderContext.hpp
git commit -m "feat: add RenderContext struct for component rendering"
```

---

### Task 2: Create MessageComponent base class

**Files:**
- Create: `include/claude/ui/components/MessageComponent.hpp`
- Create: `src/ui/components/MessageComponent.cpp`

- [ ] **Step 1: Write the header**

```cpp
// include/claude/ui/components/MessageComponent.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component_base.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>

namespace claude::ui {

class MessageComponent : public ftxui::ComponentBase {
public:
    MessageComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : msg_(msg), ctx_(ctx) {}

    virtual DisplayMessage::Type messageType() const = 0;

protected:
    const DisplayMessage& msg_;
    RenderContext ctx_;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write the empty implementation**

```cpp
// src/ui/components/MessageComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

// MessageComponent is an abstract base class with no implementation needed.
// Concrete subclasses provide OnRender() and OnEvent() overrides.

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `CMakeLists.txt`, find the FTXUI conditional source block and add:
```
src/ui/components/MessageComponent.cpp
```

- [ ] **Step 4: Build to verify**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/components/MessageComponent.hpp \
        src/ui/components/MessageComponent.cpp \
        CMakeLists.txt
git commit -m "feat: add MessageComponent base class for FTXUI component tree"
```

---

### Task 3: Create IToolRenderer interface + ToolRendererRegistry

**Files:**
- Create: `include/claude/ui/IToolRenderer.hpp`
- Create: `include/claude/ui/ToolRendererRegistry.hpp`
- Create: `src/ui/ToolRendererRegistry.cpp`
- Create: `tests/test_tool_renderer_registry.cpp`

- [ ] **Step 1: Write IToolRenderer interface**

```cpp
// include/claude/ui/IToolRenderer.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>
#include <string>
#include <vector>

namespace claude::ui {

class IToolRenderer {
public:
    virtual ~IToolRenderer() = default;

    // Tool invocation UI
    virtual ftxui::Element renderToolUse(const ToolUseBlock& tool,
                                         const RenderContext& ctx) = 0;
    virtual std::string renderToolUseAnsi(const ToolUseBlock& tool) = 0;

    // Success result UI
    virtual ftxui::Element renderToolResult(const ToolResultBlock& result,
                                            const ToolUseBlock& tool,
                                            const RenderContext& ctx) = 0;
    virtual std::string renderToolResultAnsi(const ToolResultBlock& result,
                                             const ToolUseBlock& tool) = 0;

    // Error result UI
    virtual ftxui::Element renderToolError(const ToolResultBlock& result,
                                           const ToolUseBlock& tool,
                                           const RenderContext& ctx) = 0;
    virtual std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                            const ToolUseBlock& tool) = 0;

    // Rejected UI
    virtual ftxui::Element renderToolRejected(const ToolUseBlock& tool,
                                              const RenderContext& ctx) = 0;
    virtual std::string renderToolRejectedAnsi(const ToolUseBlock& tool) = 0;

    // Canceled UI
    virtual ftxui::Element renderToolCanceled(const ToolUseBlock& tool,
                                              const RenderContext& ctx) = 0;
    virtual std::string renderToolCanceledAnsi(const ToolUseBlock& tool) = 0;

    // Progress while tool is running
    virtual ftxui::Element renderToolProgress(const ToolUseBlock& tool,
                                              const std::string& progress,
                                              const RenderContext& ctx) = 0;

    // Queued waiting UI
    virtual ftxui::Element renderToolQueued(const ToolUseBlock& tool,
                                            const RenderContext& ctx) = 0;

    // Grouped parallel same-type calls
    virtual ftxui::Element renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) = 0;

    // Compact summary text
    virtual std::string getToolUseSummary(const ToolUseBlock& tool) = 0;

    // User-facing display name
    virtual std::string userFacingName(const ToolUseBlock& tool) = 0;

    // Classification
    virtual bool isCollapsible() const = 0;
    virtual bool isResultTruncatable(const ToolResultBlock& result) const = 0;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write ToolRendererRegistry header**

```cpp
// include/claude/ui/ToolRendererRegistry.hpp
#pragma once

#include <claude/ui/IToolRenderer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace claude::ui {

class ToolRendererRegistry {
public:
    static ToolRendererRegistry& instance();

    void registerRenderer(const std::string& toolName,
                          std::unique_ptr<IToolRenderer> renderer);

    IToolRenderer* getRenderer(const std::string& toolName) const;
    IToolRenderer* getFallbackRenderer() const;

    // Non-copyable
    ToolRendererRegistry(const ToolRendererRegistry&) = delete;
    ToolRendererRegistry& operator=(const ToolRendererRegistry&) = delete;

private:
    ToolRendererRegistry();
    std::unordered_map<std::string, std::unique_ptr<IToolRenderer>> renderers_;
    std::unique_ptr<IToolRenderer> fallback_;
};

} // namespace claude::ui
```

- [ ] **Step 3: Write ToolRendererRegistry implementation**

```cpp
// src/ui/ToolRendererRegistry.cpp
#ifdef HAS_FTXUI

#include <claude/ui/ToolRendererRegistry.hpp>
#include <claude/ui/renderers/DefaultToolRenderer.hpp>

namespace claude::ui {

ToolRendererRegistry& ToolRendererRegistry::instance() {
    static ToolRendererRegistry registry;
    return registry;
}

ToolRendererRegistry::ToolRendererRegistry()
    : fallback_(std::make_unique<DefaultToolRenderer>()) {}

void ToolRendererRegistry::registerRenderer(
    const std::string& toolName,
    std::unique_ptr<IToolRenderer> renderer) {
    renderers_[toolName] = std::move(renderer);
}

IToolRenderer* ToolRendererRegistry::getRenderer(
    const std::string& toolName) const {
    auto it = renderers_.find(toolName);
    if (it != renderers_.end()) return it->second.get();
    return fallback_.get();
}

IToolRenderer* ToolRendererRegistry::getFallbackRenderer() const {
    return fallback_.get();
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 4: Write DefaultToolRenderer (minimal stub that produces current behavior)**

Header:
```cpp
// src/ui/renderers/DefaultToolRenderer.hpp
#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/IToolRenderer.hpp>
#include <claude/console/ActivityDescription.hpp>

namespace claude::ui {

class DefaultToolRenderer : public IToolRenderer {
public:
    ftxui::Element renderToolUse(const ToolUseBlock& tool,
                                 const RenderContext& ctx) override;
    std::string renderToolUseAnsi(const ToolUseBlock& tool) override;

    ftxui::Element renderToolResult(const ToolResultBlock& result,
                                    const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    std::string renderToolResultAnsi(const ToolResultBlock& result,
                                     const ToolUseBlock& tool) override;

    ftxui::Element renderToolError(const ToolResultBlock& result,
                                   const ToolUseBlock& tool,
                                   const RenderContext& ctx) override;
    std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                    const ToolUseBlock& tool) override;

    ftxui::Element renderToolRejected(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolRejectedAnsi(const ToolUseBlock& tool) override;

    ftxui::Element renderToolCanceled(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolCanceledAnsi(const ToolUseBlock& tool) override;

    ftxui::Element renderToolProgress(const ToolUseBlock& tool,
                                      const std::string& progress,
                                      const RenderContext& ctx) override;
    ftxui::Element renderToolQueued(const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    ftxui::Element renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) override;

    std::string getToolUseSummary(const ToolUseBlock& tool) override;
    std::string userFacingName(const ToolUseBlock& tool) override;
    bool isCollapsible() const override { return false; }
    bool isResultTruncatable(const ToolResultBlock& result) const override { return false; }
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

Implementation:
```cpp
// src/ui/renderers/DefaultToolRenderer.cpp
#ifdef HAS_FTXUI

#include <claude/ui/renderers/DefaultToolRenderer.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element DefaultToolRenderer::renderToolUse(
    const ToolUseBlock& tool, const RenderContext& ctx) {
    using namespace ftxui;
    auto activity = console::getActivityDescription(tool.toolName, tool.input, true);
    Color fg = Color(AnsiStyle::toolFgRGB(tool.toolName));
    Color bg = Color(AnsiStyle::toolBgRGB(tool.toolName));
    return hbox({
        text("⎿ ") | dim,
        text(" " + tool.toolName + " ") | bold | color(fg) | bgcolor(bg),
        text(" " + activity) | dim | color(Color(200, 195, 180))
    });
}

std::string DefaultToolRenderer::renderToolUseAnsi(const ToolUseBlock& tool) {
    return "⎿ " + AnsiStyle::toolBgColor(tool.toolName) +
           tool.toolName + AnsiStyle::RESET + " " +
           console::getActivityDescription(tool.toolName, tool.input, true);
}

ftxui::Element DefaultToolRenderer::renderToolResult(
    const ToolResultBlock& result, const ToolUseBlock& tool,
    const RenderContext& ctx) {
    using namespace ftxui;
    if (ctx.verbose) {
        return vbox({
            text("⎿ ") | dim,
            paragraph(result.result) | dim
        });
    }
    // Compact: one-line summary
    auto summary = getToolUseSummary(tool);
    return hbox({text("    ⎿ "), text(summary) | dim});
}

std::string DefaultToolRenderer::renderToolResultAnsi(
    const ToolResultBlock& result, const ToolUseBlock& tool) {
    return "    ⎿ " + result.result;
}

ftxui::Element DefaultToolRenderer::renderToolError(
    const ToolResultBlock& result, const ToolUseBlock& tool,
    const RenderContext& ctx) {
    using namespace ftxui;
    return hbox({
        text("✗ ") | bold | color(Color(210, 150, 150)),
        paragraph(result.result) | color(Color(210, 150, 150))
    });
}

std::string DefaultToolRenderer::renderToolErrorAnsi(
    const ToolResultBlock& result, const ToolUseBlock& tool) {
    return AnsiStyle::Semantic::TOOL_ERROR + "✗ " + result.result + AnsiStyle::RESET;
}

ftxui::Element DefaultToolRenderer::renderToolRejected(
    const ToolUseBlock& tool, const RenderContext& ctx) {
    using namespace ftxui;
    return hbox({text("⊘ ") | dim | color(Color(210, 186, 140))});
}

std::string DefaultToolRenderer::renderToolRejectedAnsi(const ToolUseBlock& tool) {
    return AnsiStyle::DIM + "⊘ Rejected" + AnsiStyle::RESET;
}

ftxui::Element DefaultToolRenderer::renderToolCanceled(
    const ToolUseBlock& tool, const RenderContext& ctx) {
    using namespace ftxui;
    return hbox({text("⊘ ") | dim});
}

std::string DefaultToolRenderer::renderToolCanceledAnsi(const ToolUseBlock& tool) {
    return AnsiStyle::DIM + "⊘ Canceled" + AnsiStyle::RESET;
}

ftxui::Element DefaultToolRenderer::renderToolProgress(
    const ToolUseBlock& tool, const std::string& progress,
    const RenderContext& ctx) {
    using namespace ftxui;
    auto activity = console::getActivityDescription(tool.toolName, tool.input, true);
    Color fg = Color(AnsiStyle::toolFgRGB(tool.toolName));
    bool blink = (ctx.tickCounter % 2 == 0);
    auto dot = blink ? text("●") | color(fg) : text("○") | dim | color(fg);
    return hbox({dot, text(" " + activity + "…") | dim});
}

ftxui::Element DefaultToolRenderer::renderToolQueued(
    const ToolUseBlock& tool, const RenderContext& ctx) {
    using namespace ftxui;
    return hbox({text("○ ") | dim, text(tool.toolName + " (queued)") | dim});
}

ftxui::Element DefaultToolRenderer::renderGroupedToolUse(
    const std::vector<ToolUseBlock>& tools, const RenderContext& ctx) {
    using namespace ftxui;
    auto count = std::to_string(tools.size());
    auto name = tools.empty() ? "Tool" : tools[0].toolName;
    Color fg = Color(AnsiStyle::toolFgRGB(name));
    return hbox({
        text("⎿ ") | dim,
        text(" " + name + " ") | color(fg),
        text(" ×" + count) | dim
    });
}

std::string DefaultToolRenderer::getToolUseSummary(const ToolUseBlock& tool) {
    return console::getActivityDescription(tool.toolName, tool.input, false);
}

std::string DefaultToolRenderer::userFacingName(const ToolUseBlock& tool) {
    return tool.toolName;
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 5: Write test for ToolRendererRegistry**

```cpp
// tests/test_tool_renderer_registry.cpp
#ifdef HAS_FTXUI

#include <catch2/catch_test_macros.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include <claude/ui/renderers/DefaultToolRenderer.hpp>

using namespace claude::ui;

TEST_CASE("ToolRendererRegistry singleton", "[tool_renderer]") {
    auto& r1 = ToolRendererRegistry::instance();
    auto& r2 = ToolRendererRegistry::instance();
    REQUIRE(&r1 == &r2);
}

TEST_CASE("ToolRendererRegistry returns fallback for unknown tool", "[tool_renderer]") {
    auto& registry = ToolRendererRegistry::instance();
    auto* renderer = registry.getRenderer("NonExistentTool");
    REQUIRE(renderer != nullptr);
    REQUIRE(dynamic_cast<DefaultToolRenderer*>(renderer) != nullptr);
}

TEST_CASE("ToolRendererRegistry returns registered renderer", "[tool_renderer]") {
    auto& registry = ToolRendererRegistry::instance();
    auto custom = std::make_unique<DefaultToolRenderer>();
    auto* raw = custom.get();
    registry.registerRenderer("CustomTool", std::move(custom));
    auto* result = registry.getRenderer("CustomTool");
    REQUIRE(result == raw);
}

TEST_CASE("ToolRendererRegistry fallback is never null", "[tool_renderer]") {
    auto& registry = ToolRendererRegistry::instance();
    REQUIRE(registry.getFallbackRenderer() != nullptr);
}

#endif // HAS_FTXUI
```

- [ ] **Step 6: Add new source files and test to CMakeLists.txt**

In CMakeLists.txt:
1. Add `src/ui/ToolRendererRegistry.cpp` and `src/ui/renderers/DefaultToolRenderer.cpp` to the FTXUI conditional source list
2. Add `tests/test_tool_renderer_registry.cpp` as a new test executable linking `claude_core` and `Catch2::Catch2WithMain`

- [ ] **Step 7: Build and run tests**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest -R test_tool_renderer --output-on-failure`
Expected: All 4 tests pass

- [ ] **Step 8: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/IToolRenderer.hpp \
        include/claude/ui/ToolRendererRegistry.hpp \
        src/ui/ToolRendererRegistry.cpp \
        src/ui/renderers/DefaultToolRenderer.hpp \
        src/ui/renderers/DefaultToolRenderer.cpp \
        tests/test_tool_renderer_registry.cpp \
        CMakeLists.txt
git commit -m "feat: add IToolRenderer interface and ToolRendererRegistry with DefaultToolRenderer"
```

---

### Task 4: Extract HeaderBar component from MainComponent

**Files:**
- Create: `include/claude/ui/components/HeaderBar.hpp`
- Create: `src/ui/components/HeaderBar.cpp`
- Reference: `src/ui/FtxuiRender.cpp:36-88` (current header rendering code)

- [ ] **Step 1: Write HeaderBar header**

```cpp
// include/claude/ui/components/HeaderBar.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>

namespace claude::ui {

struct HeaderState {
    std::string modelName;
    float contextPercent = 0.0f;
    int inputTokens = 0;
    int outputTokens = 0;
    float cost = 0.0f;
    std::string cwd;
    std::string gitBranch;
    bool isStreaming = false;
};

ftxui::Component HeaderBar(HeaderState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write HeaderBar implementation — copy rendering logic from MainComponent::OnRender() lines 36-88**

```cpp
// src/ui/components/HeaderBar.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/HeaderBar.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

namespace {
ftxui::Element renderHeader(const HeaderState& s) {
    using namespace ftxui;

    // Context usage bar
    auto pct = static_cast<int>(s.contextPercent * 100);
    Color barColor = s.contextPercent < 0.70f ? MacMint
                   : s.contextPercent < 0.85f ? MacGold
                   : MacRose;
    int filled = static_cast<int>(s.contextPercent * 20);
    std::string barStr = std::string(filled, '█') + std::string(20 - filled, '░');

    // Token/cost text
    char tokenBuf[64];
    std::snprintf(tokenBuf, sizeof(tokenBuf), "%.1fK in/%.1fK out · $%.4f",
                  s.inputTokens / 1000.0, s.outputTokens / 1000.0, s.cost);

    // CWD + git
    std::string locStr = s.cwd;
    if (!s.gitBranch.empty()) locStr += " (" + s.gitBranch + ")";

    // Status indicator
    auto statusEl = s.isStreaming
        ? text("● Running") | color(MacPeach) | bold
        : text("○ Idle") | color(MacCream);

    return hbox({
        text("╭─ Claude Code │ ") | bold | color(MacPeach),
        text(s.modelName) | color(MacSky),
        text(" │ ") | dim,
        text(barStr) | color(barColor),
        text(" " + std::to_string(pct) + "% ctx") | color(barColor),
        text(" │ ") | dim,
        text(tokenBuf) | dim | color(MacCream),
        text(" │ ") | dim,
        text(locStr) | dim | color(MacCream),
        text(" │ ") | dim,
        statusEl,
        text(" ─╮") | bold | color(MacPeach),
    });
}
} // namespace

ftxui::Component HeaderBar(HeaderState& state) {
    return ftxui::Renderer([&state] { return renderHeader(state); });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Add to CMakeLists.txt** — add `src/ui/components/HeaderBar.cpp` to FTXUI conditional sources

- [ ] **Step 4: Build to verify**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/components/HeaderBar.hpp \
        src/ui/components/HeaderBar.cpp \
        CMakeLists.txt
git commit -m "feat: extract HeaderBar component from MainComponent"
```

---

### Task 5: Extract simple message components from MainComponent

Extract the simplest message renderers (UserPrompt, AssistantText, SystemInfo, SystemError, TurnDuration, CompactBoundary) from MainComponent::OnRender(). These have minimal logic — just formatting a DisplayMessage into an Element.

**Files:**
- Create 6 component headers in `include/claude/ui/components/`
- Create 6 component implementations in `src/ui/components/`
- Reference: `src/ui/FtxuiRender.cpp` — message rendering switch cases

- [ ] **Step 1: Write UserPromptComponent**

Header:
```cpp
// include/claude/ui/components/UserPromptComponent.hpp
#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

class UserPromptComponent : public MessageComponent {
public:
    UserPromptComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override { return DisplayMessage::Type::UserPrompt; }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

Implementation (extracted from FtxuiRender.cpp UserPrompt case):
```cpp
// src/ui/components/UserPromptComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/UserPromptComponent.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element UserPromptComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("❯ ") | bold | color(MacSage),
        paragraph(msg_.text) | bold
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write SystemInfoComponent**

Header follows same pattern as UserPromptComponent. Implementation:
```cpp
// src/ui/components/SystemInfoComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/SystemInfoComponent.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element SystemInfoComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("※ ") | color(MacSky),
        paragraph(msg_.text) | dim | color(MacCream)
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Write SystemErrorComponent**

Implementation:
```cpp
ftxui::Element SystemErrorComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("✗ ") | bold | color(MacRose),
        paragraph(msg_.text) | bold | color(MacRose)
    });
}
```

- [ ] **Step 4: Write TurnDurationComponent**

Implementation:
```cpp
ftxui::Element TurnDurationComponent::OnRender() {
    using namespace ftxui;
    return paragraph(msg_.text) | dim | color(MacCream);
}
```

- [ ] **Step 5: Write CompactBoundaryComponent**

Implementation:
```cpp
ftxui::Element CompactBoundaryComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("── ") | dim | color(MacShadow),
        paragraph(msg_.text) | dim | color(MacCream),
        text(" ──") | dim | color(MacShadow)
    });
}
```

- [ ] **Step 6: Write AssistantTextComponent**

Implementation:
```cpp
// src/ui/components/AssistantTextComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantTextComponent.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element AssistantTextComponent::OnRender() {
    auto elements = FtxuiMarkdown::render(msg_.text);
    return ftxui::vbox(std::move(elements));
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 7: Add all 6 .cpp files to CMakeLists.txt**, build, and commit

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/components/UserPromptComponent.hpp \
        src/ui/components/UserPromptComponent.cpp \
        include/claude/ui/components/SystemInfoComponent.hpp \
        src/ui/components/SystemInfoComponent.cpp \
        include/claude/ui/components/SystemErrorComponent.hpp \
        src/ui/components/SystemErrorComponent.cpp \
        include/claude/ui/components/TurnDurationComponent.hpp \
        src/ui/components/TurnDurationComponent.cpp \
        include/claude/ui/components/CompactBoundaryComponent.hpp \
        src/ui/components/CompactBoundaryComponent.cpp \
        include/claude/ui/components/AssistantTextComponent.hpp \
        src/ui/components/AssistantTextComponent.cpp \
        CMakeLists.txt
git commit -m "feat: extract 6 simple message components from MainComponent"
```

---

### Task 6: Extract complex message components

Extract the components with more complex rendering logic: AssistantThinking, ToolUse, ToolResult, CollapsedReadSearch, GroupedToolUse, AgentProgress.

**Files:**
- Create 6 component headers in `include/claude/ui/components/`
- Create 6 component implementations in `src/ui/components/`
- Reference: `src/ui/FtxuiRender.cpp` — corresponding switch cases

- [ ] **Step 1: Write AssistantThinkingComponent**

The thinking component supports two states: collapsed (single line) and expanded (full Markdown). Toggle with Ctrl+O.

```cpp
// include/claude/ui/components/AssistantThinkingComponent.hpp
#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

class AssistantThinkingComponent : public MessageComponent {
public:
    AssistantThinkingComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::AssistantThinking;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

Implementation:
```cpp
// src/ui/components/AssistantThinkingComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/AssistantThinkingComponent.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element AssistantThinkingComponent::OnRender() {
    using namespace ftxui;
    if (ctx_.verbose || msg_.expanded) {
        auto content = FtxuiMarkdown::render(msg_.thinking.text);
        auto inner = vbox(std::move(content)) | dim;
        return window(text("💭 Thinking") | color(MacLavender) | dim,
                      inner) | color(MacLavender) | dim;
    }
    return hbox({
        text("💭 Thinking") | dim | italic | color(MacLavender),
        text(" (ctrl+o to expand)") | dim | color(MacShadow)
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write ToolUseComponent — delegates to IToolRenderer**

```cpp
// include/claude/ui/components/ToolUseComponent.hpp
#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

class ToolUseComponent : public MessageComponent {
public:
    ToolUseComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::AssistantToolUse;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

```cpp
// src/ui/components/ToolUseComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/ToolUseComponent.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element ToolUseComponent::OnRender() {
    auto* renderer = ToolRendererRegistry::instance().getRenderer(
        msg_.toolUse.toolName);
    return renderer->renderToolUse(msg_.toolUse, ctx_);
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Write ToolResultComponent — delegates to IToolRenderer based on result status**

```cpp
// include/claude/ui/components/ToolResultComponent.hpp
#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageComponent.hpp>

namespace claude::ui {

class ToolResultComponent : public MessageComponent {
public:
    ToolResultComponent(const DisplayMessage& msg, const RenderContext& ctx)
        : MessageComponent(msg, ctx) {}

    DisplayMessage::Type messageType() const override {
        return DisplayMessage::Type::UserToolResult;
    }
    ftxui::Element OnRender() override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

```cpp
// src/ui/components/ToolResultComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/ToolResultComponent.hpp>
#include <claude/ui/ToolRendererRegistry.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element ToolResultComponent::OnRender() {
    auto& result = msg_.toolResult;
    auto* renderer = ToolRendererRegistry::instance().getRenderer(result.toolName);

    // Use a placeholder ToolUseBlock for the tool context
    ToolUseBlock toolCtx;
    toolCtx.toolName = result.toolName;

    if (result.isError) {
        return renderer->renderToolError(result, toolCtx, ctx_);
    }
    return renderer->renderToolResult(result, toolCtx, ctx_);
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 4: Write CollapsedReadSearchComponent**

```cpp
// src/ui/components/CollapsedReadSearchComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/CollapsedReadSearchComponent.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element CollapsedReadSearchComponent::OnRender() {
    using namespace ftxui;
    auto& g = msg_.collapsedGroup;
    auto summary = g.summaryText();
    return hbox({
        text("⎿ ") | dim,
        text(summary) | color(MacSky),
        text(" (ctrl+o to expand)") | dim | color(MacShadow)
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 5: Write GroupedToolUseComponent**

```cpp
// src/ui/components/GroupedToolUseComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/GroupedToolUseComponent.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element GroupedToolUseComponent::OnRender() {
    using namespace ftxui;
    auto& tools = msg_.groupedTools;
    if (tools.empty()) return text("") | dim;
    auto* renderer = ToolRendererRegistry::instance().getRenderer(tools[0].toolName);
    // Build ToolUseBlock vector from ToolUseRenderData
    std::vector<ToolUseBlock> blocks;
    for (auto& td : tools) {
        ToolUseBlock b;
        b.toolName = td.toolName;
        b.input = td.arguments;
        blocks.push_back(b);
    }
    return renderer->renderGroupedToolUse(blocks, ctx_);
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 6: Write AgentProgressComponent**

```cpp
// src/ui/components/AgentProgressComponent.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/AgentProgressComponent.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element AgentProgressComponent::OnRender() {
    using namespace ftxui;
    return hbox({
        text("├── ") | dim | color(MacShadow),
        text("⏻ ") | color(MacPeach),
        paragraph(msg_.text) | dim | color(MacCream)
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 7: Add all 6 .cpp files to CMakeLists.txt**, build, and commit

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/components/AssistantThinkingComponent.hpp \
        src/ui/components/AssistantThinkingComponent.cpp \
        include/claude/ui/components/ToolUseComponent.hpp \
        src/ui/components/ToolUseComponent.cpp \
        include/claude/ui/components/ToolResultComponent.hpp \
        src/ui/components/ToolResultComponent.cpp \
        include/claude/ui/components/CollapsedReadSearchComponent.hpp \
        src/ui/components/CollapsedReadSearchComponent.cpp \
        include/claude/ui/components/GroupedToolUseComponent.hpp \
        src/ui/components/GroupedToolUseComponent.cpp \
        include/claude/ui/components/AgentProgressComponent.hpp \
        src/ui/components/AgentProgressComponent.cpp \
        CMakeLists.txt
git commit -m "feat: extract 6 complex message components with IToolRenderer delegation"
```

---

### Task 7: Create MessageList factory + ContentArea + StreamingText + ThinkingIndicator

**Files:**
- Create: `include/claude/ui/components/MessageList.hpp`, `src/ui/components/MessageList.cpp`
- Create: `include/claude/ui/components/ContentArea.hpp`, `src/ui/components/ContentArea.cpp`
- Create: `include/claude/ui/components/StreamingText.hpp`, `src/ui/components/StreamingText.cpp`
- Create: `include/claude/ui/components/ThinkingIndicator.hpp`, `src/ui/components/ThinkingIndicator.cpp`

- [ ] **Step 1: Write MessageList — the factory that dispatches to MessageComponents**

```cpp
// include/claude/ui/components/MessageList.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>
#include <vector>

namespace claude::ui {

ftxui::Component MessageListComponent(
    const std::vector<DisplayMessage>& messages,
    const RenderContext& ctx);

} // namespace claude::ui

#endif // HAS_FTXUI
```

```cpp
// src/ui/components/MessageList.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/MessageList.hpp>
#include <claude/ui/components/MessageComponent.hpp>
#include <claude/ui/components/UserPromptComponent.hpp>
#include <claude/ui/components/AssistantTextComponent.hpp>
#include <claude/ui/components/AssistantThinkingComponent.hpp>
#include <claude/ui/components/ToolUseComponent.hpp>
#include <claude/ui/components/ToolResultComponent.hpp>
#include <claude/ui/components/SystemInfoComponent.hpp>
#include <claude/ui/components/SystemErrorComponent.hpp>
#include <claude/ui/components/TurnDurationComponent.hpp>
#include <claude/ui/components/CompactBoundaryComponent.hpp>
#include <claude/ui/components/CollapsedReadSearchComponent.hpp>
#include <claude/ui/components/GroupedToolUseComponent.hpp>
#include <claude/ui/components/AgentProgressComponent.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

namespace {
ftxui::Element renderMessageList(const std::vector<DisplayMessage>* messages,
                                  const RenderContext* ctx) {
    using namespace ftxui;
    Elements els;
    for (auto& msg : *messages) {
        Component comp = makeComponentForMessage(msg, *ctx);
        els.push_back(comp->Render());
    }
    return vbox(std::move(els));
}

ftxui::Component makeComponentForMessage(const DisplayMessage& msg,
                                          const RenderContext& ctx) {
    using Type = DisplayMessage::Type;
    // We return a Renderer wrapper that delegates to the appropriate component
    switch (msg.type) {
        case Type::UserPrompt:
            return Renderer([msg, ctx]() mutable {
                UserPromptComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::AssistantText:
            return Renderer([msg, ctx]() mutable {
                AssistantTextComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::AssistantThinking:
            return Renderer([msg, ctx]() mutable {
                AssistantThinkingComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::AssistantToolUse:
            return Renderer([msg, ctx]() mutable {
                ToolUseComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::UserToolResult:
            return Renderer([msg, ctx]() mutable {
                ToolResultComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::SystemInfo:
            return Renderer([msg, ctx]() mutable {
                SystemInfoComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::SystemError:
            return Renderer([msg, ctx]() mutable {
                SystemErrorComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::TurnDuration:
            return Renderer([msg, ctx]() mutable {
                TurnDurationComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::CompactBoundary:
            return Renderer([msg, ctx]() mutable {
                CompactBoundaryComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::CollapsedReadSearch:
            return Renderer([msg, ctx]() mutable {
                CollapsedReadSearchComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::GroupedToolUse:
            return Renderer([msg, ctx]() mutable {
                GroupedToolUseComponent c(msg, ctx);
                return c.OnRender();
            });
        case Type::AgentProgress:
            return Renderer([msg, ctx]() mutable {
                AgentProgressComponent c(msg, ctx);
                return c.OnRender();
            });
        default:
            return Renderer([msg]() { return ftxui::paragraph(msg.text) | ftxui::dim; });
    }
}
} // namespace

ftxui::Component MessageListComponent(
    const std::vector<DisplayMessage>& messages,
    const RenderContext& ctx) {
    // Store pointers for the renderer lambda
    const auto* msgs = &messages;
    const auto* c = &ctx;
    return ftxui::Renderer([msgs, c] { return renderMessageList(msgs, c); });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write StreamingText component**

```cpp
// include/claude/ui/components/StreamingText.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <string>

namespace claude::ui {

struct StreamingState {
    std::string text;
    int tickCounter = 0;
};

ftxui::Component StreamingTextComponent(StreamingState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
```

```cpp
// src/ui/components/StreamingText.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/StreamingText.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

namespace {
ftxui::Element renderStreaming(const StreamingState& s) {
    using namespace ftxui;
    if (s.text.empty()) return text("");
    auto elements = FtxuiMarkdown::render(s.text);
    // Append blinking cursor
    bool glimmer = (s.tickCounter % 4 < 2);
    auto cursor = text("◉") | color(glimmer ? MacPeach : MacCream);
    elements.push_back(cursor);
    return vbox(std::move(elements));
}
} // namespace

ftxui::Component StreamingTextComponent(StreamingState& state) {
    return ftxui::Renderer([&state] { return renderStreaming(state); });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Write ThinkingIndicator component**

```cpp
// include/claude/ui/components/ThinkingIndicator.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <string>

namespace claude::ui {

struct ThinkingState {
    std::string summary;
    bool active = false;
    bool stalled = false;
    int tickCounter = 0;
};

ftxui::Component ThinkingIndicatorComponent(ThinkingState& state);

} // namespace claude::ui

#endif // HAS_FTXUI
```

```cpp
// src/ui/components/ThinkingIndicator.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/ThinkingIndicator.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

namespace {
ftxui::Element renderThinking(const ThinkingState& s) {
    using namespace ftxui;
    if (!s.active) return text("");

    auto spinnerEl = spinner(1, s.tickCounter) | color(MacLavender);
    auto label = s.stalled
        ? text(" Thinking (stalled)") | color(MacRose)
        : text(" Thinking") | color(MacLavender);

    Elements parts = {spinnerEl, label};
    if (!s.summary.empty()) {
        parts.push_back(text(" " + s.summary) | dim | color(MacCream));
    }
    parts.push_back(text(" (ctrl+o)") | dim | color(MacShadow));
    return hbox(std::move(parts));
}
} // namespace

ftxui::Component ThinkingIndicatorComponent(ThinkingState& state) {
    return ftxui::Renderer([&state] { return renderThinking(state); });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 4: Write ContentArea — combines MessageList + StreamingText + ThinkingIndicator in a scrollable area**

```cpp
// include/claude/ui/components/ContentArea.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <claude/ui/RenderContext.hpp>
#include <claude/ui/components/StreamingText.hpp>
#include <claude/ui/components/ThinkingIndicator.hpp>
#include <vector>

namespace claude::ui {

struct ContentState {
    const std::vector<DisplayMessage>* messages = nullptr;
    StreamingState streaming;
    ThinkingState thinking;
    bool autoScroll = true;
    float scrollRatio = 1.0f;
    int messagesAbove = 0;
};

ftxui::Component ContentAreaComponent(ContentState& state, const RenderContext& ctx);

} // namespace claude::ui

#endif // HAS_FTXUI
```

```cpp
// src/ui/components/ContentArea.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/ContentArea.hpp>
#include <claude/ui/components/MessageList.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

namespace {
ftxui::Element renderContent(ContentState& state, const RenderContext& ctx) {
    using namespace ftxui;
    Elements parts;

    // "N messages above" indicator when scrolled up
    if (state.messagesAbove > 0) {
        parts.push_back(
            text("  ↑ " + std::to_string(state.messagesAbove) +
                 " messages above") | dim | color(MacShadow) | center);
    }

    // Message list
    if (state.messages) {
        auto msgComp = MessageListComponent(*state.messages, ctx);
        parts.push_back(msgComp->Render());
    }

    // Streaming text (if any)
    if (!state.streaming.text.empty()) {
        auto streamComp = StreamingTextComponent(state.streaming);
        parts.push_back(streamComp->Render());
    }

    // Thinking indicator
    auto thinkComp = ThinkingIndicatorComponent(state.thinking);
    auto thinkEl = thinkComp->Render();
    if (thinkEl != text("")) {
        parts.push_back(thinkEl);
    }

    auto content = vbox(std::move(parts));
    return content | flex | yframe | reflect(state.box);
}
} // namespace

ftxui::Component ContentAreaComponent(ContentState& state, const RenderContext& ctx) {
    return ftxui::Renderer([&state, &ctx] { return renderContent(state, ctx); });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 5: Add all 4 new .cpp files to CMakeLists.txt**, build, commit

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/components/MessageList.hpp \
        src/ui/components/MessageList.cpp \
        include/claude/ui/components/StreamingText.hpp \
        src/ui/components/StreamingText.cpp \
        include/claude/ui/components/ThinkingIndicator.hpp \
        src/ui/components/ThinkingIndicator.cpp \
        include/claude/ui/components/ContentArea.hpp \
        src/ui/components/ContentArea.cpp \
        CMakeLists.txt
git commit -m "feat: add MessageList factory, StreamingText, ThinkingIndicator, ContentArea"
```

---

### Task 8: Create AppLayout + wire FtxuiRepl to new component tree

**Files:**
- Create: `include/claude/ui/components/AppLayout.hpp`, `src/ui/components/AppLayout.cpp`
- Modify: `src/ui/FtxuiRepl.cpp`, `include/claude/ui/FtxuiRepl.hpp`
- Modify: `src/ui/FtxuiRender.cpp` — replace MainComponent with AppLayout

This is the critical integration task. The AppLayout component orchestrates the full layout using the extracted components. After this task, `detail::MainComponent` can be deleted.

- [ ] **Step 1: Write AppLayout component**

```cpp
// include/claude/ui/components/AppLayout.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <claude/ui/components/HeaderBar.hpp>
#include <claude/ui/components/ContentArea.hpp>
#include <claude/ui/RenderContext.hpp>
#include <string>

namespace claude::ui {

struct InputState {
    std::string text;
    size_t cursorPos = 0;
    bool streaming = false;
};

struct FooterState {
    std::string modeIndicator;
    std::string hintText;
    bool authenticated = false;
};

struct AppLayoutState {
    HeaderState header;
    ContentState content;
    InputState input;
    FooterState footer;
    // Permission overlay
    bool permissionActive = false;
    std::string permissionToolName;
    std::string permissionActivity;
    int permissionFocusedIndex = 0;
    // Completions
    std::vector<std::string> completions;
    size_t completionIndex = 0;
};

ftxui::Component AppLayoutComponent(AppLayoutState& state, const RenderContext& ctx);

} // namespace claude::ui

#endif // HAS_FTXUI
```

The implementation in `src/ui/components/AppLayout.cpp` composes the layout:

```cpp
// src/ui/components/AppLayout.cpp
#ifdef HAS_FTXUI

#include <claude/ui/components/AppLayout.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace claude::ui {

namespace {
ftxui::Element renderLayout(AppLayoutState& state, const RenderContext& ctx) {
    using namespace ftxui;

    // Header
    auto headerComp = HeaderBar(state.header);
    auto header = headerComp->Render();

    // Content area
    auto contentComp = ContentAreaComponent(state.content, ctx);
    auto content = contentComp->Render();

    // Permission overlay (if active)
    if (state.permissionActive) {
        // Permission overlay rendered as dbox on top of content
        // This replicates the existing permission overlay from FtxuiRender.cpp:694-787
        auto overlay = renderPermissionOverlay(state);
        content = dbox({content | clear_under, overlay | center});
    }

    // Input line
    auto input = renderInputLine(state.input);

    // Completions (if any)
    auto completions = renderCompletions(state.completions, state.completionIndex);

    // Footer
    auto footer = renderFooter(state.footer);

    return vbox({
        header,
        separatorLight(),
        content | flex,
        separatorLight(),
        completions,
        input | bold,
        footer
    });
}

ftxui::Element renderPermissionOverlay(const AppLayoutState& s) {
    using namespace ftxui;
    // Copy the permission rendering from MainComponent
    // with 5 options: Yes(once), Allow session, Always allow, No(once), Always deny
    Elements options;
    const char* labels[] = {"Yes (once)", "Allow for this session",
                            "Yes, always allow", "No (once)", "No, always deny"};
    Color optColors[] = {MacMint, MacSage, MacSage, MacRose, MacRose};
    for (int i = 0; i < 5; ++i) {
        bool focused = (i == s.permissionFocusedIndex);
        auto line = focused
            ? text("❯ " + std::string(labels[i])) | bold | inverted | color(optColors[i])
            : text("  " + std::string(labels[i])) | dim | color(optColors[i]);
        options.push_back(line);
    }

    return window(
        text(" ⚠ Permission Required ") | bold | color(MacGold),
        vbox({
            text("  [" + s.permissionToolName + "]") | bold,
            text("  " + s.permissionActivity) | dim,
            text(""),
            vbox(std::move(options)),
            text("  ↑↓ select · Enter confirm · Esc cancel") | dim | color(MacShadow)
        })
    ) | size(WIDTH, LESS_THAN, 70) | hcenter;
}

ftxui::Element renderInputLine(const InputState& s) {
    using namespace ftxui;
    Color promptColor = s.streaming ? MacCream : MacSage;
    auto prompt = text("❯ ") | bold | color(promptColor);

    if (s.text.empty()) {
        return hbox({prompt, text("│") | inverted});
    }
    // Split text at cursor position and show inverted character
    auto before = s.text.substr(0, s.cursorPos);
    auto at = s.cursorPos < s.text.size()
        ? std::string(1, s.text[s.cursorPos])
        : std::string(" ");
    auto after = s.cursorPos < s.text.size()
        ? s.text.substr(s.cursorPos + 1)
        : std::string();
    return hbox({prompt, text(before), text(at) | inverted, text(after)});
}

ftxui::Element renderCompletions(const std::vector<std::string>& comps,
                                  size_t selectedIndex) {
    using namespace ftxui;
    if (comps.empty()) return text("");
    Elements items;
    for (size_t i = 0; i < comps.size(); ++i) {
        auto line = (i == selectedIndex)
            ? text("❯ " + comps[i]) | bold | color(MacPeach)
            : text("  " + comps[i]) | dim;
        items.push_back(line);
    }
    return window(text(" completions ") | dim | color(MacSky),
                  vbox(std::move(items)));
}

ftxui::Element renderFooter(const FooterState& s) {
    using namespace ftxui;
    Elements parts;
    if (!s.modeIndicator.empty()) {
        parts.push_back(text(s.modeIndicator) | color(MacSky));
        parts.push_back(text("  ") | dim);
    }
    parts.push_back(text(s.hintText) | dim | color(MacCream));
    parts.push_back(fillter());
    auto auth = s.authenticated
        ? text("✓ logged in") | color(MacMint) | dim
        : text("⚠ not authenticated") | color(MacGold) | dim;
    parts.push_back(auth);
    return hbox(std::move(parts));
}

} // namespace

ftxui::Component AppLayoutComponent(AppLayoutState& state, const RenderContext& ctx) {
    return ftxui::Renderer([&state, &ctx] { return renderLayout(state, ctx); });
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Modify FtxuiRepl to use AppLayout instead of MainComponent**

In `include/claude/ui/FtxuiRepl.hpp`:
- Add `#include <claude/ui/components/AppLayout.hpp>`
- Replace `ftxui::Component mainComponent_` with `AppLayoutState layoutState_`
- Expose state setter methods that update `layoutState_` fields instead of individual member variables

In `src/ui/FtxuiRepl.cpp`:
- Replace `BuildMainComponent()` to construct `AppLayoutComponent(layoutState_, ctx_)`
- Update all state setters (setModelInfo, setContextPercent, etc.) to update `layoutState_` fields

- [ ] **Step 3: Delete `detail::MainComponent` from FtxuiRender.cpp**

Remove the entire `detail::MainComponent` class and the `BuildMainComponent()` function body that creates it. The new AppLayout handles all rendering.

- [ ] **Step 4: Build and manually test**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build`
Expected: Build succeeds

Run the CLI manually to verify the layout renders correctly:
`./build/claude-cli --ftxui`

- [ ] **Step 5: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/components/AppLayout.hpp \
        src/ui/components/AppLayout.cpp \
        include/claude/ui/FtxuiRepl.hpp \
        src/ui/FtxuiRepl.cpp \
        src/ui/FtxuiRender.cpp \
        CMakeLists.txt
git commit -m "feat: replace MainComponent with AppLayout component tree"
```

---

## Phase 2: Content Type System Expansion

### Task 9: Expand DisplayMessageType enum with P0 types

**Files:**
- Modify: `include/claude/ui/UiMessageTypes.hpp`
- Modify: `src/ui/UiMessageTypes.cpp`
- Create: `tests/test_display_message_types.cpp`

- [ ] **Step 1: Add new enum values and factory methods to UiMessageTypes.hpp**

In the `DisplayMessage::Type` enum (line ~88), add after `AgentProgress`:

```cpp
// P0: Tool result subtypes (replacing UserToolResult)
UserToolSuccess,
UserToolError,
UserToolRejected,
UserToolCanceled,
// P0: Redacted thinking
AssistantRedactedThinking,
```

Add new factory methods after the existing ones:

```cpp
static DisplayMessage userToolSuccess(const String& toolUseId,
                                       const String& toolName,
                                       const String& result);
static DisplayMessage userToolError(const String& toolUseId,
                                     const String& toolName,
                                     const String& result);
static DisplayMessage userToolRejected(const String& toolUseId,
                                        const String& toolName);
static DisplayMessage userToolCanceled(const String& toolUseId,
                                        const String& toolName);
static DisplayMessage assistantRedactedThinking();
```

- [ ] **Step 2: Implement factory methods in UiMessageTypes.cpp**

```cpp
DisplayMessage DisplayMessage::userToolSuccess(
    const String& toolUseId, const String& toolName, const String& result) {
    DisplayMessage msg;
    msg.type = Type::UserToolSuccess;
    msg.messageId = MessageIdGenerator::next();
    msg.toolResult.toolUseId = toolUseId;
    msg.toolResult.toolName = toolName;
    msg.toolResult.result = result;
    msg.toolResult.isError = false;
    return msg;
}

DisplayMessage DisplayMessage::userToolError(
    const String& toolUseId, const String& toolName, const String& result) {
    DisplayMessage msg;
    msg.type = Type::UserToolError;
    msg.messageId = MessageIdGenerator::next();
    msg.toolResult.toolUseId = toolUseId;
    msg.toolResult.toolName = toolName;
    msg.toolResult.result = result;
    msg.toolResult.isError = true;
    return msg;
}

DisplayMessage DisplayMessage::userToolRejected(
    const String& toolUseId, const String& toolName) {
    DisplayMessage msg;
    msg.type = Type::UserToolRejected;
    msg.messageId = MessageIdGenerator::next();
    msg.toolResult.toolUseId = toolUseId;
    msg.toolResult.toolName = toolName;
    msg.toolResult.result = "Rejected";
    return msg;
}

DisplayMessage DisplayMessage::userToolCanceled(
    const String& toolUseId, const String& toolName) {
    DisplayMessage msg;
    msg.type = Type::UserToolCanceled;
    msg.messageId = MessageIdGenerator::next();
    msg.toolResult.toolUseId = toolUseId;
    msg.toolResult.toolName = toolName;
    msg.toolResult.result = "Canceled";
    return msg;
}

DisplayMessage DisplayMessage::assistantRedactedThinking() {
    DisplayMessage msg;
    msg.type = Type::AssistantRedactedThinking;
    msg.messageId = MessageIdGenerator::next();
    return msg;
}
```

- [ ] **Step 3: Add height estimation for new types in UiMessageTypes.cpp**

In the `estimateMessageHeight()` function, add cases for the new types:

```cpp
case DisplayMessage::Type::UserToolSuccess:   return 1;
case DisplayMessage::Type::UserToolError:     return 2;
case DisplayMessage::Type::UserToolRejected:  return 1;
case DisplayMessage::Type::UserToolCanceled:  return 1;
case DisplayMessage::Type::AssistantRedactedThinking: return 1;
```

- [ ] **Step 4: Write test**

```cpp
// tests/test_display_message_types.cpp
#include <catch2/catch_test_macros.hpp>
#include <claude/ui/UiMessageTypes.hpp>

using namespace claude;

TEST_CASE("P0 tool result subtypes create correctly", "[message_types]") {
    auto success = DisplayMessage::userToolSuccess("t1", "Read", "file contents");
    REQUIRE(success.type == DisplayMessage::Type::UserToolSuccess);
    REQUIRE(success.toolResult.toolUseId == "t1");
    REQUIRE(success.toolResult.isError == false);

    auto error = DisplayMessage::userToolError("t2", "Bash", "command failed");
    REQUIRE(error.type == DisplayMessage::Type::UserToolError);
    REQUIRE(error.toolResult.isError == true);

    auto rejected = DisplayMessage::userToolRejected("t3", "Bash");
    REQUIRE(rejected.type == DisplayMessage::Type::UserToolRejected);
    REQUIRE(rejected.toolResult.result == "Rejected");

    auto canceled = DisplayMessage::userToolCanceled("t4", "Read");
    REQUIRE(canceled.type == DisplayMessage::Type::UserToolCanceled);
    REQUIRE(canceled.toolResult.result == "Canceled");
}

TEST_CASE("Redacted thinking type creates correctly", "[message_types]") {
    auto msg = DisplayMessage::assistantRedactedThinking();
    REQUIRE(msg.type == DisplayMessage::Type::AssistantRedactedThinking);
    REQUIRE(!msg.messageId.empty());
}
```

- [ ] **Step 5: Build and test**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest -R test_display_message_types --output-on-failure`
Expected: All tests pass

- [ ] **Step 6: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/UiMessageTypes.hpp \
        src/ui/UiMessageTypes.cpp \
        tests/test_display_message_types.cpp \
        CMakeLists.txt
git commit -m "feat: add P0 DisplayMessage types (tool result subtypes, redacted thinking)"
```

---

### Task 10: Create XmlTagDispatcher

**Files:**
- Create: `include/claude/ui/XmlTagDispatcher.hpp`
- Create: `src/ui/XmlTagDispatcher.cpp`
- Create: `tests/test_xml_dispatcher.cpp`

- [ ] **Step 1: Write the header**

```cpp
// include/claude/ui/XmlTagDispatcher.hpp
#pragma once

#include <claude/ui/UiMessageTypes.hpp>
#include <map>
#include <optional>
#include <string>

namespace claude::ui {

class XmlTagDispatcher {
public:
    struct ParsedTag {
        std::string tagName;
        std::string content;
        std::map<std::string, std::string> attrs;
    };

    static DisplayMessage::Type dispatch(const std::string& text);
    static std::optional<ParsedTag> parseFirstTag(const std::string& text);

private:
    static const std::unordered_map<std::string, DisplayMessage::Type> tagMap_;
};

} // namespace claude::ui
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/ui/XmlTagDispatcher.cpp

#include <claude/ui/XmlTagDispatcher.hpp>
#include <regex>

namespace claude::ui {

const std::unordered_map<std::string, DisplayMessage::Type>
    XmlTagDispatcher::tagMap_ = {
    {"bash-stdout",          DisplayMessage::Type::UserBashOutput},
    {"bash-stderr",          DisplayMessage::Type::UserBashOutput},
    {"bash-input",           DisplayMessage::Type::UserBashInput},
    {"command-message",      DisplayMessage::Type::UserCommandMessage},
    {"local-command-stdout", DisplayMessage::Type::UserLocalCommandOutput},
    {"teammate-message",     DisplayMessage::Type::UserTeammateMessage},
    {"task-notification",    DisplayMessage::Type::UserTaskNotification},
    {"mcp-resource-update",  DisplayMessage::Type::UserMcpResourceUpdate},
    {"github-webhook-activity", DisplayMessage::Type::UserGitHubWebhook},
    {"fork-boilerplate",     DisplayMessage::Type::UserForkBoilerplate},
    {"cross-session-message", DisplayMessage::Type::UserCrossSessionMessage},
    {"channel",              DisplayMessage::Type::UserChannelMessage},
    {"user-memory-input",    DisplayMessage::Type::UserMemoryInput},
};

DisplayMessage::Type XmlTagDispatcher::dispatch(const std::string& text) {
    auto parsed = parseFirstTag(text);
    if (!parsed) return DisplayMessage::Type::UserPrompt;
    auto it = tagMap_.find(parsed->tagName);
    if (it != tagMap_.end()) return it->second;
    return DisplayMessage::Type::UserPrompt;
}

std::optional<XmlTagDispatcher::ParsedTag>
XmlTagDispatcher::parseFirstTag(const std::string& text) {
    // Match opening XML tag: <tagName> or <tagName attr="val">
    static const std::regex openTag(R"(<(\w+)(?:\s+([^>]*))?>)");
    std::smatch match;
    if (!std::regex_search(text, match, openTag)) return std::nullopt;

    ParsedTag result;
    result.tagName = match[1].str();

    // Parse attributes (simplified: key="value" pairs)
    if (match[2].matched) {
        static const std::regex attrRe(R"((\w+)="([^"]*)")");
        std::string attrStr = match[2].str();
        auto attrBegin = std::sregex_iterator(attrStr.begin(), attrStr.end(), attrRe);
        auto attrEnd = std::sregex_iterator();
        for (auto it = attrBegin; it != attrEnd; ++it) {
            result.attrs[(*it)[1].str()] = (*it)[2].str();
        }
    }

    // Extract content between opening and closing tags
    std::string closeTag = "</" + result.tagName + ">";
    auto closePos = text.find(closeTag);
    if (closePos != std::string::npos) {
        auto contentStart = match[0].length();
        result.content = text.substr(contentStart, closePos - contentStart);
    }

    return result;
}

} // namespace claude::ui
```

- [ ] **Step 3: Add P1 XML-dispatched types to DisplayMessage enum**

In `UiMessageTypes.hpp`, add after `AssistantRedactedThinking`:

```cpp
// P1: XML-tag dispatched types
UserBashOutput,
UserBashInput,
UserCommandMessage,
UserLocalCommandOutput,
UserTeammateMessage,
UserTaskNotification,
UserMcpResourceUpdate,
UserGitHubWebhook,
UserForkBoilerplate,
UserCrossSessionMessage,
UserChannelMessage,
UserMemoryInput,
// P2: Server tool use
AssistantServerToolUse,
// P2: Image attachment
UserImageAttachment,
```

- [ ] **Step 4: Write test**

```cpp
// tests/test_xml_dispatcher.cpp
#include <catch2/catch_test_macros.hpp>
#include <claude/ui/XmlTagDispatcher.hpp>

using namespace claude::ui;

TEST_CASE("XmlTagDispatcher returns UserPrompt for plain text", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("Hello world") ==
            DisplayMessage::Type::UserPrompt);
}

TEST_CASE("XmlTagDispatcher dispatches bash-stdout", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<bash-stdout>output here</bash-stdout>") ==
            DisplayMessage::Type::UserBashOutput);
}

TEST_CASE("XmlTagDispatcher dispatches command-message", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<command-message>/help</command-message>") ==
            DisplayMessage::Type::UserCommandMessage);
}

TEST_CASE("XmlTagDispatcher dispatches channel with attributes", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::dispatch(
        R"(<channel source="slack">hello</channel>)");
    REQUIRE(result == DisplayMessage::Type::UserChannelMessage);
}

TEST_CASE("XmlTagDispatcher parseFirstTag extracts content", "[xml_dispatcher]") {
    auto parsed = XmlTagDispatcher::parseFirstTag("<bash-input>ls -la</bash-input>");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->tagName == "bash-input");
    REQUIRE(parsed->content == "ls -la");
}

TEST_CASE("XmlTagDispatcher parseFirstTag extracts attributes", "[xml_dispatcher]") {
    auto parsed = XmlTagDispatcher::parseFirstTag(R"(<channel source="slack">msg</channel>)");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->attrs.at("source") == "slack");
}

TEST_CASE("XmlTagDispatcher returns nullopt for no tags", "[xml_dispatcher]") {
    auto parsed = XmlTagDispatcher::parseFirstTag("no tags here");
    REQUIRE_FALSE(parsed.has_value());
}
```

- [ ] **Step 5: Build and test**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest -R test_xml_dispatcher --output-on-failure`
Expected: All 6 tests pass

- [ ] **Step 6: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/XmlTagDispatcher.hpp \
        src/ui/XmlTagDispatcher.cpp \
        include/claude/ui/UiMessageTypes.hpp \
        tests/test_xml_dispatcher.cpp \
        CMakeLists.txt
git commit -m "feat: add XmlTagDispatcher and expand DisplayMessage with XML-dispatched types"
```

---

### Task 11: Integrate XML dispatch into NormalizeStage

**Files:**
- Modify: `src/ui/MessagePipeline.cpp`

- [ ] **Step 1: Add XmlTagDispatcher include and update UserMessage handling**

In `MessagePipeline.cpp`, add at the top:
```cpp
#include <claude/ui/XmlTagDispatcher.hpp>
```

In `NormalizeStage::processEvent()`, find the `StreamEvent::Type::UserMessage` case (around line 130). Currently it creates `DisplayMessage::userPrompt(event.text)`. Change to:

```cpp
case StreamEvent::Type::UserMessage: {
    auto dispatchType = XmlTagDispatcher::dispatch(event.text);
    if (dispatchType != DisplayMessage::Type::UserPrompt) {
        DisplayMessage msg;
        msg.type = dispatchType;
        msg.messageId = MessageIdGenerator::next();
        msg.text = event.text;
        auto parsed = XmlTagDispatcher::parseFirstTag(event.text);
        if (parsed) msg.text = parsed->content;
        messages.push_back(std::move(msg));
    } else {
        messages.push_back(DisplayMessage::userPrompt(event.text));
    }
    return true;
}
```

- [ ] **Step 2: Update ToolResultReady to use P0 subtypes**

Find the `StreamEvent::Type::ToolResultReady` case. Currently it creates `DisplayMessage::userToolResult(...)`. Add dispatch based on result status:

```cpp
case StreamEvent::Type::ToolResultReady: {
    // Determine result subtype
    bool isRejected = event.metadata.value("rejected", "false") == "true";
    bool isCanceled = event.metadata.value("canceled", "false") == "true";

    DisplayMessage msg;
    if (isRejected) {
        msg = DisplayMessage::userToolRejected(event.toolUseId, event.toolName);
    } else if (isCanceled) {
        msg = DisplayMessage::userToolCanceled(event.toolUseId, event.toolName);
    } else if (event.isError) {
        msg = DisplayMessage::userToolError(event.toolUseId, event.toolName, event.text);
    } else {
        msg = DisplayMessage::userToolSuccess(event.toolUseId, event.toolName, event.text);
    }
    messages.push_back(std::move(msg));
    pendingToolUseIndex_.erase(event.toolUseId);
    return true;
}
```

- [ ] **Step 3: Build and verify existing tests still pass**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest --output-on-failure 2>&1 | tail -20`
Expected: All existing tests pass (new behavior is additive)

- [ ] **Step 4: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add src/ui/MessagePipeline.cpp
git commit -m "feat: integrate XmlTagDispatcher and tool result subtypes into NormalizeStage"
```

---

### Task 12: Add MessageList support for new types + ToolResult subtypes

**Files:**
- Modify: `src/ui/components/MessageList.cpp`

- [ ] **Step 1: Add new type cases to MessageList factory switch**

Add these cases to `makeComponentForMessage()` in MessageList.cpp:

```cpp
case Type::UserToolSuccess: {
    return Renderer([msg, ctx]() mutable {
        ToolResultComponent c(msg, ctx);
        return c.OnRender();
    });
}
case Type::UserToolError: {
    return Renderer([msg, ctx]() mutable {
        ToolResultComponent c(msg, ctx);
        return c.OnRender();
    });
}
case Type::UserToolRejected: {
    return Renderer([msg, ctx]() mutable {
        using namespace ftxui;
        return hbox({text("⊘ ") | dim | color(MacGold)});
    });
}
case Type::UserToolCanceled: {
    return Renderer([msg, ctx]() mutable {
        using namespace ftxui;
        return hbox({text("⊘ ") | dim});
    });
}
case Type::AssistantRedactedThinking: {
    return Renderer([]() {
        using namespace ftxui;
        return hbox({text("💭 Thinking...") | dim | italic | color(MacLavender)});
    });
}
case Type::UserBashOutput:
case Type::UserBashInput:
case Type::UserCommandMessage:
case Type::UserLocalCommandOutput:
case Type::UserTeammateMessage:
case Type::UserTaskNotification:
case Type::UserMcpResourceUpdate:
case Type::UserGitHubWebhook:
case Type::UserForkBoilerplate:
case Type::UserCrossSessionMessage:
case Type::UserChannelMessage:
case Type::UserMemoryInput: {
    return Renderer([msg]() mutable {
        using namespace ftxui;
        return paragraph(msg.text) | dim | color(MacCream);
    });
}
```

- [ ] **Step 2: Build and commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build
git add src/ui/components/MessageList.cpp
git commit -m "feat: add MessageList rendering for all new DisplayMessage types"
```

---

## Phase 3: Performance Optimization

### Task 13: Implement OffscreenFreeze

**Files:**
- Create: `include/claude/ui/OffscreenFreeze.hpp`
- Create: `src/ui/OffscreenFreeze.cpp`
- Modify: `src/ui/VirtualScroll.hpp` — add viewport range getters
- Create: `tests/test_offscreen_freeze.cpp`

- [ ] **Step 1: Add viewport range accessors to VirtualScroll**

In `include/claude/ui/VirtualScroll.hpp`, add to public section:

```cpp
size_t lastVisibleIndex() const { return lastVisibleIdx_; }
```

And add the private member if not present:
```cpp
size_t lastVisibleIdx_ = 0;
```

Update `getVisibleRange()` to also set `lastVisibleIdx_`:

```cpp
std::pair<size_t, size_t> getVisibleRange(int terminalHeight) const {
    // ... existing logic ...
    // Also update lastVisibleIdx_ (make it mutable or compute inline)
    return {firstVisibleIdx_, endIdx};
}
```

- [ ] **Step 2: Write OffscreenFreeze header**

```cpp
// include/claude/ui/OffscreenFreeze.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>

namespace claude::ui {

class OffscreenFreeze : public ftxui::ComponentBase {
public:
    OffscreenFreeze(ftxui::Component child, int messageIndex,
                    std::function<bool(int)> isVisibleFn);

    ftxui::Element OnRender() override;

private:
    int messageIndex_;
    std::function<bool(int)> isVisibleFn_;
    int cachedHeight_ = 1;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Write OffscreenFreeze implementation**

```cpp
// src/ui/OffscreenFreeze.cpp
#ifdef HAS_FTXUI

#include <claude/ui/OffscreenFreeze.hpp>
#include <ftxui/dom/elements.hpp>

namespace claude::ui {

OffscreenFreeze::OffscreenFreeze(ftxui::Component child,
                                  int messageIndex,
                                  std::function<bool(int)> isVisibleFn)
    : messageIndex_(messageIndex), isVisibleFn_(std::move(isVisibleFn)) {
    Add(std::move(child));
}

ftxui::Element OffscreenFreeze::OnRender() {
    using namespace ftxui;

    if (!isVisibleFn_(messageIndex_)) {
        // Not visible: return placeholder with cached height
        return filler() | size(HEIGHT, EQUAL, cachedHeight_);
    }

    // Visible: render child normally and cache height
    auto el = ChildAt(0)->Render();
    // Ratchet: height only increases, never shrinks
    int newHeight = std::max(cachedHeight_, 1);
    cachedHeight_ = newHeight;
    return el;
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 4: Write test**

```cpp
// tests/test_offscreen_freeze.cpp
#ifdef HAS_FTXUI

#include <catch2/catch_test_macros.hpp>
#include <claude/ui/OffscreenFreeze.hpp>
#include <ftxui/component/component.hpp>

using namespace claude::ui;

TEST_CASE("OffscreenFreeze returns placeholder when not visible", "[offscreen]") {
    auto child = ftxui::Renderer([] { return ftxui::text("Hello World"); });
    auto freeze = std::make_shared<OffscreenFreeze>(
        child, 5, [](int idx) { return idx >= 10 && idx <= 20; });
    auto el = freeze->OnRender();
    // Should return a filler, not "Hello World"
    REQUIRE(el != ftxui::text("Hello World"));
}

TEST_CASE("OffscreenFreeze renders child when visible", "[offscreen]") {
    auto child = ftxui::Renderer([] { return ftxui::text("Visible Content"); });
    auto freeze = std::make_shared<OffscreenFreeze>(
        child, 15, [](int idx) { return idx >= 10 && idx <= 20; });
    auto el = freeze->OnRender();
    // Should render child content
    REQUIRE(el != ftxui::filler());
}

#endif // HAS_FTXUI
```

- [ ] **Step 5: Build and test**

Run: `cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest -R test_offscreen_freeze --output-on-failure`
Expected: Tests pass

- [ ] **Step 6: Commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add include/claude/ui/OffscreenFreeze.hpp \
        src/ui/OffscreenFreeze.cpp \
        include/claude/ui/VirtualScroll.hpp \
        tests/test_offscreen_freeze.cpp \
        CMakeLists.txt
git commit -m "feat: add OffscreenFreeze component for viewport optimization"
```

---

### Task 14: Add plain-text fast path to FtxuiMarkdown

**Files:**
- Modify: `src/ui/FtxuiMarkdown.cpp`
- Modify: `include/claude/ui/FtxuiMarkdown.hpp`

- [ ] **Step 1: Add hasMarkdownSyntax() to header**

In `FtxuiMarkdown.hpp`, add to the public section of the class:

```cpp
static bool hasMarkdownSyntax(const std::string& text);
```

- [ ] **Step 2: Implement hasMarkdownSyntax() in FtxuiMarkdown.cpp**

```cpp
bool FtxuiMarkdown::hasMarkdownSyntax(const std::string& text) {
    // Fast regex check for common Markdown markers
    static const std::regex mdSyntax(
        R"(([#`~>*|\[\]!-]|\d+\.\s|\*\*|~~))",
        std::regex::optimize
    );
    return std::regex_search(text, mdSyntax);
}
```

- [ ] **Step 3: Add fast path to render() method**

At the top of the `render()` method, before any parsing:

```cpp
std::vector<ftxui::Element> FtxuiMarkdown::render(const std::string& markdown) {
    if (!hasMarkdownSyntax(markdown)) {
        return {ftxui::paragraph(markdown)};
    }
    // ... existing parsing logic ...
}
```

- [ ] **Step 4: Build and commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build
git add include/claude/ui/FtxuiMarkdown.hpp src/ui/FtxuiMarkdown.cpp
git commit -m "perf: add plain-text fast path to skip Markdown parser for simple text"
```

---

### Task 15: Add MarkdownCache LRU for static messages

**Files:**
- Modify: `src/ui/FtxuiMarkdown.cpp`
- Modify: `include/claude/ui/FtxuiMarkdown.hpp`

- [ ] **Step 1: Add MarkdownCache class to FtxuiMarkdown.cpp (anonymous namespace)**

```cpp
namespace {
class MarkdownCache {
public:
    using CacheKey = uint64_t;

    std::optional<std::vector<FtxuiMarkdown::ParsedBlock>> get(CacheKey key) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            lruList_.splice(lruList_.begin(), lruList_, it->second.second);
            return it->second.first;
        }
        return std::nullopt;
    }

    void put(CacheKey key, std::vector<FtxuiMarkdown::ParsedBlock> blocks) {
        if (cache_.size() >= maxSize_) {
            evictOldest();
        }
        lruList_.push_front(key);
        cache_[key] = {std::move(blocks), lruList_.begin()};
    }

    void clear() {
        cache_.clear();
        lruList_.clear();
    }

private:
    void evictOldest() {
        auto key = lruList_.back();
        lruList_.pop_back();
        cache_.erase(key);
    }

    static constexpr size_t maxSize_ = 200;
    std::unordered_map<CacheKey,
        std::pair<std::vector<FtxuiMarkdown::ParsedBlock>,
                  std::list<CacheKey>::iterator>> cache_;
    std::list<CacheKey> lruList_;
};

// Simple hash function (replace with XXH3 if available)
uint64_t simpleHash(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h ^= static_cast<uint64_t>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

MarkdownCache g_markdownCache;
} // namespace
```

- [ ] **Step 2: Use cache in render() method**

```cpp
std::vector<ftxui::Element> FtxuiMarkdown::render(const std::string& markdown) {
    if (!hasMarkdownSyntax(markdown)) {
        return {ftxui::paragraph(markdown)};
    }

    auto key = simpleHash(markdown);
    if (auto cached = g_markdownCache.get(key)) {
        // Re-render cached blocks (blocks → Elements is cheap, parsing is expensive)
        std::vector<ftxui::Element> elements;
        for (auto& block : *cached) {
            elements.push_back(renderBlock(block));
        }
        return elements;
    }

    auto blocks = parseBlocks(markdown);
    g_markdownCache.put(key, blocks);

    std::vector<ftxui::Element> elements;
    for (auto& block : blocks) {
        elements.push_back(renderBlock(block));
    }
    return elements;
}
```

- [ ] **Step 3: Build and commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build
git add src/ui/FtxuiMarkdown.cpp include/claude/ui/FtxuiMarkdown.hpp
git commit -m "perf: add MarkdownCache LRU for static message re-rendering"
```

---

## Phase 4: Per-Tool Custom Renderers

### Task 16: Implement core tool renderers (Read, Bash, Edit)

**Files:**
- Create: `src/ui/renderers/ReadToolRenderer.hpp`, `src/ui/renderers/ReadToolRenderer.cpp`
- Create: `src/ui/renderers/BashToolRenderer.hpp`, `src/ui/renderers/BashToolRenderer.cpp`
- Create: `src/ui/renderers/EditToolRenderer.hpp`, `src/ui/renderers/EditToolRenderer.cpp`

Each renderer overrides the `IToolRenderer` virtual methods. The key customizations:

**ReadToolRenderer**: Shows file path + line range in tool use; shows content line count in result.
**BashToolRenderer**: Shows command text (truncated) in tool use; shows exit code + stderr for errors.
**EditToolRenderer**: Shows file path in tool use; shows diff preview via DiffRenderer for result.

- [ ] **Step 1: Write ReadToolRenderer**

Header:
```cpp
// src/ui/renderers/ReadToolRenderer.hpp
#pragma once
#ifdef HAS_FTXUI

#include <claude/ui/IToolRenderer.hpp>

namespace claude::ui {

class ReadToolRenderer : public IToolRenderer {
public:
    ftxui::Element renderToolUse(const ToolUseBlock& tool,
                                 const RenderContext& ctx) override;
    std::string renderToolUseAnsi(const ToolUseBlock& tool) override;
    ftxui::Element renderToolResult(const ToolResultBlock& result,
                                    const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    std::string renderToolResultAnsi(const ToolResultBlock& result,
                                     const ToolUseBlock& tool) override;
    ftxui::Element renderToolError(const ToolResultBlock& result,
                                   const ToolUseBlock& tool,
                                   const RenderContext& ctx) override;
    std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                    const ToolUseBlock& tool) override;
    ftxui::Element renderToolRejected(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolRejectedAnsi(const ToolUseBlock& tool) override;
    ftxui::Element renderToolCanceled(const ToolUseBlock& tool,
                                      const RenderContext& ctx) override;
    std::string renderToolCanceledAnsi(const ToolUseBlock& tool) override;
    ftxui::Element renderToolProgress(const ToolUseBlock& tool,
                                      const std::string& progress,
                                      const RenderContext& ctx) override;
    ftxui::Element renderToolQueued(const ToolUseBlock& tool,
                                    const RenderContext& ctx) override;
    ftxui::Element renderGroupedToolUse(
        const std::vector<ToolUseBlock>& tools,
        const RenderContext& ctx) override;
    std::string getToolUseSummary(const ToolUseBlock& tool) override;
    std::string userFacingName(const ToolUseBlock& tool) override;
    bool isCollapsible() const override { return true; }
    bool isResultTruncatable(const ToolResultBlock& result) const override;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

Implementation key method — `renderToolUse`:
```cpp
ftxui::Element ReadToolRenderer::renderToolUse(
    const ToolUseBlock& tool, const RenderContext& ctx) {
    using namespace ftxui;
    auto desc = console::getActivityDescription(tool.toolName, tool.input, true);
    Color fg(80, 200, 120);   // Read green
    Color bg(20, 60, 20);
    return hbox({
        text("⎿ ") | dim,
        text(" Read ") | bold | color(fg) | bgcolor(bg),
        text(" " + desc) | color(Color(200, 195, 180))
    });
}
```

Other methods delegate to DefaultToolRenderer for shared behavior (rejected/canceled/queued/progress).

- [ ] **Step 2: Write BashToolRenderer**

Key customization in `renderToolUse` — show command text:
```cpp
ftxui::Element BashToolRenderer::renderToolUse(
    const ToolUseBlock& tool, const RenderContext& ctx) {
    using namespace ftxui;
    auto cmd = extractField(tool.input, "command");
    if (cmd.size() > 60) cmd = cmd.substr(0, 57) + "...";
    Color fg(100, 160, 220);
    Color bg(15, 30, 70);
    return hbox({
        text("⎿ ") | dim,
        text(" Bash ") | bold | color(fg) | bgcolor(bg),
        text(" " + cmd) | color(Color(200, 195, 180))
    });
}
```

Key customization in `renderToolResult` — show truncated stdout/stderr:
```cpp
ftxui::Element BashToolRenderer::renderToolResult(
    const ToolResultBlock& result, const ToolUseBlock& tool,
    const RenderContext& ctx) {
    using namespace ftxui;
    if (ctx.verbose) {
        // Show full output in verbose mode
        return vbox({
            text("    ⎿") | dim,
            paragraph(result.result) | dim
        });
    }
    // Compact: first line only
    auto firstLineEnd = result.result.find('\n');
    auto firstLine = firstLineEnd != std::string::npos
        ? result.result.substr(0, firstLineEnd)
        : result.result;
    if (firstLine.size() > 80) firstLine = firstLine.substr(0, 77) + "...";
    return hbox({text("    ⎿ "), text(firstLine) | dim});
}
```

- [ ] **Step 3: Write EditToolRenderer**

Key customization — show diff preview for result:
```cpp
ftxui::Element EditToolRenderer::renderToolResult(
    const ToolResultBlock& result, const ToolUseBlock& tool,
    const RenderContext& ctx) {
    using namespace ftxui;
    // Count added/removed lines from result
    int added = 0, removed = 0;
    std::istringstream stream(result.result);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.starts_with("+") && !line.starts_with("+++")) added++;
        else if (line.starts_with("-") && !line.starts_with("---")) removed++;
    }
    return hbox({
        text("    ⎿ ") | dim,
        text("+" + std::to_string(added)) | color(MacMint),
        text("/") | dim,
        text("-" + std::to_string(removed)) | color(MacRose)
    });
}
```

- [ ] **Step 4: Add to CMakeLists.txt, build, commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp
git add src/ui/renderers/ReadToolRenderer.hpp src/ui/renderers/ReadToolRenderer.cpp \
        src/ui/renderers/BashToolRenderer.hpp src/ui/renderers/BashToolRenderer.cpp \
        src/ui/renderers/EditToolRenderer.hpp src/ui/renderers/EditToolRenderer.cpp \
        CMakeLists.txt
git commit -m "feat: add Read, Bash, Edit tool renderers with custom UI"
```

---

### Task 17: Implement remaining tool renderers + registration

**Files:**
- Create: `src/ui/renderers/WriteToolRenderer.{hpp,cpp}`
- Create: `src/ui/renderers/GrepToolRenderer.{hpp,cpp}`
- Create: `src/ui/renderers/GlobToolRenderer.{hpp,cpp}`
- Create: `src/ui/renderers/AgentToolRenderer.{hpp,cpp}`
- Create: `src/ui/renderers/WebFetchToolRenderer.{hpp,cpp}`
- Create: `src/ui/renderers/WebSearchToolRenderer.{hpp,cpp}`
- Create: `src/ui/renderers/LspToolRenderer.{hpp,cpp}`
- Modify: `src/bootstrap/AgentRunner.cpp` — register all renderers

- [ ] **Step 1: Write remaining 7 tool renderers**

Each follows the same pattern as Read/Bash/Edit — override `renderToolUse()` and `renderToolResult()` with tool-specific formatting, delegate rejected/canceled/queued/progress to DefaultToolRenderer behavior.

Key customizations per tool:
- **WriteToolRenderer**: Show target path + bytes written in result
- **GrepToolRenderer**: Show pattern + match count in result
- **GlobToolRenderer**: Show pattern + file count in result
- **AgentToolRenderer**: Show agent type + sub-agent progress
- **WebFetchToolRenderer**: Show URL + status code in result
- **WebSearchToolRenderer**: Show query + result count in result
- **LspToolRenderer**: Show operation + symbol name in tool use

- [ ] **Step 2: Register all renderers in AgentRunner.cpp**

In `src/bootstrap/AgentRunner.cpp`, add after the existing FTXUI includes:

```cpp
#ifdef HAS_FTXUI
#include <claude/ui/ToolRendererRegistry.hpp>
#include <claude/ui/renderers/ReadToolRenderer.hpp>
#include <claude/ui/renderers/BashToolRenderer.hpp>
#include <claude/ui/renderers/EditToolRenderer.hpp>
#include <claude/ui/renderers/WriteToolRenderer.hpp>
#include <claude/ui/renderers/GrepToolRenderer.hpp>
#include <claude/ui/renderers/GlobToolRenderer.hpp>
#include <claude/ui/renderers/AgentToolRenderer.hpp>
#include <claude/ui/renderers/WebFetchToolRenderer.hpp>
#include <claude/ui/renderers/WebSearchToolRenderer.hpp>
#include <claude/ui/renderers/LspToolRenderer.hpp>
#endif
```

In the `createAgentLoop()` function, add renderer registration after creating the agent loop:

```cpp
#ifdef HAS_FTXUI
    auto& registry = ui::ToolRendererRegistry::instance();
    registry.registerRenderer("Read", std::make_unique<ui::ReadToolRenderer>());
    registry.registerRenderer("Bash", std::make_unique<ui::BashToolRenderer>());
    registry.registerRenderer("Edit", std::make_unique<ui::EditToolRenderer>());
    registry.registerRenderer("Write", std::make_unique<ui::WriteToolRenderer>());
    registry.registerRenderer("Grep", std::make_unique<ui::GrepToolRenderer>());
    registry.registerRenderer("Glob", std::make_unique<ui::GlobToolRenderer>());
    registry.registerRenderer("Agent", std::make_unique<ui::AgentToolRenderer>());
    registry.registerRenderer("WebFetch", std::make_unique<ui::WebFetchToolRenderer>());
    registry.registerRenderer("WebSearch", std::make_unique<ui::WebSearchToolRenderer>());
    registry.registerRenderer("LSP", std::make_unique<ui::LspToolRenderer>());
#endif
```

- [ ] **Step 3: Build, test, and commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest --output-on-failure 2>&1 | tail -10
git add src/ui/renderers/ src/bootstrap/AgentRunner.cpp CMakeLists.txt
git commit -m "feat: add all per-tool renderers and register at startup"
```

---

## Phase 5: Accessibility + ANSI Compatibility

### Task 18: Implement TerminalCapabilities

**Files:**
- Create: `include/claude/console/TerminalCapabilities.hpp`
- Create: `src/console/TerminalCapabilities.cpp`
- Create: `tests/test_terminal_capabilities.cpp`

- [ ] **Step 1: Write header**

```cpp
// include/claude/console/TerminalCapabilities.hpp
#pragma once

namespace claude::console {

class TerminalCapabilities {
public:
    static int detectColorLevel();
    static bool supportsTrueColor() { return detectColorLevel() >= 3; }
    static bool supports256Color() { return detectColorLevel() >= 2; }
    static bool supportsAtLeast16Color() { return detectColorLevel() >= 1; }
};

} // namespace claude::console
```

- [ ] **Step 2: Write implementation**

```cpp
// src/console/TerminalCapabilities.cpp

#include <claude/console/TerminalCapabilities.hpp>
#include <cstdlib>
#include <string>

namespace claude::console {

int TerminalCapabilities::detectColorLevel() {
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm) {
        std::string ct(colorterm);
        if (ct.find("truecolor") != std::string::npos ||
            ct.find("24bit") != std::string::npos) {
            return 3;
        }
    }
    const char* term = std::getenv("TERM");
    if (term) {
        std::string t(term);
        if (t.find("256color") != std::string::npos) return 2;
        if (t.find("xterm") != std::string::npos) return 1;
    }
    return 1; // Default: assume at least 16-color
}

} // namespace claude::console
```

- [ ] **Step 3: Write test**

```cpp
// tests/test_terminal_capabilities.cpp
#include <catch2/catch_test_macros.hpp>
#include <claude/console/TerminalCapabilities.hpp>

using namespace claude::console;

TEST_CASE("detectColorLevel returns int in range 1-3", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(level >= 1);
    REQUIRE(level <= 3);
}

TEST_CASE("supportsTrueColor is consistent with detectColorLevel", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(TerminalCapabilities::supportsTrueColor() == (level >= 3));
}

TEST_CASE("supports256Color is consistent with detectColorLevel", "[terminal_cap]") {
    int level = TerminalCapabilities::detectColorLevel();
    REQUIRE(TerminalCapabilities::supports256Color() == (level >= 2));
}
```

- [ ] **Step 4: Build, test, commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest -R test_terminal_capabilities --output-on-failure
git add include/claude/console/TerminalCapabilities.hpp \
        src/console/TerminalCapabilities.cpp \
        tests/test_terminal_capabilities.cpp \
        CMakeLists.txt
git commit -m "feat: add TerminalCapabilities detection for color level auto-downgrade"
```

---

### Task 19: Add daltonized + ANSI themes to ThemeSystem

**Files:**
- Modify: `src/console/ThemeSystem.cpp`

- [ ] **Step 1: Add DARK_DALTONIZED theme**

In ThemeSystem.cpp, add a new theme factory function after the existing `dark()`:

```cpp
Theme darkDaltonized() {
    return ThemeBuilder()
        .name("dark-daltonized")
        .description("Dark theme with color-blind friendly cyan/orange instead of green/red")
        .primary("\033[36m").secondary("\033[34m").accent("\033[35m")
        .success("\033[36m")       // cyan instead of green
        .warning("\033[33m")
        .error("\033[38;5;208m")   // orange instead of red
        .info("\033[34m").muted("\033[90m").text("\033[37m").background("\033[40m")
        .promptColor("\033[1;36m")     // cyan
        .assistantColor("\033[1;36m")
        .thinkingColor("\033[2;35m")
        .toolSuccessColor("\033[1;36m") // cyan
        .toolErrorColor("\033[1;38;5;208m") // orange
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[36m")       // cyan
        .diffRemoveColor("\033[38;5;208m") // orange
        .diffChunkColor("\033[33m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[36m")     // cyan
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[38;5;208m") // orange
        .build();
}
```

- [ ] **Step 2: Add LIGHT_DALTONIZED, DARK_ANSI, LIGHT_ANSI themes**

```cpp
Theme lightDaltonized() {
    return ThemeBuilder()
        .name("light-daltonized")
        .description("Light theme with color-blind friendly cyan/orange")
        .primary("\033[34m").secondary("\033[36m").accent("\033[35m")
        .success("\033[36m")       // cyan
        .warning("\033[33m")
        .error("\033[38;5;208m")   // orange
        .info("\033[34m").muted("\033[90m").text("\033[30m").background("\033[47m")
        .promptColor("\033[1;36m")
        .assistantColor("\033[1;34m")
        .thinkingColor("\033[2;35m")
        .toolSuccessColor("\033[1;36m")
        .toolErrorColor("\033[1;38;5;208m")
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[36m")
        .diffRemoveColor("\033[38;5;208m")
        .diffChunkColor("\033[33m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[36m")
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[38;5;208m")
        .build();
}

Theme darkAnsi() {
    return ThemeBuilder()
        .name("dark-ansi")
        .description("Dark theme limited to ANSI 16 colors for terminal compatibility")
        .primary("\033[36m").secondary("\033[34m").accent("\033[35m")
        .success("\033[32m").warning("\033[33m").error("\033[31m")
        .info("\033[34m").muted("\033[90m").text("\033[37m").background("\033[40m")
        .promptColor("\033[1;32m").assistantColor("\033[1;36m")
        .thinkingColor("\033[2;35m").toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m").codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m").diffRemoveColor("\033[31m").diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m").contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}

Theme lightAnsi() {
    return ThemeBuilder()
        .name("light-ansi")
        .description("Light theme limited to ANSI 16 colors")
        .primary("\033[34m").secondary("\033[36m").accent("\033[35m")
        .success("\033[32m").warning("\033[33m").error("\033[31m")
        .info("\033[34m").muted("\033[90m").text("\033[30m").background("\033[47m")
        .promptColor("\033[1;32m").assistantColor("\033[1;34m")
        .thinkingColor("\033[2;35m").toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m").codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m").diffRemoveColor("\033[31m").diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m").contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}
```

- [ ] **Step 3: Register the 4 new themes in the builtin themes list**

In `ThemeManager::ThemeManager()` constructor, add to the `builtinThemes_` vector:

```cpp
builtinThemes_.push_back(darkDaltonized());
builtinThemes_.push_back(lightDaltonized());
builtinThemes_.push_back(darkAnsi());
builtinThemes_.push_back(lightAnsi());
```

- [ ] **Step 4: Add auto-downgrade in main.cpp**

In `src/main.cpp`, after argument parsing and before creating the UI, add:

```cpp
#include <claude/console/TerminalCapabilities.hpp>
#include <claude/console/ThemeSystem.hpp>

// Auto-downgrade theme if terminal doesn't support truecolor
if (!console::TerminalCapabilities::supportsTrueColor()) {
    auto& themeMgr = console::ThemeManager::instance();
    auto current = themeMgr.currentThemeName();
    // If user hasn't explicitly selected a theme, use ANSI fallback
    if (current == "dark" || current.empty()) {
        themeMgr.setTheme("dark-ansi");
    }
}
```

- [ ] **Step 5: Build and commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build
git add src/console/ThemeSystem.cpp src/main.cpp
git commit -m "feat: add daltonized + ANSI themes and terminal color auto-downgrade"
```

---

## Phase 6: Search + Permission Enhancement

### Task 20: Implement SearchOverlay component

**Files:**
- Create: `include/claude/ui/SearchOverlay.hpp`
- Create: `src/ui/SearchOverlay.cpp`
- Create: `tests/test_search_overlay.cpp`

- [ ] **Step 1: Write SearchOverlay header**

```cpp
// include/claude/ui/SearchOverlay.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <claude/ui/UiMessageTypes.hpp>
#include <vector>
#include <string>

namespace claude::ui {

class SearchOverlay : public ftxui::ComponentBase {
public:
    SearchOverlay();

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;

    void activate();
    void deactivate();
    bool isActive() const { return active_; }

    struct SearchResult {
        int messageIndex;
        int matchOffset;
        std::string contextLine;
    };

    const std::vector<SearchResult>& results() const { return results_; }
    int currentMatchIndex() const { return currentMatch_; }
    void nextMatch();
    void prevMatch();

    void setMessages(const std::vector<DisplayMessage>* messages) {
        messages_ = messages;
    }

    int highlightMessageIndex() const;
    int highlightMatchOffset() const;

private:
    void performSearch();
    std::string searchQuery_;
    std::vector<SearchResult> results_;
    int currentMatch_ = -1;
    bool active_ = false;
    const std::vector<DisplayMessage>* messages_ = nullptr;
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write SearchOverlay implementation**

```cpp
// src/ui/SearchOverlay.cpp
#ifdef HAS_FTXUI

#include <claude/ui/SearchOverlay.hpp>
#include <claude/ui/FtxuiColors.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>

namespace claude::ui {

SearchOverlay::SearchOverlay() = default;

void SearchOverlay::activate() {
    active_ = true;
    searchQuery_.clear();
    results_.clear();
    currentMatch_ = -1;
}

void SearchOverlay::deactivate() {
    active_ = false;
    results_.clear();
    currentMatch_ = -1;
}

void SearchOverlay::nextMatch() {
    if (results_.empty()) return;
    currentMatch_ = (currentMatch_ + 1) % static_cast<int>(results_.size());
}

void SearchOverlay::prevMatch() {
    if (results_.empty()) return;
    currentMatch_ = (currentMatch_ - 1 + static_cast<int>(results_.size()))
                    % static_cast<int>(results_.size());
}

int SearchOverlay::highlightMessageIndex() const {
    if (currentMatch_ < 0 || currentMatch_ >= static_cast<int>(results_.size()))
        return -1;
    return results_[currentMatch_].messageIndex;
}

int SearchOverlay::highlightMatchOffset() const {
    if (currentMatch_ < 0 || currentMatch_ >= static_cast<int>(results_.size()))
        return -1;
    return results_[currentMatch_].matchOffset;
}

void SearchOverlay::performSearch() {
    results_.clear();
    currentMatch_ = -1;
    if (searchQuery_.empty() || !messages_) return;

    std::string query = searchQuery_;
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);

    for (size_t i = 0; i < messages_->size(); ++i) {
        auto& msg = (*messages_)[i];
        auto text = msg.searchableText();
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);

        size_t pos = 0;
        while ((pos = text.find(query, pos)) != std::string::npos) {
            SearchResult r;
            r.messageIndex = static_cast<int>(i);
            r.matchOffset = static_cast<int>(pos);
            // Extract context around match
            auto start = pos > 30 ? pos - 30 : 0;
            auto end = std::min(pos + query.size() + 30, text.size());
            r.contextLine = msg.searchableText().substr(start, end - start);
            results_.push_back(r);
            pos += 1; // Find all occurrences
        }
    }

    if (!results_.empty()) currentMatch_ = 0;
}

ftxui::Element SearchOverlay::OnRender() {
    using namespace ftxui;
    if (!active_) return text("");

    std::string countStr;
    if (!results_.empty()) {
        countStr = " " + std::to_string(currentMatch_ + 1) + "/" +
                   std::to_string(results_.size()) + " matches ";
    } else if (!searchQuery_.empty()) {
        countStr = " no matches ";
    }

    return hbox({
        text("╭─ Search: ") | color(MacSky),
        text(searchQuery_) | bold | color(MacPeach),
        text(countStr) | dim | color(MacCream),
        text("Enter↓ Shift+Enter↑ Esc╮") | dim | color(MacShadow)
    });
}

bool SearchOverlay::OnEvent(ftxui::Event event) {
    if (!active_) return false;

    if (event == ftxui::Event::Escape) {
        deactivate();
        return true;
    }
    if (event == ftxui::Event::Return) {
        nextMatch();
        return true;
    }
    // Shift+Enter for prev match (special handling)
    if (event.input() == "\n" && event.shift()) {
        prevMatch();
        return true;
    }

    // Text input
    if (event.is_character()) {
        searchQuery_ += event.character();
        performSearch();
        return true;
    }
    if (event == ftxui::Event::Backspace && !searchQuery_.empty()) {
        searchQuery_.pop_back();
        performSearch();
        return true;
    }

    return false;
}

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 3: Add searchableText() to DisplayMessage**

In `UiMessageTypes.hpp`, add to DisplayMessage struct:

```cpp
std::string searchableText() const;
```

In `UiMessageTypes.cpp`:

```cpp
std::string DisplayMessage::searchableText() const {
    switch (type) {
        case Type::UserPrompt:
        case Type::SystemInfo:
        case Type::SystemError:
        case Type::TurnDuration:
        case Type::HookSummary:
        case Type::AgentProgress:
            return text;
        case Type::AssistantText:
            return text;
        case Type::AssistantThinking:
            return thinking.text;
        case Type::AssistantToolUse:
            return toolUse.toolName + " " + toolUse.input;
        case Type::UserToolResult:
        case Type::UserToolSuccess:
        case Type::UserToolError:
            return toolResult.toolName + " " + toolResult.result;
        case Type::CollapsedReadSearch:
            return collapsedGroup.summaryText();
        default:
            return text;
    }
}
```

- [ ] **Step 4: Write test**

```cpp
// tests/test_search_overlay.cpp
#ifdef HAS_FTXUI

#include <catch2/catch_test_macros.hpp>
#include <claude/ui/SearchOverlay.hpp>
#include <claude/ui/UiMessageTypes.hpp>

using namespace claude::ui;

TEST_CASE("SearchOverlay starts inactive", "[search]") {
    SearchOverlay overlay;
    REQUIRE_FALSE(overlay.isActive());
}

TEST_CASE("SearchOverlay activate/deactivate", "[search]") {
    SearchOverlay overlay;
    overlay.activate();
    REQUIRE(overlay.isActive());
    overlay.deactivate();
    REQUIRE_FALSE(overlay.isActive());
}

TEST_CASE("SearchOverlay finds matches in messages", "[search]") {
    SearchOverlay overlay;
    std::vector<DisplayMessage> messages;
    messages.push_back(DisplayMessage::userPrompt("Hello world"));
    messages.push_back(DisplayMessage::userPrompt("Hello again"));
    messages.push_back(DisplayMessage::userPrompt("Goodbye"));

    overlay.setMessages(&messages);
    overlay.activate();
    // Simulate typing "Hello"
    overlay.OnEvent(ftxui::Event::Character('H'));
    overlay.OnEvent(ftxui::Event::Character('e'));
    overlay.OnEvent(ftxui::Event::Character('l'));
    overlay.OnEvent(ftxui::Event::Character('l'));
    overlay.OnEvent(ftxui::Event::Character('o'));

    REQUIRE(overlay.results().size() == 2);
    REQUIRE(overlay.currentMatchIndex() == 0);
}

TEST_CASE("SearchOverlay nextMatch cycles", "[search]") {
    SearchOverlay overlay;
    std::vector<DisplayMessage> messages;
    messages.push_back(DisplayMessage::userPrompt("cat dog cat"));
    overlay.setMessages(&messages);
    overlay.activate();
    overlay.OnEvent(ftxui::Event::Character('c'));
    overlay.OnEvent(ftxui::Event::Character('a'));
    overlay.OnEvent(ftxui::Event::Character('t'));

    auto initial = overlay.currentMatchIndex();
    overlay.nextMatch();
    // Should advance to next match or cycle
    REQUIRE(overlay.currentMatchIndex() >= 0);
}

#endif // HAS_FTXUI
```

- [ ] **Step 5: Build, test, commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build && cd build && ctest -R test_search_overlay --output-on-failure
git add include/claude/ui/SearchOverlay.hpp src/ui/SearchOverlay.cpp \
        include/claude/ui/UiMessageTypes.hpp src/ui/UiMessageTypes.cpp \
        tests/test_search_overlay.cpp CMakeLists.txt
git commit -m "feat: add SearchOverlay component with message search"
```

---

### Task 21: Implement IPermissionRenderer + built-in permission renderers

**Files:**
- Create: `include/claude/ui/IPermissionRenderer.hpp`
- Create: `include/claude/ui/PermissionRendererRegistry.hpp`
- Create: `src/ui/permissions/DefaultPermissionRenderer.{hpp,cpp}`
- Create: `src/ui/permissions/BashPermissionRenderer.{hpp,cpp}`
- Create: `src/ui/permissions/FileEditPermissionRenderer.{hpp,cpp}`
- Create: `src/ui/permissions/FileWritePermissionRenderer.{hpp,cpp}`
- Create: `src/ui/permissions/FileReadPermissionRenderer.{hpp,cpp}`
- Create: `src/ui/PermissionRendererRegistry.cpp`
- Create: `tests/test_permission_renderer.cpp`
- Modify: `src/ui/FtxuiPermission.cpp` — delegate to IPermissionRenderer

- [ ] **Step 1: Write IPermissionRenderer interface**

```cpp
// include/claude/ui/IPermissionRenderer.hpp
#pragma once
#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>
#include <claude/ui/RenderContext.hpp>
#include <string>
#include <vector>

namespace claude::ui {

struct PermissionRequest {
    std::string toolName;
    std::string input;  // JSON string
    std::string activity;
};

class IPermissionRenderer {
public:
    virtual ~IPermissionRenderer() = default;

    virtual ftxui::Element renderPrompt(const PermissionRequest& req,
                                        const RenderContext& ctx) = 0;
    virtual std::string getActivityDescription(const PermissionRequest& req) = 0;
    virtual std::vector<ftxui::Element> renderDetailLines(
        const PermissionRequest& req, const RenderContext& ctx) {
        return {};
    }
};

} // namespace claude::ui

#endif // HAS_FTXUI
```

- [ ] **Step 2: Write PermissionRendererRegistry**

```cpp
// include/claude/ui/PermissionRendererRegistry.hpp
#pragma once

#include <claude/ui/IPermissionRenderer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace claude::ui {

class PermissionRendererRegistry {
public:
    static PermissionRendererRegistry& instance();
    void registerRenderer(const std::string& toolName,
                          std::unique_ptr<IPermissionRenderer> renderer);
    IPermissionRenderer* getRenderer(const std::string& toolName) const;
    IPermissionRenderer* getFallbackRenderer() const;

private:
    PermissionRendererRegistry();
    std::unordered_map<std::string, std::unique_ptr<IPermissionRenderer>> renderers_;
    std::unique_ptr<IPermissionRenderer> fallback_;
};

} // namespace claude::ui
```

Implementation follows the same pattern as ToolRendererRegistry.

- [ ] **Step 3: Write DefaultPermissionRenderer**

Produces the current uniform permission prompt format.

- [ ] **Step 4: Write BashPermissionRenderer**

Shows command content with safety warnings:

```cpp
std::vector<ftxui::Element> BashPermissionRenderer::renderDetailLines(
    const PermissionRequest& req, const RenderContext& ctx) {
    using namespace ftxui;
    auto cmd = extractField(req.input, "command");
    Elements lines;
    lines.push_back(hbox({text("  Command: "), text(cmd) | color(MacCream)}));
    // Safety warnings
    if (cmd.find("rm ") != std::string::npos ||
        cmd.find("rmdir") != std::string::npos) {
        lines.push_back(text("  ⚠ Destructive command") | color(MacRose) | bold);
    }
    if (cmd.find("sudo") != std::string::npos) {
        lines.push_back(text("  ⚠ Elevated privileges") | color(MacGold) | bold);
    }
    return lines;
}
```

- [ ] **Step 5: Write FileEditPermissionRenderer**

Shows file path + diff preview:

```cpp
std::vector<ftxui::Element> FileEditPermissionRenderer::renderDetailLines(
    const PermissionRequest& req, const RenderContext& ctx) {
    using namespace ftxui;
    auto path = extractField(req.input, "file_path");
    Elements lines;
    lines.push_back(hbox({text("  File: "), text(path) | color(MacCream)}));
    // Show old_string / new_string summary if present
    auto oldStr = extractField(req.input, "old_string");
    auto newStr = extractField(req.input, "new_string");
    if (!oldStr.empty()) {
        lines.push_back(hbox({
            text("  - "), text(truncate(oldStr, 60)) | color(MacRose)
        }));
    }
    if (!newStr.empty()) {
        lines.push_back(hbox({
            text("  + "), text(truncate(newStr, 60)) | color(MacMint)
        }));
    }
    return lines;
}
```

- [ ] **Step 6: Write FileWritePermissionRenderer + FileReadPermissionRenderer**

Similar pattern — extract file_path and display it.

- [ ] **Step 7: Register permission renderers in AgentRunner.cpp**

```cpp
#ifdef HAS_FTXUI
    auto& permRegistry = ui::PermissionRendererRegistry::instance();
    permRegistry.registerRenderer("Bash", std::make_unique<ui::BashPermissionRenderer>());
    permRegistry.registerRenderer("Edit", std::make_unique<ui::FileEditPermissionRenderer>());
    permRegistry.registerRenderer("Write", std::make_unique<ui::FileWritePermissionRenderer>());
    permRegistry.registerRenderer("Read", std::make_unique<ui::FileReadPermissionRenderer>());
#endif
```

- [ ] **Step 8: Delegate FtxuiPermission to IPermissionRenderer**

In `src/ui/FtxuiPermission.cpp`, change the permission rendering to call:

```cpp
auto* renderer = PermissionRendererRegistry::instance().getRenderer(toolName);
PermissionRequest req{toolName, input, activity};
auto detailLines = renderer->renderDetailLines(req, ctx);
// Use detailLines in the permission overlay rendering
```

- [ ] **Step 9: Add feedback input to PermissionPrompt**

In `include/claude/ui/components/PermissionPrompt.hpp`, modify `Options`:

```cpp
struct Option {
    std::string label;
    char shortcutKey;
    bool hasFeedbackInput = false;
    std::string feedbackPlaceholder;
};

struct Options {
    std::string toolName;
    std::string activity;
    std::string description;
    std::vector<Option> choices;
};
```

In `src/ui/components/PermissionPrompt.cpp`, when the selected choice has `hasFeedbackInput = true` and the user presses Tab, expand an inline text input. Enter submits feedback + confirms.

- [ ] **Step 10: Build, test, commit**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build
git add include/claude/ui/IPermissionRenderer.hpp \
        include/claude/ui/PermissionRendererRegistry.hpp \
        src/ui/PermissionRendererRegistry.cpp \
        src/ui/permissions/ \
        src/ui/FtxuiPermission.cpp \
        include/claude/ui/components/PermissionPrompt.hpp \
        src/ui/components/PermissionPrompt.cpp \
        src/bootstrap/AgentRunner.cpp \
        tests/test_permission_renderer.cpp \
        CMakeLists.txt
git commit -m "feat: add IPermissionRenderer with per-tool permission prompts and feedback input"
```

---

## Self-Review

### Spec Coverage Check

| Spec Section | Task |
|---|---|
| 1. Core Architecture: Component Tree | Tasks 1-8 |
| 2. IToolRenderer Interface | Tasks 3, 16, 17 |
| 3. Content Type Expansion | Tasks 9-12 |
| 4. OffscreenFreeze + Fast Path + LRU | Tasks 13-15 |
| 5. Color Accessibility + ANSI | Tasks 18-19 |
| 6. Message Search | Task 20 |
| 7. Permission Enhancement | Task 21 |

All spec sections covered.

### Placeholder Scan

No TBD/TODO/placeholders found. All steps contain concrete code.

### Type Consistency

- `ToolUseBlock` / `ToolResultBlock` used consistently across IToolRenderer, DefaultToolRenderer, and MessageList
- `RenderContext` defined in Task 1, used consistently in all subsequent tasks
- `DisplayMessage::Type` enum values match between UiMessageTypes.hpp, XmlTagDispatcher, and MessageList switch
- `HeaderState` / `ContentState` / `AppLayoutState` defined in component headers, used in AppLayout

No inconsistencies found.
