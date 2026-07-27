# P6-P0c RCA: ToolProgress Flicker / Replacement Polish

## 1. Event Chain: ToolProgress → ToolResult

### Complete flow

```
AgentLoop (tool execution)
  → StreamToolEvent{Started, activity="Reading file.cpp..."}
  → StreamBuffer::feed(StreamToolEvent)
    → flushTextBuffer(false)          // commit pending text as TextParagraph
    → emit DisplayEvent{ToolProgress, toolCallId, toolName, activity}
  → display callback (AgentRunner.cpp:389-420)
    → AnswerPostProcessor::process()  // light cleanup, passes through
    → FtxuiRepl::handleDisplayEvent() // screen_->Post(...)
      → flush streamingText_ → AnswerText block
      → push ContentBlock::ToolProgress at contentBlocks_.back()
      → toolProgressIndices_[callId] = contentBlocks_.size() - 1

  ... tool executes, model may stream more text ...

  → StreamToolEvent{Completed, summary=ToolResultSummary{...}}
  → StreamBuffer::feed(StreamToolEvent)
    → flushTextBuffer(false)
    → emit DisplayEvent{ToolResult, toolCallId, toolName, summary}
  → FtxuiRepl::handleDisplayEvent()
    → flush streamingText_ → AnswerText block
    → O(1) lookup toolProgressIndices_[callId] → index N
    → contentBlocks_.erase(begin + N)          // REMOVE from position N
    → shift all toolProgressIndices_[*] > N    // fix up other spinners' indices
    → push ContentBlock::ToolResult at contentBlocks_.back()  // APPEND at end
    → runIncrementalPipeline()                 // NO-OP

  ... more tools may run ...

  → DisplayEvent{AnswerEnd}
  → FtxuiRepl::handleDisplayEvent()
    → orphan ToolProgress cleanup (convert to "Interrupted" ToolResult)
    → MessagePipeline::process(contentBlocks_)  // 7-pass grouping
      → Pass 2: groupToolResultPairs (adjacent ToolProgress+ToolResult → ToolGroup)
      → Pass 4: collapseReadSearchGroups (adjacent ToolResults → CollapsedGroup)
    → lastStableIndex_ = contentBlocks_.size()
```

### Render chain

```
FtxuiRepl::BuildMainComponent()
  → layoutState_.content.contentBlocks = &contentBlocks_   // pointer to vector
  → AppLayoutComponent::Render()
    → for each block in *contentBlocks_:
        renderFtxuiElement(block)                           // ContentBlockFtxui.cpp

ToolProgress rendering (ContentBlockFtxui.cpp:257-264):
  hbox({ "  ⎿ ", renderToolBadge(toolName), " ", text(activity) | dim })

ToolResult rendering (ContentBlockFtxui.cpp:267-348):
  hbox({ renderToolBadge(toolName), " ", summary | dim, ctrlOHint })
```

## 2. Root Cause: Erase+Append Instead of In-Place Replacement

### The problem

In `FtxuiRepl.cpp:280-317`, when a ToolResult arrives:

```cpp
// Step A: Remove ToolProgress from its original position N
contentBlocks_.erase(contentBlocks_.begin() + static_cast<long>(idx));

// Step B: Append ToolResult at the end
contentBlocks_.push_back(std::move(cb));  // <-- DIFFERENT position than N
```

This creates a **position shift**: the ToolResult appears at a different location than the ToolProgress spinner it replaces.

### Single-tool scenario (minor impact)

With one tool at a time, ToolProgress is usually the last block. The erase+append is a near no-op visually since "end" and "position N" are the same place:

```
Before:  [AnswerText] [ToolProgress: Read file1...]           ← spinner at end
After:   [AnswerText] [ToolResult: Read file1 (312 lines)]     ← result at end
```

### Multi-tool parallel scenario (major impact)

When multiple tools execute concurrently (parallel reads, independent bashes, etc.):

