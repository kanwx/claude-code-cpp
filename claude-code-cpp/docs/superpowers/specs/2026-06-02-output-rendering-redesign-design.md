# Output Rendering Component-First Redesign

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Redesign the C++ claude-code-cpp output rendering architecture to match the original TypeScript Claude Code's component-based, tool-owned rendering system. Resolve the 9 critical gaps identified in the C++ vs TS comparison: MainComponent god-function, missing content types, no per-tool custom rendering, no offscreen freeze, no color accessibility, no ANSI 16-color fallback, no message search, no plain-text fast path, and no permission feedback input.

**Architecture:** Component-first FTXUI component tree (replacing the monolithic MainComponent::OnRender) + IToolRenderer abstraction with per-tool registration + expanded DisplayMessage type system with XML tag dispatch + OffscreenFreeze viewport optimization + accessibility-first theme additions.

**Tech Stack:** C++23, FTXUI (fullscreen TUI), ANSI escape sequences (readline fallback), nlohmann/json

**Builds on:** 2026-05-22-output-rendering-design.md (Tool render virtual methods, StreamingMarkdown, ThinkingVisibility), 2026-05-28-ui-alignment-design.md (Header, bottom layout, permission overlay)

---

## 1. Core Architecture: Component Tree Refactoring

### 1.1 Problem

`detail::MainComponent::OnRender()` is a 1356-line god function that handles all rendering inline — message type switching, streaming text, thinking spinner, permission overlay, input line, completions, and event handling. The existing `src/ui/components/` directory has standalone component classes (PermissionPromptComponent, ToolUseRenderer) but the main render path does not delegate to them.

### 1.2 Target component tree

```
FtxuiRepl
  └─ AppLayout (FTXUI Component)
       ├─ HeaderBar              — brand, context bar, model, tokens, cost, cwd, git, status
       ├─ ContentArea            — flex, yframe, OffscreenFreeze + VirtualScroll
       │    ├─ MessageList       — message loop, delegates to MessageComponent per message
       │    │    ├─ UserPromptComponent
       │    │    ├─ AssistantTextComponent
       │    │    ├─ AssistantThinkingComponent
       │    │    ├─ ToolUseComponent         — delegates to IToolRenderer
       │    │    ├─ ToolResultComponent      — delegates to IToolRenderer (4 subtypes)
       │    │    ├─ SystemInfoComponent
       │    │    ├─ SystemErrorComponent
       │    │    ├─ TurnDurationComponent
       │    │    ├─ CompactBoundaryComponent
       │    │    ├─ CollapsedReadSearchComponent
       │    │    ├─ GroupedToolUseComponent
       │    │    └─ AgentProgressComponent
       │    ├─ StreamingText      — StreamingMarkdown live rendering
       │    └─ ThinkingIndicator  — spinner + summary
       ├─ CompletionOverlay      — command/tab completions
       ├─ PermissionOverlay      — dbox() overlay, delegates to IPermissionRenderer
       ├─ InputLine              — cursor with inverted character
       └─ PromptInputFooter      — mode indicator, hints, auth status
```

### 1.3 MessageComponent base class

```cpp
class MessageComponent : public ComponentBase {
public:
    MessageComponent(const DisplayMessage& msg, const ThemeColors& theme)
        : msg_(msg), theme_(theme) {}

    virtual DisplayMessageType type() const = 0;

protected:
    const DisplayMessage& msg_;
    const ThemeColors& theme_;
};
```

### 1.4 MessageList factory

```cpp
class MessageList : public ComponentBase {
public:
    Component makeComponent(const DisplayMessage& msg, const ThemeColors& theme) {
        switch (msg.type) {
            case DisplayMessageType::UserPrompt:
                return Make<UserPromptComponent>(msg, theme);
            case DisplayMessageType::AssistantText:
                return Make<AssistantTextComponent>(msg, theme);
            case DisplayMessageType::AssistantToolUse:
                return Make<ToolUseComponent>(msg, theme);
            // ... all types
            default:
                return Make<DefaultMessageComponent>(msg, theme);
        }
    }
};
```

