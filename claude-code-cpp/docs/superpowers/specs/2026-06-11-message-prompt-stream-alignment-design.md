# Message Format, System Prompt & Streaming Pipeline Alignment Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Align claude-code-cpp's message format, system prompt, and streaming pipeline with the TypeScript original to fix the root causes of poor output quality.

**Architecture:** Three-phase bottom-up rewrite with interleaved verification: (1) Refactor Message from flat-string+arrays to ContentBlock[] model matching the Anthropic API's content block structure, (1.5) Quick streaming fixes (signature, stop reasons) that don't depend on Phase 2/3, (2) Align system prompt to TS's section-based architecture with proper dynamic boundary and context injection into user messages, (3) Unify the streaming pipeline by eliminating duplicate SSE parsing and implementing progressive tool result yielding. Each phase produces a buildable, testable system.

**Tech Stack:** C++17, Anthropic Messages API, ftxui, nlohmann/json

---

## Background & Root Cause Analysis

The C++ implementation has three core subsystems that diverge from the TypeScript original:

1. **Message format**: Uses `flat string content + parallel arrays (toolCalls, toolResults, thinking)` instead of the API's native `ContentBlock[]` model. This loses block ordering, breaks cache control placement, causes `is_error` to not propagate, and sends thinking blocks that should be stripped.

2. **System prompt**: Embeds git status and CLAUDE.md directly in the system prompt instead of user-message attachments. Missing dynamic sections (memory, scratchpad, frc). Section text diverges from TS. Cache control markers are incorrectly placed.

3. **Streaming pipeline**: Two independent SSE parsers process the same events. Interleaved tool execution enqueues but doesn't yield results progressively. Signature deltas are appended instead of overwritten. Missing stop reasons (`model_context_window_exceeded`, `refusal`).

The message format is the root cause — most bugs in the other subsystems stem from the inability to represent interleaved content blocks.

---

## Phase 1: Message Format — ContentBlock[] Model

### 1.1 New ContentBlockParam Type

Replace the flat-string Message with a ContentBlockParam-based model matching the Anthropic API:

```cpp
// In include/claude/core/Types.hpp

struct CacheControl {
    enum Scope { None, Global, Org };
    Scope scope = None;
};

struct ContentBlockParam {
    enum Type { Text, ToolUse, ToolResult, Thinking, RedactedThinking };
    Type type;

    // Text block
    String text;

    // ToolUse block
    String id;          // tool_use_id (e.g., "toolu_xxx")
    String name;        // tool name
    Json input;         // parsed JSON object (not string)

    // ToolResult block
    String toolUseId;   // matches ToolUse.id
    String resultContent; // string content (extend to variant later for images)
    bool isError = false;

    // Thinking block
    String thinking;
    String signature;

    // RedactedThinking block
    String redactedData;

    // Cache control (optional, for system prompt blocks)
    std::optional<CacheControl> cacheControl;

    // Convenience constructors
    static ContentBlockParam text(String t) {
        return {.type = Text, .text = std::move(t)};
    }
    static ContentBlockParam toolUse(String id, String name, Json input) {
        return {.type = ToolUse, .id = std::move(id), .name = std::move(name),
                .input = std::move(input)};
    }
    static ContentBlockParam toolResult(String toolUseId, String content, bool err = false) {
        return {.type = ToolResult, .toolUseId = std::move(toolUseId),
                .resultContent = std::move(content), .isError = err};
    }
    static ContentBlockParam thinking(String t, String sig) {
        return {.type = Thinking, .thinking = std::move(t), .signature = std::move(sig)};
    }
    static ContentBlockParam redactedThinking(String data) {
        return {.type = RedactedThinking, .redactedData = std::move(data)};
    }
};
```

### 1.2 New Message Type