```
Before (3 parallel reads running):
  [AnswerText "Let me read the key files"]
  [ToolProgress: Read file1.cpp...]     ← index 1
  [ToolProgress: Read file2.cpp...]     ← index 2
  [ToolProgress: Read file3.cpp...]     ← index 3

After file1 completes (current erase+append):
  [AnswerText "Let me read the key files"]
  [ToolProgress: Read file2.cpp...]     ← shifted UP from index 2 to 1
  [ToolProgress: Read file3.cpp...]     ← shifted UP from index 3 to 2
  [ToolResult: Read file1.cpp (312 lines)]  ← at END, not where its spinner was
```

The user sees:
- file1 spinner vanishes from its position
- file2/file3 spinners JUMP UP by one line
- file1 result appears at the BOTTOM

This is the primary flicker/discontinuity. Each tool completion causes a cascading position shift of all remaining spinners.

### Why screen_->Post() batching doesn't help

Both the erase and push_back happen inside the SAME `screen_->Post()` callback, so there is no intermediate frame showing the erased-but-not-yet-appended state. The issue is NOT a mid-frame glitch — it's that the final frame shows the result at the WRONG position.

### TS reference behavior

From `docs/phase6/ts-output-reference.md:82`:

> ToolProgress lingering after result | Replaced immediately

The TS implementation does **in-place replacement**: the ToolProgress block at index N is transformed into a ToolResult block at the SAME index N. The spinner "morphs" into the result without position shift.

## 3. Secondary Issue: Text Flushing Causes AnswerText Fragmentation

### The mechanism

Both `StreamBuffer::feed(StreamToolEvent::Started)` and `StreamBuffer::feed(StreamToolEvent::Completed)` call `flushTextBuffer(false)` before emitting their DisplayEvent. This commits any text the model streamed during tool execution as an AnswerText block.

Sequence for a typical tool:
```
1. Model streams: "Let me search for pipeline files."
2. flushTextBuffer → AnswerText "Let me search for pipeline files." at position 0
3. ToolProgress(Glob, "Searching...") at position 1
4. Model streams: "Found 8 files. Let me read them."
5. flushTextBuffer → AnswerText "Found 8 files. Let me read them." at position 2
6. ToolProgress removed from position 1, ToolResult(Glob) appended at position 2
```

Final layout:
```
● Let me search for pipeline files.       ← position 0
● Found 8 files. Let me read them.        ← position 1 (was 2, shifted up)
  ⎿ [Glob] Found 8 files                  ← position 2 (ToolResult appended)
```

The ToolResult appears AFTER narration text that was streamed DURING tool execution. While this respects temporal order (the user saw the narration before the result), it creates a semantic mismatch: the result is separated from the text that INTRODUCED it.

### Impact assessment

This is a **lower-severity** issue than the position shift. The temporal ordering is arguably correct for streaming UX — the user saw things in this order. However, the TS reference absorbs such narration text into phase headers (P6-P0a partially addresses this by classifying narration and allowing pass-through).

## 4. Tertiary Issue: AnswerEnd CollapsedGroup Replacement

### How P6-P0a/P0b deferred grouping affects streaming

`runIncrementalPipeline()` is deliberately a no-op (FtxuiRepl.cpp:765-780). During streaming, each tool result appears as an individual `ToolResult` block with full detail. At AnswerEnd, `MessagePipeline::process()` replaces groups of adjacent ToolResults with `CollapsedGroup` summary blocks.

```
During streaming:
  ⎿ [Read] Read file1.cpp (312 lines)  [Ctrl+O]
  ⎿ [Read] Read file2.cpp (145 lines)  [Ctrl+O]
  ⎿ [Read] Read file3.cpp (267 lines)  [Ctrl+O]

At AnswerEnd (one-time transformation):
  ⎿ Read 3 files  [Ctrl+O]
```

### Is this a problem?