### 1.5 Event handling distribution

- Keyboard/mouse events propagate through the FTXUI component tree
- Each Component handles its own events via `OnEvent()`
- Global shortcuts (Ctrl+C, Ctrl+O, Ctrl+F) handled by AppLayout
- PermissionOverlay handles its own arrow/enter/escape events
- InputLine handles text input and cursor movement

### 1.6 Migration strategy

1. Create the new component classes in `src/ui/components/` with implementations extracted from MainComponent::OnRender()
2. Wire them into the component tree in AppLayout
3. Delete MainComponent::OnRender() and MainComponent::OnEvent()
4. Each step is independently testable — components render identically to the inline code they replace

### 1.7 Files

**Created:**
- `include/claude/ui/components/AppLayout.hpp`
- `include/claude/ui/components/HeaderBar.hpp`
- `include/claude/ui/components/ContentArea.hpp`
- `include/claude/ui/components/MessageList.hpp`
- `include/claude/ui/components/MessageComponent.hpp` (base class)
- `include/claude/ui/components/UserPromptComponent.hpp`
- `include/claude/ui/components/AssistantTextComponent.hpp`
- `include/claude/ui/components/AssistantThinkingComponent.hpp`
- `include/claude/ui/components/ToolUseComponent.hpp`
- `include/claude/ui/components/ToolResultComponent.hpp`
- `include/claude/ui/components/SystemInfoComponent.hpp`
- `include/claude/ui/components/SystemErrorComponent.hpp`
- `include/claude/ui/components/TurnDurationComponent.hpp`
- `include/claude/ui/components/CompactBoundaryComponent.hpp`
- `include/claude/ui/components/CollapsedReadSearchComponent.hpp`
- `include/claude/ui/components/GroupedToolUseComponent.hpp`
- `include/claude/ui/components/AgentProgressComponent.hpp`
- `include/claude/ui/components/StreamingText.hpp`
- `include/claude/ui/components/ThinkingIndicator.hpp`
- `include/claude/ui/components/CompletionOverlay.hpp`
- `include/claude/ui/components/PermissionOverlay.hpp`
- `include/claude/ui/components/InputLine.hpp`
- Corresponding `.cpp` files in `src/ui/components/`

**Modified:**
- `src/ui/FtxuiRepl.cpp` — replace MainComponent with AppLayout tree
- `src/ui/FtxuiRender.cpp` — extract inline rendering into component classes, then delete MainComponent
- `include/claude/ui/FtxuiRepl.hpp` — update to use new component tree

**Deleted:**
- `detail::MainComponent` class in `src/ui/FtxuiRender.cpp` (after extraction)

---

## 2. IToolRenderer Interface

### 2.1 Problem

All tools render with the same `⎿ [ToolBadge] ActivityDescription` format. TS allows each Tool class to fully customize its rendering via 12+ virtual methods. The 2026-05-22 spec added `renderToolUse()` / `renderToolResult()` virtual methods on Tool, but they lack: error/rejected/canceled rendering, progress/queued states, grouped rendering, summary extraction, and a registration mechanism.

### 2.2 IToolRenderer interface