```cpp
struct Message {
    MessageRole role;
    std::vector<ContentBlockParam> content;

    // Convenience constructors
    static Message user(String text) {
        Message m; m.role = MessageRole::User;
        m.content.push_back(ContentBlockParam::text(std::move(text)));
        return m;
    }
    static Message userBlocks(std::vector<ContentBlockParam> blocks) {
        Message m; m.role = MessageRole::User;
        m.content = std::move(blocks);
        return m;
    }
    static Message assistant(String text) {
        Message m; m.role = MessageRole::Assistant;
        m.content.push_back(ContentBlockParam::text(std::move(text)));
        return m;
    }
    static Message assistantBlocks(std::vector<ContentBlockParam> blocks) {
        Message m; m.role = MessageRole::Assistant;
        m.content = std::move(blocks);
        return m;
    }
    static Message system(String text) {
        Message m; m.role = MessageRole::System;
        m.content.push_back(ContentBlockParam::text(std::move(text)));
        return m;
    }

    // Utility: extract all text from content blocks
    String textContent() const;

    // Utility: extract all tool_use blocks
    std::vector<ContentBlockParam> toolUseBlocks() const;

    // Utility: does this message have any non-empty content?
    bool hasContent() const;
};
```

### 1.3 Key Methods That Become Trivial

**stripThinking()** — filter content blocks:
```cpp
void stripThinkingFromHistory() {
    for (auto& msg : history) {
        if (msg.role == MessageRole::Assistant) {
            msg.content.erase(
                std::remove_if(msg.content.begin(), msg.content.end(),
                    [](const ContentBlockParam& b) {
                        return b.type == ContentBlockParam::Thinking;
                    }),
                msg.content.end());
        }
    }
}
```
Note: RedactedThinking is preserved (API requires it), only Thinking is stripped.

**buildApiRequest()** — direct serialization, no format conversion:
```cpp
// For Anthropic: content blocks serialize directly
for (const auto& block : msg.content) {
    switch (block.type) {
        case ContentBlockParam::Text:
            j = {{"type", "text"}, {"text", block.text}};
            break;
        case ContentBlockParam::ToolUse:
            j = {{"type", "tool_use"}, {"id", block.id},
                 {"name", block.name}, {"input", block.input}};
            break;
        case ContentBlockParam::ToolResult:
            j = {{"type", "tool_result"}, {"tool_use_id", block.toolUseId},
                 {"content", block.resultContent}};
            if (block.isError) j["is_error"] = true;
            break;
        case ContentBlockParam::Thinking:
            j = {{"type", "thinking"}, {"thinking", block.thinking},
                 {"signature", block.signature}};
            break;
        case ContentBlockParam::RedactedThinking:
            j = {{"type", "redacted_thinking"}, {"data", block.redactedData}};
            break;
    }
    if (block.cacheControl) {
        // Apply cache_control marker
    }
}
```

**Consecutive tool results merge** — Anthropic requires multiple tool_results in one user message:
```cpp
// Already handled naturally: when building API messages,
// accumulate consecutive ToolResult blocks into a single user message.
```

### 1.4 Bugs Fixed Automatically

| Bug | Before | After |
|-----|--------|-------|
| `is_error` not propagated | `buildApiRequest()` didn't serialize `isError` from ToolResponse | `ContentBlockParam::ToolResult` includes `isError` inline |
| Thinking re-sent on every API call | `buildApiRequest()` always included thinking/signature | `stripThinking()` filters `type == Thinking` blocks; `RedactedThinking` preserved |
| Block ordering lost | Text in `content`, tools in `toolCalls[]` — order impossible | All blocks in `content[]` in API order |
| Cache markers on wrong message | Index calculation broke after format conversion | `cacheControl` on individual `ContentBlockParam` |
| Content block reconstruction wrong | Text→thinking→tool_use fixed order | Original API ordering preserved |

### 1.5 Files Modified

| File | Change |
|------|--------|
| `include/claude/core/Types.hpp` | Add `ContentBlockParam`, rewrite `Message` |
| `src/api/AnthropicClient.cpp` | Rewrite `buildRequest()` to serialize ContentBlockParam directly |
| `src/api/OpenAIClient.cpp` | Rewrite `buildRequest()` to convert ContentBlockParam to OpenAI format |
| `src/core/AgentLoop.cpp` | Rewrite `buildApiRequest()`, `executeToolCalls()`, `stripThinkingFromHistory()` |
| `src/core/AgentLoopStreaming.cpp` | Rewrite stream callbacks to build ContentBlockParam |
| `src/core/AgentLoopTools.cpp` | `executeTool()` returns ContentBlockParam::ToolResult |
| `src/compact/MicroCompact.cpp` | Operate on ContentBlock[] instead of flat content + toolResults |
| `src/compact/AutoCompact.cpp` | Serialize ContentBlock[] for summarization prompt |
| `src/compact/PostCompactCleanup.cpp` | Enforce alternation on ContentBlock-based messages |
| `src/bootstrap/AgentRunner.cpp` | Update session resume to use ContentBlockParam format |
| `src/context/ContextInjector.cpp` | Inject context as ContentBlockParam in user messages |
| `tests/*.cpp` | Update all tests referencing old Message fields |