**No.** This is the designed behavior and matches the TS reference. The user sees detailed progress during execution, and a clean summary at the end. The TS reference also shows collapsed groups in final output (line 22-28 of ts-output-reference.md).

The one-time transformation at AnswerEnd is acceptable because:
1. It only happens once per API round, not per tool
2. The CollapsedGroup appears at the SAME position as the first ToolResult it replaces
3. The summary is more useful than individual lines for the final output

### Potential improvement (out of P0c scope)

If desired, the transition could be smoothed by keeping the first ToolResult line and fading/shifting the others, but this is cosmetic polish, not a functional issue.

## 5. TS vs C++ Streaming Comparison

| Aspect | TS Original | C++ Current | Gap |
|--------|-------------|-------------|-----|
| ToolProgress display | Spinner + "Reading X..." | `⎿ [Badge] activity` dimmed | Same line format, different decoration |
| ToolProgress→ToolResult transition | In-place replacement (same position) | Erase+append (different position) | **GAP: position shift** |
| Parallel tool progress | Stable positions, in-place morph | Cascading shifts on each completion | **GAP: visual instability** |
| Inter-tool narration | May be absorbed into phase header | Flushed as AnswerText between tools | Partially addressed by P6-P0a |
| Tool collapse during stream | Unknown (likely deferred) | Deferred to AnswerEnd (no-op) | Likely same |
| AnswerEnd collapse | Grouped summary | Grouped summary (P6-P0a/P0b) | **Parity achieved** |

## 6. Gap Location

The gap is in **FtxuiRepl** (`FtxuiRepl.cpp:280-317`), specifically the ToolResult handler. The `DisplayEvent` layer and `StreamBuffer` are correct — they emit the right events at the right times. The renderer (`ContentBlockFtxui`) is correct — it renders whatever block type it receives at whatever position.

The fix is localized to the ToolResult handler's ToolProgress removal strategy in TWO files (identical pattern):

| File | Lines | Role |
|------|-------|------|
| `src/ui/FtxuiRepl.cpp` | 280-317 | FTXUI mode — visible position shift |
| `include/claude/metrics/HeadlessContentBlockAccumulator.hpp` | 104-138 | Headless mode — ordering affects pipeline |

Both use the same erase+append pattern. The visual flicker only affects FTXUI, but the ordering issue (ToolResult at wrong position relative to inter-tool text) affects both modes.

### Not a gap in:
- **StreamBuffer**: Correctly emits ToolProgress then ToolResult
- **AnswerPostProcessor**: Correctly passes through events
- **ContentBlockFtxui rendering**: Correctly renders blocks at their positions
- **MessagePipeline**: Correctly groups at AnswerEnd
- **DisplayEvent types**: Complete and correctly typed

## 7. Minimal Fix Proposal

### Strategy: In-place ToolProgress → ToolResult transformation

Instead of erase+append, replace the ToolProgress block in-place:

```cpp
// Current (FtxuiRepl.cpp:280-317):
//   contentBlocks_.erase(begin + idx);          // remove
//   contentBlocks_.push_back(std::move(cb));    // append at end

// Proposed:
//   contentBlocks_[idx].type = ContentBlock::ToolResult;
//   contentBlocks_[idx].summary = std::move(cb.summary);
//   contentBlocks_[idx].resultStatus = ...;
//   contentBlocks_[idx].expanded = verboseTools_;
//   // Keep toolName and toolCallId (already set from ToolProgress)
//   // Clear activity field (no longer needed)
//   toolProgressIndices_.erase(it);  // remove from map, no index shifting needed
```

### What changes

| File | Change | Lines |
|------|--------|-------|
| `src/ui/FtxuiRepl.cpp` | In-place mutation in ToolResult handler | ~15 lines changed, ~10 removed |
| `include/claude/metrics/HeadlessContentBlockAccumulator.hpp` | Same in-place mutation (header-only) | ~10 lines changed, ~5 removed |
| `tests/stream/test_MessagePipeline.cpp` | New test: parallel tools keep stable positions | ~30 lines |
| (no header changes needed beyond the accumulator) | — | 0 |