```cpp
// include/claude/ui/IToolRenderer.hpp

class IToolRenderer {
public:
    virtual ~IToolRenderer() = default;

    // --- Tool invocation UI ---
    virtual Element renderToolUse(const ToolUseBlock& tool, const RenderContext& ctx) = 0;
    virtual std::string renderToolUseAnsi(const ToolUseBlock& tool) = 0;

    // --- Success result UI ---
    virtual Element renderToolResult(const ToolResultBlock& result,
                                     const ToolUseBlock& tool,
                                     const RenderContext& ctx) = 0;
    virtual std::string renderToolResultAnsi(const ToolResultBlock& result,
                                             const ToolUseBlock& tool) = 0;

    // --- Error result UI ---
    virtual Element renderToolError(const ToolResultBlock& result,
                                    const ToolUseBlock& tool,
                                    const RenderContext& ctx) = 0;
    virtual std::string renderToolErrorAnsi(const ToolResultBlock& result,
                                            const ToolUseBlock& tool) = 0;

    // --- Rejected/canceled UI ---
    virtual Element renderToolRejected(const ToolUseBlock& tool, const RenderContext& ctx) = 0;
    virtual std::string renderToolRejectedAnsi(const ToolUseBlock& tool) = 0;
    virtual Element renderToolCanceled(const ToolUseBlock& tool, const RenderContext& ctx) = 0;
    virtual std::string renderToolCanceledAnsi(const ToolUseBlock& tool) = 0;

    // --- Progress/queued ---
    virtual Element renderToolProgress(const ToolUseBlock& tool,
                                       const std::string& progress,
                                       const RenderContext& ctx) = 0;
    virtual Element renderToolQueued(const ToolUseBlock& tool, const RenderContext& ctx) = 0;

    // --- Grouped (parallel same-type calls) ---
    virtual Element renderGroupedToolUse(const std::vector<ToolUseBlock>& tools,
                                         const RenderContext& ctx) = 0;

    // --- Compact summary ---
    virtual std::string getToolUseSummary(const ToolUseBlock& tool) = 0;

    // --- User-facing name ---
    virtual std::string userFacingName(const ToolUseBlock& tool) = 0;

    // --- Classification ---
    virtual bool isCollapsible() const = 0;
    virtual bool isResultTruncatable(const ToolResultBlock& result) const = 0;
};
```

### 2.3 RenderContext

```cpp
struct RenderContext {
    bool verbose = false;
    bool isStreaming = false;
    int maxWidth = 80;
    int tickCounter = 0;
    const ThemeColors& theme;
};
```

### 2.4 ToolRendererRegistry

```cpp
// include/claude/ui/ToolRendererRegistry.hpp

class ToolRendererRegistry {
public:
    static ToolRendererRegistry& instance();

    void registerRenderer(const std::string& toolName,
                          std::unique_ptr<IToolRenderer> renderer);
    IToolRenderer* getRenderer(const std::string& toolName) const;
    IToolRenderer* getFallbackRenderer() const;

private:
    std::unordered_map<std::string, std::unique_ptr<IToolRenderer>> renderers_;
    std::unique_ptr<IToolRenderer> fallback_;  // DefaultToolRenderer
};
```

### 2.5 Built-in renderers

| Renderer | Custom rendering |
|----------|-----------------|
| `ReadToolRenderer` | File path + line range; result shows content summary |
| `BashToolRenderer` | Command text (truncated); result shows stdout/stderr |
| `EditToolRenderer` | Diff preview (DiffRenderer); result shows changed lines |
| `WriteToolRenderer` | Target file path; result shows bytes written |
| `GrepToolRenderer` | Pattern + path; result shows match count |
| `GlobToolRenderer` | Glob pattern; result shows file count |
| `AgentToolRenderer` | Agent type + sub-agent progress + token count |
| `WebFetchToolRenderer` | URL; result shows fetch status |
| `WebSearchToolRenderer` | Query; result shows result count |
| `LspToolRenderer` | Operation + symbol + location |
| `DefaultToolRenderer` | Unified `[Badge] Activity` format (current behavior) |

### 2.6 Relationship to 2026-05-22 spec Tool virtual methods

The 2026-05-22 spec added `renderToolUse()` / `renderToolResult()` virtual methods on the `Tool` base class. IToolRenderer is a separate abstraction that decouples rendering from the Tool class itself. Migration path:

1. Implement IToolRenderer for each tool, initially delegating to the Tool virtual methods
2. Gradually move rendering logic from Tool to IToolRenderer
3. Tool virtual methods become thin wrappers calling the registry

This avoids a big-bang rewrite while achieving the desired decoupling.

### 2.7 Files