### 1.6 Migration Strategy

Since this is a fundamental data structure rewrite affecting 50+ files, use a phased migration:

1. Define new `ContentBlockParam` and new `Message` type in `Types.hpp` alongside the old types. Mark old `Message` fields as `[[deprecated]]`.
2. Migrate consumers one at a time: `AnthropicClient::buildRequest()` first (proves serialization works), then `AgentLoop::buildApiRequest()`, then streaming, then compaction.
3. After all consumers use the new type, delete old fields.
4. **Session persistence**: Add version detection in `resumeLastSession()` — old-format session files (with flat `content` string + `tool_calls`/`tool_results` arrays) must be converted to the new ContentBlockParam format on load. Add `migrateLegacySession(Json&)` function.

### 1.7 ToolResult Truncation Flag

Add a `truncated` flag to `ContentBlockParam::ToolResult` so the model knows when a result was cut short:

```cpp
struct ContentBlockParam {
    // ... existing fields ...

    // ToolResult
    String toolUseId;
    String resultContent;
    bool isError = false;
    bool truncated = false;   // true if result was truncated by ResultTruncation
};
```

When `ResultTruncation::truncate()` shortens a result, set `truncated = true`. The Anthropic API doesn't have a native field for this, so append `[truncated]` to the result content string when serializing if the flag is set.

### 1.8 CacheControl Serialization Rules

- Only emit `cache_control` on ContentBlockParam when `role == MessageRole::System`.
- For user/assistant messages, the `cacheControl` field is ignored during serialization.
- Exception: a cache breakpoint on the second-to-last user message (for conversation prefix caching) is applied at the message level during `buildApiRequest()`, not on individual blocks.

### 1.9 ToolResult Merging in API Request

When building the API request, consecutive messages containing `ContentBlockParam::ToolResult` blocks must be merged into a single `role: "user"` message. This matches the Anthropic API requirement that tool_results appear in user messages:

```cpp
// In buildApiRequest(): when iterating messages, if the current message
// starts with ToolResult blocks and the previous API message was also a
// user message with tool_results, merge the blocks into that message.
```

---

## Phase 1.5: Quick Streaming Fixes (No Dependencies)

These small fixes can be applied immediately after Phase 1, independent of Phase 2/3.

### 1.5a Signature Delta Fix

**Bug:** `AgentLoopStreaming.cpp` appends signature deltas: `signature += delta`
**Correct:** Anthropic API sends the final complete signature each time, not incremental. Use overwrite: `signature = delta`

**File:** `src/core/AgentLoopStreaming.cpp` — one-line change.

### 1.5b Stop Reason Completion

**`model_context_window_exceeded`**: The Anthropic API returns this as a stream `error` event (type `overloaded_error`), not as a `stop_reason`. The SSE parser in `AnthropicClient` should detect this error type and throw a `PromptTooLongException` (or similar), which `AgentLoop` catches to trigger reactive compact. This matches the TS pattern where `413` / `overloaded_error` triggers compaction retry.

**`refusal`**: This is an OpenAI `finish_reason`, not an Anthropic `stop_reason`. Handle it only in the `OpenAIClient` code path — yield an error and terminate the loop.

### 1.5c Streaming Tool Executor Safety

Ensure thread-safety for the `onStreamToolEvent` callback when tools complete from thread-pool threads:
- The callback posts to the UI thread via `screen_->Post()` (already the case in FtxuiRepl)
- For ANSI mode, the callback writes to stdout directly — add a mutex if not already present
- Verify `collectResults()` still works for non-interleaved mode while interleaved results flow through callbacks

### 2.1 Section-Based Architecture

Replace the current `SystemPromptBuilder` monolithic build with a section-based model matching TS:

```cpp
// In include/claude/context/SystemPromptBuilder.hpp

struct SystemPromptSection {
    String id;           // "intro", "doing_tasks", "session_guidance", "env_info", etc.
    String content;
    bool isStatic;       // true = before dynamic boundary, false = after
};

class SystemPromptBuilder {
public:
    // Build sections (not a flat string)
    std::vector<SystemPromptSection> buildSections();

    // Build API blocks with cache_control
    std::vector<TextBlockParam> buildApiBlocks();

    // Dynamic boundary marker (matching TS exactly).
    // NOTE: This marker is used ONLY for internal section splitting.
    // It is NOT included in the API request TextBlockParam array.
    // The cache_control placement on the last static block achieves
    // the same effect without leaking the marker to the model.
    static constexpr const char* DYNAMIC_BOUNDARY = "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__";
};
```

**Important:** The dynamic boundary marker string is used internally to split sections into static vs. dynamic groups. When building `TextBlockParam` for the API, the marker is NOT included as a block. Instead, `cache_control` markers on the last static block (global scope) and the last dynamic block (org scope) achieve the caching boundary.

### 2.2 Static Sections (before boundary)

In TS order:
1. **SDK prefix** (standalone block, cache_control: null)
2. **Intro** (`getSimpleIntroSection`)
3. **System rules** (`getSimpleSystemSection` — includes system reminders bullets)
4. **Doing tasks** (`getSimpleDoingTasksSection`)
5. **Actions** (`getActionsSection`)
6. **Using your tools** (`getUsingYourToolsSection` — includes task tracking bullet)
7. **Tone and style** (`getSimpleToneAndStyleSection`)
8. **Output efficiency** (`getOutputEfficiencySection` — NOT the C++ hybrid version)

### 2.3 Dynamic Sections (after boundary)

In TS order:
1. **session_guidance** — add fork subagent, Explore agent threshold, skill invocation, verification agent
2. **memory** — NEW: auto-memory management instructions. Conditional on whether `~/.claude/projects/<project-hash>/memory/MEMORY.md` exists. Tells the model about the memory directory structure and how to save/update memories. Use `ClaudeMdLoader`'s existing `AutoMem` detection to determine insertion.
3. **env_info** — wrapped in `<env>` XML tags; add model ID, knowledge cutoff, model family, availability text, fast mode
4. **language** — match TS text: "Always respond in {language}. Use {language} for all explanations..."
5. **output_style** — conditional
6. **mcp_instructions** — MCP server tool descriptions
7. **scratchpad** — NEW: per-session temp directory instructions
8. **frc** — NEW: function result clearing notice
9. **summarize_tool_results** — match TS single-sentence version (remove C++ additions)
10. **system_reminders** — move from dynamic to static (in system rules section)
11. **proactive** — conditional

### 2.4 Context Injection into User Messages

Move git status, CLAUDE.md, skills, date from system prompt to user message content blocks:

```cpp
// In ContextInjector — build user message content blocks
std::vector<ContentBlockParam> buildUserContext(const String& userInput) {
    std::vector<ContentBlockParam> blocks;

    // User's actual input
    blocks.push_back(ContentBlockParam::text(userInput));

    // Git status as system-reminder
    if (gitStatus_) {
        String gitStr = formatGitStatus();
        blocks.push_back(ContentBlockParam::text(
            "<system-reminder>\n" + gitStr + "\n</system-reminder>"));
    }

    // CLAUDE.md as system-reminder
    if (!claudeMd_.empty()) {
        blocks.push_back(ContentBlockParam::text(
            "<system-reminder>\n" + claudeMd_ + "\n</system-reminder>"));
    }

    // Date
    blocks.push_back(ContentBlockParam::text(
        "<system-reminder>\nCurrent date: " + getCurrentDate() + "\n</system-reminder>"));

    // Skills discovery
    if (!skills_.empty()) {
        blocks.push_back(ContentBlockParam::text(
            "<system-reminder>\n" + formatSkills() + "\n</system-reminder>"));
    }

    // Memories
    for (auto& [name, content] : memories_) {
        blocks.push_back(ContentBlockParam::text(
            "<system-reminder>\n" + content + "\n</system-reminder>"));
    }

    return blocks;
}
```

### 2.5 Cache Control on System Prompt Blocks

Match TS's 4-block scheme:
1. SDK prefix block → `cache_control: null`
2. Static content → `cache_control: { type: "ephemeral", scope: "global" }` on last static block
3. Dynamic content → `cache_control: null` (no caching)
4. Last block overall → `cache_control: { type: "ephemeral", scope: "org" }` override

### 2.6 Text Content Corrections

