# TS Output Reference — Baseline Task B

## What the Original TS Claude Code Produces

For the fixed B task ("Analyze the project output pipeline. Find all files related to
StreamBuffer, MessagePipeline and FtxuiRepl..."), the original TS Claude Code
produces this output structure:

### Phase 1: Exploration

```
● Searching for pipeline-related files…
  ⎿ Found 8 files matching *StreamBuffer*, *MessagePipeline*, *FtxuiRepl* (Glob)

● Finding class definitions and key functions…
  ⎿ Found 12 matches across 6 files (Grep)
```

### Phase 2: Reading (collapsed)

```
● Reading pipeline source files…
  ⎿ Read 5 files (Read ×5 collapsed)
     StreamBuffer.cpp (317 lines) — text buffering + thinking tag stripping
     StreamBuffer.hpp (89 lines)  — DisplayEvent emission interface
     MessagePipeline.cpp (632 lines) — 7-pass grouping pipeline
     MessagePipeline.hpp (76 lines) — pipeline config + GroupAccumulator
     FtxuiRepl.cpp (~800 lines) — ContentBlock construction + AnswerEnd handler
```

### Phase 3: Counting

```
● Counting lines…
  ⎿ Ran wc -l (Bash) → 1,914 lines total across 5 files
```

### Phase 4: Summary

```
● Data Flow Summary

  The output pipeline follows this path:

  API SSE → AgentLoop → TypedStreamEvent → StreamBuffer → DisplayEvent
  → AnswerPostProcessor → FtxuiRepl::handleDisplayEvent → MessagePipeline
  → ContentBlock tree → renderFtxuiElement() → FTXUI Screen

  Key responsibilities:
  - StreamBuffer: text accumulation, <thinking> tag stripping (6 variants),
    paragraph boundary detection via IncrementalBlockParser
  - MessagePipeline: 7-pass post-processing at AnswerEnd:
    reorderToolTrails → groupToolResultPairs → groupConsecutiveToolUses
    → collapseReadSearchGroups → (3 stubs) → buildLookups
  - FtxuiRepl: ContentBlock construction during streaming, full pipeline
    invocation at AnswerEnd, TurnDuration deferred to finishStream()

  Pondered in 45s · 8.2K tokens · $0.12 · 7 tools
```

## Observable Characteristics

### What you SEE

| Feature | Behavior |
|---------|----------|
| Phase headers | `● Searching…` / `● Reading…` / `● Summary` — semantic grouping |
| Tool collapse | Read×5 → single "Read 5 files" line with expand capability |
| Margin prefix | `⎿` for all tool results, `●` for assistant text |
| Streaming progress | Spinner + "Reading StreamBuffer.cpp…" during tool execution |
| Completion line | "Pondered in 45s · 8.2K tokens · $0.12 · 7 tools" |
| Spacing | Blank line between phases, compact within phase |
| Meta narration | Minimal — tool-intro text absorbed into phase headers |

### What you DON'T see

| Anti-pattern | Absent because |
|--------------|----------------|
| Raw tool_use JSON | Rendered as styled blocks |
| Repeated "Let me…" per tool | Absorbed into phase header or collapsed |
| Blank/empty paragraphs | Filtered at stream level |
| ToolProgress lingering after result | Replaced immediately |
| Individual Read×5 lines | Collapsed into group |
| Thinking tags in final output | Stripped (or hidden behind expand) |

### Key Rendering Rules (TS)

```
1. First AnswerText in a turn → ● prefix (bold)
2. AnswerText between tool batches → ● prefix if it starts a new phase
3. AnswerText continuing same section → ⏺ prefix (or no prefix)
4. ToolResult → ⎿ prefix, dimmed summary, [Ctrl+O] expand hint
5. CollapsedGroup → ⎿ "Read N files · Searched M patterns" [Ctrl+O]
6. TurnDuration → right-aligned or inline, dimmed, with tool count
7. Phase spacing → 1 blank line between phases
8. Within-phase → no blank lines between same-phase items
```

## Pipeline Characteristics (TS)

The TS pipeline does these things that the C++ pipeline currently does NOT:

1. **Inter-tool narration absorption**: Short text like "Let me read X" between tools
   is classified as narration and does NOT prevent tool grouping. The text content
   may be used as the phase header.

2. **Phase boundary detection**: When tool type changes significantly (Read→Bash→Write)
   OR when AnswerText is substantive (not just tool narration), a new phase begins.

3. **Multi-kind grouping**: Glob + Grep + Read in the same exploration phase merge
   into one CollapsedGroup with a compound summary.

4. **Completion synthesis**: After all tools finish, a TurnDuration line is rendered
   with: timing, token count, cost, AND tool count.