**Created:**
- `include/claude/ui/IToolRenderer.hpp`
- `include/claude/ui/ToolRendererRegistry.hpp`
- `src/ui/renderers/ReadToolRenderer.cpp`
- `src/ui/renderers/BashToolRenderer.cpp`
- `src/ui/renderers/EditToolRenderer.cpp`
- `src/ui/renderers/WriteToolRenderer.cpp`
- `src/ui/renderers/GrepToolRenderer.cpp`
- `src/ui/renderers/GlobToolRenderer.cpp`
- `src/ui/renderers/AgentToolRenderer.cpp`
- `src/ui/renderers/WebFetchToolRenderer.cpp`
- `src/ui/renderers/WebSearchToolRenderer.cpp`
- `src/ui/renderers/LspToolRenderer.cpp`
- `src/ui/renderers/DefaultToolRenderer.cpp`

**Modified:**
- `src/bootstrap/AgentRunner.cpp` — register all built-in renderers at startup

---

## 3. Content Type System Expansion

### 3.1 Problem

C++ DisplayMessage has 14 variants. TS has 30+ content types including redacted_thinking, image attachments, XML-tag-dispatched user messages (12 subtypes), server_tool_use, and 4 distinct tool result states. This gap means C++ cannot render these content types at all.

### 3.2 New DisplayMessage types

```cpp
enum class DisplayMessageType : uint8_t {
    // --- Existing (unchanged) ---
    UserPrompt,
    AssistantText,
    AssistantThinking,
    AssistantToolUse,
    UserToolResult,         // Kept for backward compat during migration
    SystemInfo,
    SystemError,
    TurnDuration,
    PermissionPrompt,
    CompactBoundary,
    HookSummary,
    CollapsedReadSearch,
    GroupedToolUse,
    AgentProgress,

    // --- New: Thinking ---
    AssistantRedactedThinking,

    // --- New: Server tool use ---
    AssistantServerToolUse,

    // --- New: Tool result subtypes (replace UserToolResult) ---
    UserToolSuccess,
    UserToolError,
    UserToolRejected,
    UserToolCanceled,

    // --- New: Image attachment ---
    UserImageAttachment,

    // --- New: XML-tag dispatched user messages ---
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
};
```

### 3.3 XML tag dispatcher

```cpp
// include/claude/ui/XmlTagDispatcher.hpp

class XmlTagDispatcher {
public:
    struct ParsedTag {
        std::string tagName;
        std::string content;
        std::map<std::string, std::string> attrs;
    };

    static DisplayMessageType dispatch(const std::string& text);
    static std::optional<ParsedTag> parseFirstTag(const std::string& text);

private:
    static const std::unordered_map<std::string, DisplayMessageType> tagMap_;
};
```

Tag mapping:

| XML Tag | DisplayMessageType |
|---------|-------------------|
| `<bash-stdout>` / `<bash-stderr>` | `UserBashOutput` |
| `<bash-input>` | `UserBashInput` |
| `<command-message>` | `UserCommandMessage` |
| `<local-command-stdout>` | `UserLocalCommandOutput` |
| `<teammate-message>` | `UserTeammateMessage` |
| `<task-notification>` | `UserTaskNotification` |
| `<mcp-resource-update>` | `UserMcpResourceUpdate` |
| `<github-webhook-activity>` | `UserGitHubWebhook` |
| `<fork-boilerplate>` | `UserForkBoilerplate` |
| `<cross-session-message>` | `UserCrossSessionMessage` |
| `<channel source="...">` | `UserChannelMessage` |
| `<user-memory-input>` | `UserMemoryInput` |

### 3.4 NormalizeStage enhancement

In MessagePipeline::NormalizeStage, user text messages go through XML dispatch:

```
StreamEvent::textDelta (role=user)
  → XmlTagDispatcher::parseFirstTag(text)
    → if tag found: create corresponding DisplayMessageType
    → if no tag: create UserPrompt
```

### 3.5 Image attachment data

```cpp
struct ImageAttachmentData {
    std::string mediaType;   // "image/png", "image/jpeg"
    std::string data;        // base64 encoded
    std::string sourceUrl;   // optional source URL
};
```