| Section | Current C++ Text | Correct TS Text |
|---------|-----------------|-----------------|
| `system_reminders` | Custom paragraph about "System reminders appear in tool results..." | "- Tool results and user messages may include <system-reminder> tags. <system-reminder> tags contain useful information and reminders..." |
| `language` | "The user has configured the language to: {lang}. Respond in this language unless the user explicitly asks for another." | "Always respond in {language}. Use {language} for all explanations, comments, and communications with the user. Technical terms and code identifiers should remain in their original form." |
| `output_efficiency` | Hybrid of ant-only "Communicating with the user" + external "Output efficiency" + C++ additions | Match TS external version exactly; remove "End-of-turn summary" addition |
| `tone_and_style` | Includes "Match responses to the task..." (from output_efficiency) | Remove that text; match TS `getSimpleToneAndStyleSection()` |
| `summarize_tool_results` | Extended C++ version with 5 extra sentences | TS single sentence: "When working with tool results, write down any important information you might need later in your response, as the original tool result may be cleared later." |
| `env_info` | Flat bullets with `true/false` for booleans | XML `<env>` wrapper, `Yes/No` for booleans, add model ID/cutoff/family/availability |

### 2.7 Remove Unnecessary C++ Additions

- Remove RAG context from system prompt (`withRagContext()`, `withAutoRag()`)
- Remove "End-of-turn summary" instruction from output_efficiency
- Remove "Match responses to the task" from tone_and_style
- Remove extended tool result summarization text

### 2.8 Files Modified

| File | Change |
|------|--------|
| `include/claude/context/SystemPromptBuilder.hpp` | Refactor to section-based model |
| `src/context/SystemPromptBuilder.cpp` | Rewrite section building with correct order and text |
| `src/context/Prompts.cpp` | Fix section text, add missing sections (memory, scratchpad, frc) |
| `include/claude/context/ContextInjector.hpp` | Change to build ContentBlockParam user messages |
| `src/context/ContextInjector.cpp` | Rewrite injection to use system-reminder in user content blocks |
| `src/context/ClaudeMdLoader.cpp` | Stop embedding in system prompt |
| `src/core/AgentLoop.cpp` | Update `injectContext()` to use ContentBlock user messages |

---

## Phase 3: Streaming Pipeline Alignment

### 3.1 Eliminate Duplicate SSE Parsing

**Current:** `AnthropicClient::processSseEvent()` and `AgentLoopStreaming.cpp` both parse raw SSE JSON independently, with `dynamic_cast<AnthropicClient*>` gating.

**Target:** Single parsing path.

```
AnthropicClient SSE → processSseEvent() → TypedStreamEvent → onTypedEvent callback
                                                         ↓
                                              AgentLoop consumes TypedStreamEvent
                                              (no raw JSON parsing in AgentLoop)
```

**Implementation:**
- `AnthropicClient` remains the sole SSE parser, emitting `TypedStreamEvent` via callback
- `AgentLoopStreaming.cpp` removes all inline JSON parsing of SSE events
- `AgentLoopStreaming.cpp` only processes `TypedStreamEvent` objects
- `OpenAIClient` already emits `TypedStreamEvent` for most event types; fill in any gaps
- Remove `dynamic_cast<AnthropicClient*>` check from `AgentLoopStreaming.cpp`
- Add virtual `bool isSseParser() const` to `ApiClient` base class (default false, AnthropicClient returns true)

### 3.2 Progressive Tool Result Yielding

**Current:** Tools are enqueued at `content_block_stop`, results collected after stream ends.

**Target:** Each tool completion immediately produces a `StreamToolEvent::Completed` that flows through `StreamBuffer` → `DisplayEvent` → `ContentBlock`.

**Implementation:**
- In `AgentLoopTools.cpp`, each tool execution runs in a thread pool
- On completion, `onStreamToolEvent(StreamToolEvent::Completed{...})` is called immediately
- This callback is already wired to `StreamBuffer::feed()` in `AgentRunner.cpp`
- The `StreamBuffer` already flushes text buffer and emits `DisplayEvent::ToolResult`
- Remove `collectResults()` blocking wait; replace with `waitForRemaining()` that only waits for tools still running at stream end

### 3.3 Signature Delta Fix

**Bug:** C++ appends signature deltas: `signature += delta`
**Correct:** API sends final complete signature: `signature = delta`

**Fix:** One-line change in `AgentLoopStreaming.cpp` — change `+=` to `=` for signature accumulation.

