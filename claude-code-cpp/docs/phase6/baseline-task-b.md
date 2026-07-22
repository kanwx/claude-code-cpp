# Phase 6 Baseline Task B — Golden Benchmark

## Fixed Task Input

```
Analyze the project output pipeline.
Find all files related to StreamBuffer, MessagePipeline and FtxuiRepl.
Summarize their responsibilities and explain the data flow.
```

## Expected Tool Timeline

```
User: "Analyze the project output pipeline..."
  └─ Assistant thinking (streaming text + spinner)
  └─ Tool: Glob *StreamBuffer*/*MessagePipeline*/*FtxuiRepl*
  └─ ToolResult: found N files
  └─ Tool: Grep "class StreamBuffer" / "class MessagePipeline" / "class FtxuiRepl"
  └─ ToolResult: found M matches
  └─ Tool: Read StreamBuffer.cpp
  └─ ToolResult: content + line count
  └─ Tool: Read StreamBuffer.hpp
  └─ ToolResult: content + line count
  └─ Tool: Read MessagePipeline.cpp
  └─ ToolResult: content + line count
  └─ Tool: Read MessagePipeline.hpp
  └─ ToolResult: content + line count
  └─ Tool: Read FtxuiRepl.cpp (relevant section)
  └─ ToolResult: content + line count
  └─ Tool: Bash wc -l on found files
  └─ ToolResult: line counts
  └─ Final Answer: structured summary
```

## Stable Trigger Guarantees

This task reliably triggers:

| Tool | Guarantee |
|------|-----------|
| Glob | ≥1 call — pattern matching file names |
| Grep | ≥1 call — finding class/function definitions |
| Read | ≥3 calls — multiple files with interleaved narration |
| Bash | ≥1 call — wc -l or similar |
| Final Answer | Synthesized summary from tool results |

## Trace Capture Command

```bash
# C++ capture — produces stderr trace with ContentBlock tree before/after pipeline
CLAUDE_CODE_DEBUG_METRICS=1 /Users/kankan/claude-code/claude-code-cpp/build/claude-cli \
  2>/tmp/phase6-baseline-b-cpp.log

# Then paste the task input:
#   Analyze the project output pipeline.
#   Find all files related to StreamBuffer, MessagePipeline and FtxuiRepl.
#   Summarize their responsibilities and explain the data flow.

# Extract tool timeline from trace
grep "onDisplayEvent" /tmp/phase6-baseline-b-cpp.log > /tmp/phase6-baseline-b-events.log

# Extract ContentBlock tree (before pipeline)
grep "BEFORE\[" /tmp/phase6-baseline-b-cpp.log > /tmp/phase6-baseline-b-before.log

# Extract ContentBlock tree (after pipeline)
grep "AFTER\[" /tmp/phase6-baseline-b-cpp.log > /tmp/phase6-baseline-b-after.log
```

## TS Reference Capture

```bash
# Run the same task in original TS Claude Code
claude "Analyze the project output pipeline.
Find all files related to StreamBuffer, MessagePipeline and FtxuiRepl.
Summarize their responsibilities and explain the data flow."
```

## Layer-by-Layer Verification Chain

Each Phase 6 patch must produce a comparable trace. The verification chain:

```
Layer 1: Task Input (this file — fixed, never changes)
    ↓
Layer 2: Model Output Trace (API messages — CLAUDE_CODE_DEBUG_API_MESSAGES=1)
    ↓  captures: what the model actually returned (tool_use, text, thinking)
    ↓
Layer 3: DisplayEvent Trace (CLAUDE_CODE_DEBUG_METRICS=1 → "onDisplayEvent" lines)
    ↓  captures: how StreamBuffer shaped events (thinking stripped, text paragraph boundaries)
    ↓
Layer 4: ContentBlock Tree BEFORE pipeline
    ↓  ("BEFORE[" lines in CONTENT_BLOCKS_DUMP)
    ↓  captures: what FtxuiRepl built from raw DisplayEvents
    ↓
Layer 5: ContentBlock Tree AFTER pipeline
    ↓  ("AFTER[" lines in CONTENT_BLOCKS_DUMP)
    ↓  captures: what MessagePipeline produced (grouping, collapsing)
    ↓
Layer 6: FTXUI Render Snapshot
    ↓  (manual screenshot or terminal recording)
    ↓  captures: what the user actually sees
    ↓
Layer 7: Final Transcript (user-visible output)
       (copy-paste from terminal after session)
```

## Per-Patch Answer Template

Every Phase 6 patch commit message must answer:

```
1. Which layer(s) changed? (1-7 above)
2. Before trace: [key metrics from Layer 4/5/6]
3. After trace:  [key metrics from Layer 4/5/6]
4. TS alignment: [what the original does at this layer]
5. Stability: P0-P4 unaffected? (yes / no, and why)
```

## Regression Guard

After each Phase 6 patch, re-run and diff against this baseline:

```bash
# Compare DisplayEvent trace (Layer 3)
diff /tmp/phase6-baseline-b-events.log /tmp/phase6-patch-N-events.log

# Compare ContentBlock types (Layer 5)
# Key metrics: CollapsedGroup count, ToolResult count, AnswerText count
grep -c "CollapsedGroup" /tmp/phase6-baseline-b-after.log
grep -c "CollapsedGroup" /tmp/phase6-patch-N-after.log
```