Rendering: both FTXUI and Console paths show a placeholder `[image: image/png, N bytes]`. Future expansion can add Sixel/Kitty protocol inline images.

### 3.6 Tool result subtype migration

Phase 1: Add the 4 new types alongside existing `UserToolResult`. NormalizeStage creates the new types based on result status.
Phase 2: Update all rendering code to handle the new types.
Phase 3: Remove `UserToolResult` and its `resultStatus` enum.

### 3.7 Priority

- **P0 (must have):** UserToolSuccess/Error/Rejected/Canceled, AssistantRedactedThinking
- **P1 (should have):** UserBashOutput, UserCommandMessage, UserTaskNotification
- **P2 (nice to have):** UserImageAttachment, UserTeammateMessage, remaining XML tags

### 3.8 Files

**Modified:**
- `include/claude/ui/UiMessageTypes.hpp` — add new DisplayMessageType variants and data structs
- `src/ui/UiMessageTypes.cpp` — add height estimation for new types
- `src/ui/MessagePipeline.cpp` — enhance NormalizeStage with XML dispatch + new type creation

**Created:**
- `include/claude/ui/XmlTagDispatcher.hpp`
- `src/ui/XmlTagDispatcher.cpp`

---

## 4. Performance: OffscreenFreeze + Fast Path + LRU Cache

### 4.1 OffscreenFreeze

**Problem:** FTXUI renders all visible messages' full Element trees every frame. With many messages, CPU usage is high. The 50-messages-per-frame hard cap is a brute-force workaround.

**Design:**

```cpp
// include/claude/ui/OffscreenFreeze.hpp

class OffscreenFreeze : public ComponentBase {
public:
    OffscreenFreeze(Component child, int messageIndex);

    Element OnRender() override {
        if (!isVisibleInViewport()) {
            // Return empty Element with fixed height for layout space
            return ftxui::vbox({ftxui::filler() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, cachedHeight_)});
        }
        auto el = child_->Render();
        // Cache rendered height for future offscreen placeholder sizing.
        // FTXUI Elements are DOM-like — height is measured after rendering.
        // Use the message's pre-estimated height from VirtualScroll.
        cachedHeight_ = std::max(cachedHeight_, msg_.estimatedHeight);
        return el;
    }

private:
    bool isVisibleInViewport() const;
    int cachedHeight_ = 1;
    int messageIndex_;
};
```

- VirtualScroll maintains `firstVisibleIndex_` and `lastVisibleIndex_`
- ContentArea renders viewport ± buffer (3 messages above/below) normally
- Messages outside viewport return `placeholder(cachedHeight)` — no Element tree built
- Once a message has been rendered, its height is cached for future placeholder sizing
- **Ratchet pattern**: height only increases (never shrinks from cached value) to prevent layout jumps
- After OffscreenFreeze is in place, remove the 50-messages/frame hard cap from ContentArea

### 4.2 Plain-text fast path

**Problem:** All text goes through the Markdown parser even when it contains no Markdown syntax.

**Design:**

```cpp
// Add to FtxuiMarkdown

static bool hasMarkdownSyntax(const std::string& text) {
    static const std::regex mdSyntax(
        R"(([#*`~>\[\]|!-]|\d+\.\s|\*\*|~~|^\s*[-*]\s))",
        std::regex::optimize
    );
    return std::regex_search(text, mdSyntax);
}

Element FtxuiMarkdown::render(const std::string& text, const RenderContext& ctx) {
    if (!hasMarkdownSyntax(text)) {
        return paragraph(text);  // Skip parser entirely
    }
    return renderMarkdown(text, ctx);
}
```

Estimated impact: 30-40% of render calls (SystemInfo, TurnDuration, tool descriptions) skip the parser.

### 4.3 Markdown LRU cache

**Problem:** Window resize or scroll remount causes full re-parse of already-rendered Markdown.

**Design:**

```cpp
class MarkdownCache {
public:
    using CacheKey = uint64_t;  // XXH3 hash of content