### 3.4 Error Event and Stop Reason Handling

**Context window exceeded** is NOT a `stop_reason` — the Anthropic API returns it as a stream `error` event with `type: "overloaded_error"`. The SSE parser should detect this and throw a `PromptTooLongException`, which `AgentLoop` catches to trigger reactive compact. This matches the TS pattern where 413/overloaded_error triggers compaction retry.

**`refusal`** is an OpenAI `finish_reason`, not an Anthropic `stop_reason`. Handle it only in the `OpenAIClient` code path — yield an error message and terminate the loop.

```cpp
// In AnthropicClient::processSseEvent — error event handling
if (errorType == "overloaded_error") {
    throw PromptTooLongException(errorMsg);
}

// In AgentLoopStreaming — OpenAI refusal
if (stopReason == "refusal") {
    // Only reachable via OpenAI client
    return IterationResult{.stopReason = "refusal"};
}
```

### 3.5 Streaming Fallback Tool Executor Discard

When a streaming fallback occurs (model switch), generate synthetic error `ToolResponse` for all pending/running interleaved tools:

```cpp
void discardStreamingTools() {
    for (auto& pending : pendingToolCalls_) {
        StreamToolEvent err;
        err.type = StreamToolEventType::Error;
        err.toolCallId = pending.callId;
        err.toolName = pending.toolName;
        onStreamToolEvent_(std::move(err));
    }
    pendingToolCalls_.clear();
}
```

### 3.6 Files Modified

| File | Change |
|------|--------|
| `include/claude/core/AgentLoop.hpp` | Add `discardStreamingTools()`, `PromptTooLongException`, remove `isAnthropicClient` logic |
| `src/core/AgentLoopStreaming.cpp` | Remove duplicate SSE parsing, consume only TypedStreamEvent; fix signature `+=` → `=`; handle OpenAI `refusal` |
| `src/core/AgentLoopTools.cpp` | Tool completion calls `onStreamToolEvent` immediately; thread-safety verification |
| `src/api/AnthropicClient.cpp` | Detect `overloaded_error` and throw `PromptTooLongException`; verify TypedStreamEvent output completeness |
| `src/api/OpenAIClient.cpp` | Handle `refusal` finish_reason; fill gaps in TypedStreamEvent output |
| `include/claude/api/ApiClient.hpp` | Add `virtual bool isNativeSseParser() const` (default false, AnthropicClient returns true) |

---

## Verification Strategy

### Per-Phase Build & Test Gate
After each phase, the system MUST build and pass all tests before proceeding. Additionally, run a minimal smoke test: single-turn conversation with "Say hello" and verify the API request format.

### Phase 1 Verification (Message Format)
- Build succeeds with new `Message` type
- All unit tests pass after migration
- API request JSON matches TS format (compare with `claude --debug` output from TS version)
- `is_error` field present in tool_result blocks sent to API
- Thinking blocks stripped before API call; redacted_thinking preserved
- Content block order matches API response order
- Session resume correctly migrates old-format session files

### Phase 1.5 Verification (Quick Streaming Fixes)
- Signature is overwritten, not appended (unit test with two signature_delta events)
- `overloaded_error` SSE event triggers reactive compact (not a crash)
- OpenAI `refusal` finish_reason yields error message

### Phase 2 Verification (System Prompt)
- System prompt section count and order matches TS
- No git status or CLAUDE.md content in system prompt
- Git status appears in user message as `<system-reminder>` tag
- Cache control markers correctly placed on system prompt blocks
- Memory prompt section present (when MEMORY.md exists)
- Text content of each section matches TS
- Dynamic boundary marker NOT included in API request

### Phase 3 Verification (Streaming)
- No duplicate SSE parsing — only `AnthropicClient` parses SSE
- Tool results appear in UI during streaming (not just after stream end)
- Fallback discards pending tool results
- Thread-safety: concurrent tool completions don't corrupt output

### Cross-Phase Integration Test
- Run a simple conversation ("Hello") — verify text output is clean
- Run a tool-use conversation — verify tool results display properly
- Run a multi-tool conversation — verify grouping works
- Run a thinking-capable model — verify thinking is not leaked, redacted_thinking is preserved
- **Packet comparison**: Run both TS and C++ clients with mitmproxy, compare raw API request JSON — verify content block order, field names, and cache_control markers match exactly