### What does NOT change

- `toolProgressIndices_` map structure — still used for O(1) lookup, just no longer needs index shifting
- StreamBuffer — same events, same flush behavior
- MessagePipeline — same AnswerEnd processing
- ContentBlockFtxui — same rendering (ToolResult renders at whatever index it's at)
- `runIncrementalPipeline()` — still a no-op (correct)
- Auto-compact, ESC cancel, Ctrl+C, Bash kill — untouched by design

### Edge cases handled

1. **ToolProgress already removed by Tombstone (B5 cleanup)**: The lookup `contentBlocks_[idx].type == ContentBlock::ToolProgress && contentBlocks_[idx].toolCallId == callId` guard (line 286-288) catches this — the in-place mutation only happens if the block is still a ToolProgress with matching callId.

2. **Orphan ToolProgress at AnswerEnd** (line 462-471): Unchanged. Orphaned blocks are still converted to "Interrupted" ToolResult in-place.

3. **No matching ToolProgress** (tool started before FTXUI init): The `toolProgressIndices_.find()` returns end, the in-place path is skipped, and the ToolResult is still pushed as a new block. No regression.

4. **Concurrent tools with interleaved text**: The index map correctly identifies each ToolProgress position independently. No index shifting means adjacent ToolProgress blocks stay at stable positions.

### What the user sees after the fix

Multi-tool parallel scenario:
```
Before (3 parallel reads running):
  ● Let me read the key files
    ⎿ [Read] Reading file1.cpp...
    ⎿ [Read] Reading file2.cpp...
    ⎿ [Read] Reading file3.cpp...

After file1 completes (in-place replacement):
  ● Let me read the key files
    ⎿ [Read] Read file1.cpp (312 lines)  [Ctrl+O]   ← SAME position
    ⎿ [Read] Reading file2.cpp...                     ← UNCHANGED position
    ⎿ [Read] Reading file3.cpp...                     ← UNCHANGED position
```

No jumps. No shifts. The spinner morphs into the result.

## 8. Risks and Acceptance Criteria

### Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Regression: single-tool flow | Low | In-place mutation is semantically equivalent for single-tool case (no blocks after to shift) |
| Regression: Orphan cleanup at AnswerEnd | Low | AnswerEnd cleanup also does in-place mutation; no interaction with the new code path |
| Regression: MessagePipeline Pass 2 (groupToolResultPairs) | Low | Pass 2 looks for adjacent ToolProgress+ToolResult. After in-place replacement, there's no ToolProgress left, so Pass 2 is a no-op. This is correct — the ToolProgress was already consumed. |
| Rendering glitch: ToolProgress "activity" field leaked | Low | Must clear `block.activity` when mutating to ToolResult to avoid stale data in renderer |
| stableId changes causing re-render artifact | Low | Keep the same stableId (already set on the ToolProgress block); the in-place mutation preserves it |

### Acceptance Criteria

1. **Single tool**: ToolProgress → ToolResult transition shows result at same screen position (no visible jump)
2. **Parallel tools (3+)**: Each ToolResult replaces its spinner in-place; other spinners do not move
3. **Interleaved text**: Text between ToolProgress and ToolResult stays in correct temporal position
4. **No ToolProgress artifacts**: "activity" field not rendered on mutated ToolResult blocks
5. **Tombstone (B5 cleanup)**: Removing a ToolResult while its ToolProgress is still showing works correctly
6. **Orphan cleanup**: Tools that never complete (AnswerEnd arrives first) still get "Interrupted" status
7. **ctest: 723+ → all pass**
8. **Baseline Task B TTY run**: No visual regression in tool output
9. **MessagePipeline tests unchanged**: Pass 2 no longer sees ToolProgress blocks after in-place replacement
10. **HeadlessContentBlockAccumulator**: Mirror change needed (same erase+append pattern exists there)