    std::optional<std::vector<ParsedBlock>> get(CacheKey key);
    void put(CacheKey key, std::vector<ParsedBlock> blocks);

private:
    static constexpr size_t maxSize_ = 200;
    std::unordered_map<CacheKey,
        std::pair<std::vector<ParsedBlock>, std::list<CacheKey>::iterator>>
        cache_;
    std::list<CacheKey> lruList_;
};
```

Note: StreamingMarkdown's `completedElements_` already handles the streaming case. This LRU serves static messages re-entering the viewport after being frozen.

### 4.4 Files

**Created:**
- `include/claude/ui/OffscreenFreeze.hpp`
- `src/ui/OffscreenFreeze.cpp`

**Modified:**
- `src/ui/FtxuiMarkdown.cpp` — add hasMarkdownSyntax() fast path + MarkdownCache
- `src/ui/VirtualScroll.cpp` — expose firstVisibleIndex/lastVisibleIndex for OffscreenFreeze
- `src/ui/FtxuiRender.cpp` (or new ContentArea.cpp) — wrap messages in OffscreenFreeze, remove 50/frame cap

---

## 5. Color Accessibility + ANSI 16-Color Compatibility

### 5.1 Daltonized themes

Add two built-in themes that replace red/green semantic colors with blue/orange (distinguishable by red-green color-blind users):

```cpp
// In ThemeSystem.cpp

static const ThemeColors DARK_DALTONIZED = {
    .success       = RGB(80, 180, 210),   // cyan instead of green
    .error         = RGB(220, 140, 80),   // orange instead of red
    .diff_add      = RGB(80, 180, 210),   // cyan
    .diff_remove   = RGB(220, 140, 80),   // orange
    .context_ok    = RGB(80, 180, 210),
    .context_crit  = RGB(220, 140, 80),
    // remaining colors inherit from DARK theme
};

static const ThemeColors LIGHT_DALTONIZED = {
    // Same red/green → blue/orange substitution on LIGHT base
};
```

### 5.2 ANSI 16-color themes

```cpp
static const ThemeColors DARK_ANSI = {
    .primary       = ANSI_BLUE,
    .success       = ANSI_CYAN,
    .error         = ANSI_RED,
    .warning       = ANSI_YELLOW,
    .diff_add      = ANSI_CYAN,
    .diff_remove   = ANSI_RED,
    .diff_chunk    = ANSI_YELLOW,
    .prompt        = ANSI_GREEN,
    .assistant     = ANSI_WHITE,
    .thinking      = ANSI_MAGENTA,
    // All colors mapped to standard ANSI 0-15
};

static const ThemeColors LIGHT_ANSI = { /* same approach on light base */ };
```

### 5.3 Terminal capability detection

```cpp
// include/claude/console/TerminalCapabilities.hpp

class TerminalCapabilities {
public:
    static int detectColorLevel();  // 0=mono, 1=16, 2=256, 3=truecolor

    static bool supportsTrueColor()   { return detectColorLevel() >= 3; }
    static bool supports256Color()    { return detectColorLevel() >= 2; }
};
```

Detection logic:
- `COLORTERM=24bit` or `COLORTERM=truecolor` → level 3
- `TERM=*256color*` → level 2
- `TERM=*xterm*` → level 1
- Default → level 1 (assume at least 16-color)

### 5.4 Auto-downgrade

At startup:
1. Detect color level
2. If level < 3 and user has not manually selected a theme → auto-select ANSI theme
3. If user selected a truecolor theme but terminal doesn't support it → warn once and downgrade
4. RGB colors in ThemeColors are rendered with ANSI approximation based on current level

### 5.5 Color cascade (explicit pass-through)

Since FTXUI lacks React's context mechanism, use explicit color passing:

- MessageComponent constructor receives `const ThemeColors& theme`
- Each sub-component receives theme reference from parent
- This achieves the same effect as TS's `TextHoverColorContext` — uncolored children inherit parent color

### 5.6 Files

**Created:**
- `include/claude/console/TerminalCapabilities.hpp`

**Modified:**
- `src/console/ThemeSystem.cpp` — add DARK_DALTONIZED, LIGHT_DALTONIZED, DARK_ANSI, LIGHT_ANSI themes
- `src/ui/FtxuiRender.cpp` (or new AppLayout.cpp) — auto-detect and apply theme downgrade
- All new MessageComponent subclasses — accept and forward ThemeColors reference

---

## 6. Message Search

### 6.1 SearchOverlay component

```cpp
// include/claude/ui/SearchOverlay.hpp

