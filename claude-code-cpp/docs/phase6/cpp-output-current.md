# C++ Output Current — Baseline Task B

## What the Current C++ Version Produces

For the same fixed B task, captured with `CLAUDE_CODE_DEBUG_METRICS=1`.

### Tool Timeline (from DisplayEvent trace)

Expected DisplayEvent sequence:

```
AnswerStart
TextPartial "Let me search for pipeline-related files."
TextPartial "Let me search for pipeline-related files.\n\n"
ToolProgress call_1 Glob "Searching for *StreamBuffer*..."
ToolResult call_1 Glob "Found 8 files"
TextPartial "Now let me find the class definitions."
ToolProgress call_2 Grep "Searching for class StreamBuffer..."
ToolResult call_2 Grep "Found 12 matches"
TextPartial "Let me read the key files."
ToolProgress call_3 Read "Reading StreamBuffer.cpp"
ToolResult call_3 Read "Read StreamBuffer.cpp (317 lines)"
TextPartial "Now MessagePipeline.cpp."
ToolProgress call_4 Read "Reading MessagePipeline.cpp"
ToolResult call_4 Read "Read MessagePipeline.cpp (632 lines)"
TextPartial "And the headers."
ToolProgress call_5 Read "Reading StreamBuffer.hpp"
ToolResult call_5 Read "Read StreamBuffer.hpp (89 lines)"
ToolProgress call_6 Read "Reading MessagePipeline.hpp"
ToolResult call_6 Read "Read MessagePipeline.hpp (76 lines)"
ToolProgress call_7 Read "Reading FtxuiRepl.cpp"
ToolResult call_7 Read "Read FtxuiRepl.cpp (~800 lines)"
TextPartial "Let me count the lines."
ToolProgress call_8 Bash "Running wc -l"
ToolResult call_8 Bash "1,914 lines total"
TextPartial "The output pipeline follows..."
TextPartial "...FTXUI Screen."
AnswerEnd
```

### ContentBlock Tree BEFORE Pipeline (Layer 4)

Expected structure at AnswerEnd, before `messagePipeline_.process()`:

```
[0]  AnswerText      "Let me search for pipeline-related files.\n\n"
[1]  ToolResult      (Glob)    "Found 8 files"
[2]  AnswerText      "Now let me find the class definitions."
[3]  ToolResult      (Grep)    "Found 12 matches"
[4]  AnswerText      "Let me read the key files."
[5]  ToolResult      (Read)    "Read StreamBuffer.cpp (317 lines)"
[6]  AnswerText      "Now MessagePipeline.cpp."
[7]  ToolResult      (Read)    "Read MessagePipeline.cpp (632 lines)"
[8]  AnswerText      "And the headers."
[9]  ToolResult      (Read)    "Read StreamBuffer.hpp (89 lines)"
[10] ToolResult      (Read)    "Read MessagePipeline.hpp (76 lines)"
[11] ToolResult      (Read)    "Read FtxuiRepl.cpp (~800 lines)"
[12] AnswerText      "Let me count the lines."
[13] ToolResult      (Bash)    "1,914 lines total"
[14] AnswerText      "The output pipeline follows this path: ..."
```

Key observation: **ToolResults at [9] and [10] and [11] ARE consecutive** (no AnswerText between them). P3 `groupConsecutiveToolUses` WILL merge these. But [5], [7], [9-11] are separated by AnswerText blocks.

### ContentBlock Tree AFTER Pipeline (Layer 5)

Expected structure after `messagePipeline_.process()`:

```
[0]  AnswerText      "Let me search for pipeline-related files.\n\n"
[1]  ToolResult      (Glob)    "Found 8 files"
[2]  AnswerText      "Now let me find the class definitions."
[3]  ToolResult      (Grep)    "Found 12 matches"
[4]  AnswerText      "Let me read the key files."
[5]  ToolResult      (Read)    "Read StreamBuffer.cpp (317 lines)"
[6]  AnswerText      "Now MessagePipeline.cpp."
[7]  ToolResult      (Read)    "Read MessagePipeline.cpp (632 lines)"
[8]  AnswerText      "And the headers."
[9]  CollapsedGroup            "Read 3 files"  ← P3 merged [9],[10],[11]
                                     children: Read×3 (hpp, hpp, cpp)
[10] AnswerText      "Let me count the lines."
[11] ToolResult      (Bash)    "1,914 lines total"
[12] AnswerText      "The output pipeline follows..."
```

**Critical finding**: Even after the pipeline, only 3 of 5 Read results are collapsed. Read at [5] and [7] remain individual because AnswerText at [4] and [6] breaks the group in `isGroupBreaker()`.

Also: Glob at [1] and Grep at [3] remain individual — not grouped with Read because:
1. They're different tool categories (Search vs FileRead)
2. AnswerText separates them

### What the User Sees (Layer 6 — FTXUI render)

```
⏺ Let me search for pipeline-related files.

Glob  Found 8 files  [Ctrl+O to expand]

⏺ Now let me find the class definitions.

Grep  Found 12 matches  [Ctrl+O to expand]

⏺ Let me read the key files.

Read  Read StreamBuffer.cpp (317 lines)  [Ctrl+O to expand]

⏺ Now MessagePipeline.cpp.

Read  Read MessagePipeline.cpp (632 lines)  [Ctrl+O to expand]

⏺ And the headers.

⎿ Read 3 files  [Ctrl+O to expand]          ← CollapsedGroup (only 3 of 5)

⏺ Let me count the lines.

Bash  1,914 lines total  [Ctrl+O to expand]

⏺ The output pipeline follows this path:

  API SSE → AgentLoop → TypedStreamEvent → ...

  Pondered in 45s · 8.2K tokens · $0.12     ← NO tool count
```

### Identified Problems

| # | Problem | Severity | Layer |
|---|---------|----------|-------|
| 1 | Read×5 not fully collapsed (only 3/5) | HIGH | MessagePipeline P4 |
| 2 | Glob + Grep not grouped with Read | HIGH | MessagePipeline P4 |
| 3 | Inter-tool narration ("Let me…", "Now…") visible as separate AnswerText blocks | HIGH | MessagePipeline P4 + Renderer |
| 4 | No phase headers (all AnswerText rendered identically) | HIGH | Renderer |
| 5 | Only 1st AnswerText gets `●`, rest get `⏺` | MEDIUM | Renderer |
| 6 | ToolResult uses badge style `[Glob]` not `⎿` margin | MEDIUM | Renderer |
| 7 | TurnDuration missing tool count | MEDIUM | FtxuiRepl |
| 8 | AnswerText after CollapsedGroup gets `⏺` not `●` | MEDIUM | Renderer |
| 9 | CollapsedGroup present (pipeline works) but under-triggers | HIGH | MessagePipeline |

### What Already Works

| Feature | Status |
|---------|--------|
| Pipeline invocation at AnswerEnd | WORKS — `FtxuiRepl.cpp:495` |
| P3 groupConsecutiveToolUses (consecutive same-type) | WORKS — merged Read×3 at [9-11] |
| P4 collapseReadSearchGroups (basic) | WORKS — CollapsedGroup created |
| P4 buildGroupSummary (verb tense) | WORKS — "Read 3 files" |
| P1 reorderToolTrails | WORKS — progress→result ordered |
| P2 groupToolResultPairs | WORKS — pairs merged into ToolGroup |
| TurnDuration rendering | WORKS — time/tokens/cost shown |
| Thinking tag stripping | WORKS — 6 tag variants |
| ContentBlock type system | WORKS — all 12 types available |
| MAX_BLOCKS cap (2000) | WORKS — tombstone on overflow |