class SearchOverlay : public ComponentBase {
public:
    SearchOverlay();

    Element OnRender() override;
    bool OnEvent(Event event) override;

    void activate();                    // Ctrl+F or /
    void deactivate();                  // Esc
    bool isActive() const;

    struct SearchResult {
        int messageIndex;
        int matchOffset;
        std::string contextLine;
    };

    const std::vector<SearchResult>& results() const;
    int currentMatchIndex() const;
    void nextMatch();                   // Enter / ↓
    void prevMatch();                   // Shift+Enter / ↑

    void setMessages(const std::vector<DisplayMessage>* messages);

private:
    void performSearch(const std::string& query);
    std::string searchQuery_;
    std::vector<SearchResult> results_;
    int currentMatch_ = -1;
    bool active_ = false;
    const std::vector<DisplayMessage>* messages_ = nullptr;
};
```

### 6.2 UI layout

Search bar rendered as overlay at the top of ContentArea:

```
╭─ Search: "TODO" ──── 3/12 matches ──── Enter↓ Shift+Enter↑ Esc╮
│                                                                 │
│  ... message content with highlighted matches ...               │
```

### 6.3 Integration

- ContentArea owns SearchOverlay
- Ctrl+F or `/` activates search (handled by AppLayout event dispatch)
- When search is active, MessageList rendering adds highlight background to matched text spans
- Current match scrolls to viewport center via VirtualScroll
- Esc closes search, removes all highlights

### 6.4 Searchable text extraction

Each DisplayMessage provides `searchableText()` method returning plain-text content for search indexing.

### 6.5 Files

**Created:**
- `include/claude/ui/SearchOverlay.hpp`
- `src/ui/SearchOverlay.cpp`

**Modified:**
- `src/ui/UiMessageTypes.cpp` — add `searchableText()` implementation per message type
- New `ContentArea.cpp` — integrate SearchOverlay

---

## 7. Permission Prompt Enhancement

### 7.1 IPermissionRenderer interface

```cpp
// include/claude/ui/IPermissionRenderer.hpp

class IPermissionRenderer {
public:
    virtual ~IPermissionRenderer() = default;

    virtual Element renderPrompt(const PermissionRequest& req,
                                 const RenderContext& ctx) = 0;
    virtual std::string getActivityDescription(const PermissionRequest& req) = 0;
    virtual std::vector<Element> renderDetailLines(const PermissionRequest& req,
                                                    const RenderContext& ctx) {
        return {};  // default: no detail lines
    }
};
```

### 7.2 Built-in permission renderers

| Renderer | Custom detail lines |
|----------|-------------------|
| `BashPermissionRenderer` | Command content + safety warnings (e.g. `rm`, `sudo`) |
| `FileEditPermissionRenderer` | File path + diff preview |
| `FileWritePermissionRenderer` | Target file path + size |
| `FileReadPermissionRenderer` | File path |
| `DefaultPermissionRenderer` | Unified format (current behavior) |

### 7.3 Feedback text input

```cpp
struct PermissionOption {
    PermissionDecision decision;
    std::string label;             // "Yes (once)"
    char shortcutKey;              // 'y'
    bool hasFeedbackInput;         // whether this option has a feedback text input
    std::string feedbackPlaceholder;  // "Tab to expand..."
};
```

When user selects an option with `hasFeedbackInput = true`, pressing Tab expands an inline text input. Enter submits feedback + confirms decision. This matches TS's permission feedback mechanism.

### 7.4 PermissionRendererRegistry

Mirrors ToolRendererRegistry pattern:

```cpp
class PermissionRendererRegistry {
public:
    static PermissionRendererRegistry& instance();
    void registerRenderer(const std::string& toolName,
                          std::unique_ptr<IPermissionRenderer> renderer);
    IPermissionRenderer* getRenderer(const std::string& toolName) const;
    IPermissionRenderer* getFallbackRenderer() const;
};
```

### 7.5 Files

**Created:**
- `include/claude/ui/IPermissionRenderer.hpp`
- `include/claude/ui/PermissionRendererRegistry.hpp`
- `src/ui/permissions/BashPermissionRenderer.cpp`
- `src/ui/permissions/FileEditPermissionRenderer.cpp`
- `src/ui/permissions/FileWritePermissionRenderer.cpp`
- `src/ui/permissions/FileReadPermissionRenderer.cpp`
- `src/ui/permissions/DefaultPermissionRenderer.cpp`

**Modified:**
- `src/ui/FtxuiPermission.cpp` — delegate to IPermissionRenderer instead of inline rendering
- `src/ui/components/PermissionPrompt.cpp` — add feedback input support
- `include/claude/ui/components/PermissionPrompt.hpp` — add PermissionOption with feedback fields

---

## 8. Implementation Order

The design is decomposed into dependency-ordered phases:

### Phase 1: Component Tree + Tool Renderer Interface (foundation)
1. Create MessageComponent base class + MessageList factory
2. Extract rendering from MainComponent into individual MessageComponent subclasses
3. Create IToolRenderer interface + ToolRendererRegistry + DefaultToolRenderer
4. Wire ToolUseComponent/ToolResultComponent to delegate to IToolRenderer
5. Delete MainComponent after all rendering is delegated

### Phase 2: Content Type Expansion (data model)
6. Add new DisplayMessageType variants to UiMessageTypes.hpp
7. Implement XmlTagDispatcher + integrate into NormalizeStage
8. Add UserToolSuccess/Error/Rejected/Canceled (P0)
9. Add AssistantRedactedThinking (P0)
10. Add UserBashOutput, UserCommandMessage, UserTaskNotification (P1)
11. Add remaining XML tag types + UserImageAttachment (P2)

### Phase 3: Performance Optimization
12. Implement OffscreenFreeze + integrate into ContentArea
13. Remove 50-messages/frame hard cap
14. Add hasMarkdownSyntax() fast path to FtxuiMarkdown
15. Add MarkdownCache LRU for static messages

### Phase 4: Per-Tool Custom Renderers
16. Implement ReadToolRenderer, BashToolRenderer, EditToolRenderer
17. Implement WriteToolRenderer, GrepToolRenderer, GlobToolRenderer
18. Implement AgentToolRenderer, WebFetchToolRenderer, WebSearchToolRenderer, LspToolRenderer
19. Register all renderers at startup

### Phase 5: Accessibility + ANSI Compatibility
20. Add DARK_DALTONIZED + LIGHT_DALTONIZED themes
21. Add DARK_ANSI + LIGHT_ANSI themes
22. Implement TerminalCapabilities detection
23. Add auto-downgrade at startup

### Phase 6: Search + Permission Enhancement
24. Implement SearchOverlay component
25. Integrate search into ContentArea with Ctrl+F
26. Add searchableText() to DisplayMessage types
27. Implement IPermissionRenderer + built-in renderers
28. Add feedback text input to permission prompt

---

## 9. Non-Goals

- **Markdown parser replacement** — the self-written parser stays for now; a future spec may evaluate switching to a C Markdown library (e.g. cmark-gfm)
- **Sixel/Kitty inline image rendering** — placeholder only for UserImageAttachment
- **Multi-line input (Shift+Enter)** — significant input system rewrite, separate spec
- **React Compiler / automatic memoization** — C++ has no equivalent; OffscreenFreeze addresses the same problem
- **Brand color shimmer animation** — low priority visual polish, separate spec
