# Stream Rendering Fixes: Flicker, Jank, and Layout Alignment

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix the three user-facing output quality problems in claude-code-cpp: text flicker/jank during streaming, thinking content flashing, and layout misalignment with the TypeScript original.

**Architecture:** Fix the implementation to match the already-approved 5-layer pipeline design (2026-06-09), wire the missing AnswerPostProcessor, and align rendering details with TS behavior. No new architecture — strict compliance with existing specs.

**Tech Stack:** C++17, ftxui, Anthropic Messages API

---

## Background

The 2026-06-09 Streaming Pipeline Rewrite design defined a clean 5-layer architecture:

```
SSE → StreamBuffer → AnswerPostProcessor → ContentBlockRenderer → Terminal
```

The types (`TypedStreamEvent`, `DisplayEvent`, `ContentBlock`), the `StreamBuffer`, `IncrementalBlockParser`, and `AnswerPostProcessor` are all implemented. But three implementation bugs and two missing wiring steps cause severe output quality problems.

---

## Root Cause Analysis

### Bug 1 (CRITICAL): Per-token TextPartial emission

**File:** `src/stream/StreamBuffer.cpp`, line 147

```cpp
// CURRENT (broken):
textAccumulator_ += cleanText;
emit(DisplayEvent{.type = DisplayEventType::TextPartial, .text = cleanText}); // EVERY token!
```

The design spec says TextPartial is a **safety-net** emitted only when the buffer exceeds FLUSH_THRESHOLD (256 chars) without a block boundary. Emitting it per-token causes `screen_->Post()` (full terminal redraw) on every single SSE token. A 500-token paragraph triggers 500 redraws → severe flicker and jank.

### Bug 2: Duplicate text via TextParagraph + TextPartial overlap

Because B1 emits TextPartial per-token AND `flushTextBuffer()` emits TextParagraph with the full accumulator, the UI receives overlapping text events. The `FtxuiRepl` accumulates TextPartial into `streamingText_`, then TextParagraph commits the same text again.

### Bug 3: Thinking text may leak into display

The `StreamBuffer` rate-limits thinking updates correctly (50ms/256 char thresholds), but `FtxuiRepl` stores `thinkingText_` directly. If any code path accidentally renders `thinkingText_` as visible content, thinking text flickers on screen. The current code is defensive but lacks explicit guards.

### Bug 4 (CRITICAL): AnswerPostProcessor not wired

`src/stream/AnswerPostProcessor.cpp` is compiled and tested, but NOT connected between `StreamBuffer` and `FtxuiRepl`. Result:
- Consecutive tool results never get grouped into `ToolGroup`
- Tool results appear mid-text instead of being reordered after text
- `Tombstone` deduplication never fires

### Bug 5: ToolProgress removal is O(n) and fragile

`FtxuiRepl` uses `std::remove_if` on `contentBlocks_` vector to replace ToolProgress with ToolResult. If the toolCallId doesn't match (edge case), orphaned "● Running..." blocks persist in the display.

### Bug 6: contentBlocks_ grows unbounded across turns

No per-turn boundary tracking. `contentBlocks_` accumulates all blocks from all turns indefinitely, with no lifecycle management.

---

## Design: Fixes

### Fix 1: StreamBuffer TextDelta — eliminate per-token emit

**File:** `src/stream/StreamBuffer.cpp` — `feed(TypedStreamEvent&&)` TextDelta handler

**Before:**
```cpp
case StreamEventType::TextDelta: {
    // ... thinking tag stripping ...
    if (cleanText.empty()) break;
    textAccumulator_ += cleanText;
    emit(DisplayEvent{.type = DisplayEventType::TextPartial, .text = cleanText});  // BAD
    if (blockParser_.append(cleanText)) {
        flushTextBuffer(false);
    } else if (textAccumulator_.size() >= FLUSH_THRESHOLD) {
        flushTextBuffer(false);
    }
    break;
}
```

**After:**
```cpp
case StreamEventType::TextDelta: {
    // ... thinking tag stripping (unchanged) ...
    if (cleanText.empty()) break;

    textAccumulator_ += cleanText;
    bool hasBoundary = blockParser_.append(cleanText);

    if (hasBoundary && blockParser_.lastBoundaryPos() > 0) {
        // Paragraph boundary detected: flush complete paragraph
        lastEmittedPos_ = 0;
        flushTextBuffer(false);
    } else if (textAccumulator_.size() >= FLUSH_THRESHOLD) {
        // Safety-net: long paragraph with no boundary.
        // Emit ONLY the delta since last emit. The UI APPENDS TextPartial,
        // so sending the full accumulator would cause duplication.
        String delta = textAccumulator_.substr(lastEmittedPos_);
        lastEmittedPos_ = textAccumulator_.size();
        if (!delta.empty()) {
            emit(DisplayEvent{
                .type = DisplayEventType::TextPartial,
                .text = delta
            });
        }
        // Note: do NOT clear textAccumulator_. The full paragraph is sent
        // as TextParagraph at the next boundary or StreamEnd.
    }
    break;
}
```

**Key changes:**
1. TextPartial emitted ONLY as a safety-net (rare), not per-token
2. TextPartial sends incremental delta (tracked by `lastEmittedPos_`), not full accumulator
3. This prevents duplication since `FtxuiRepl` APPENDS TextPartial text to `streamingText_`
4. UI redraws drop from ~500/turn to ~2-5/turn

**New member in StreamBuffer.hpp:**
```cpp
size_t lastEmittedPos_ = 0;  // Track position already sent via TextPartial
```

### Fix 2: flushTextBuffer — keep current logic, benefit from Fix 1

`flushTextBuffer` currently emits `TextParagraph` with `std::move(textAccumulator_)` and clears the buffer. This is correct behavior — it sends the complete paragraph at boundaries. After Fix 1 removes the per-token TextPartial overlap, this becomes clean.

No code change needed for `flushTextBuffer` itself.

### Fix 3: Thinking tag filtering — cross-chunk and fullwidth variants

The current `stripThinkingTags` handles standard tags but has two gaps:
1. **Cross-chunk tags**: `"<thin"` and `"king>"` arriving in separate TextDelta chunks aren't detected
2. **Fullwidth variants in FtxuiRepl secondary filter**: Only standard `<think>` / `<thinking>` tags are stripped; CJK fullwidth variants (`＜thinking＞`) leak through

#### 3a. StreamBuffer: Enhanced tag detection with prefix buffering

**File:** `src/stream/StreamBuffer.cpp`

Add `thinkingTagBuffer_` to accumulate partial tag prefixes across chunks:

```cpp
// New member in StreamBuffer.hpp:
String thinkingTagBuffer_;  // Buffer partial tag prefixes across chunks

// Enhanced stripThinkingTags with prefix-aware scanning:
// Before processing, prepend any buffered partial prefix from the previous chunk.
// After processing, if the text ends with a prefix of any known tag, buffer it
// for the next chunk and remove it from output.
//
// Known tag prefixes to detect:
//   "<", "<t", "<th", "<thi", "<thin", "<think", "<thinki", "<thinkin",
//   "<thinking", "</", "</t", ... etc.
//   Fullwidth: "＜", "＜t", ... etc.
```

**Implementation approach** (state machine, not regex):
1. Before processing `cleanText`, prepend `thinkingTagBuffer_` to it
2. Scan character by character, maintaining a match state against all known tag patterns (both standard and fullwidth variants)
3. When a full open tag matches → set `inThinkingTag_ = true`, discard matched chars
4. When `inThinkingTag_` is true, discard all chars until a close tag matches
5. When a close tag matches → set `inThinkingTag_ = false`
6. At end of chunk, if a partial prefix of any tag is buffered → save to `thinkingTagBuffer_` for next chunk
7. Return only non-tag, non-discarded text

#### 3b. FtxuiRepl: Fullwidth variants in secondary filter

**File:** `src/ui/FtxuiRepl.cpp` — `handleDisplayEvent(TextPartial)`

The current secondary filter only handles `<think>` and `<thinking>`. Add fullwidth variants:

```cpp
case DisplayEventType::TextPartial: {
    String filtered = ev.text;
    static const std::vector<std::pair<String, String>> residualTags = {
        {"<think>", "</think>"},
        {"<thinking>", "</thinking>"},
        // CJK fullwidth angle bracket variants
        {"\xef\xbc\x9c" "think" "\xef\xbc\x9e",
         "\xef\xbc\x9c" "/think" "\xef\xbc\x9e"},
        {"\xef\xbc\x9c" "thinking" "\xef\xbc\x9e",
         "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e"},
        // HTML-entity encoded variants
        {"&lt;think&gt;", "&lt;/think&gt;"},
        {"&lt;thinking&gt;", "&lt;/thinking&gt;"},
    };
    for (const auto& [openTag, closeTag] : residualTags) {
        size_t pos = 0;
        while ((pos = filtered.find(openTag)) != String::npos) {
            auto endPos = filtered.find(closeTag, pos + openTag.size());
            if (endPos != String::npos) {
                filtered.erase(pos, endPos + closeTag.size() - pos);
            } else {
                filtered.erase(pos);
                break;
            }
        }
        // Also strip orphan close tags
        while ((pos = filtered.find(closeTag)) != String::npos) {
            filtered.erase(pos, closeTag.size());
        }
    }
    if (filtered.empty()) break;
    streamingText_ += filtered;
    streamingRenderer_.append(filtered);
    // ...
}
```

#### 3c. ContentBlockRenderer guard

**File:** `src/ui/renderers/ContentBlockFtxui.cpp`

Add render-time guard for ThinkingBlock:
- When collapsed (`!block.expanded`): render only "∴ Thinking" indicator, never `block.detailText`
- When expanded: render `block.detailText` only if explicitly toggled by user (Ctrl+O)
- Debug assertion: `assert(!block.detailText.empty() || !block.expanded)`

### Fix 4: Wire AnswerPostProcessor into the pipeline

**File:** `src/bootstrap/AgentRunner.cpp`

Insert `AnswerPostProcessor` between `StreamBuffer` callback and `FtxuiRepl::handleDisplayEvent()`:

```cpp
// In AgentRunner::runStreaming(), when setting up the StreamBuffer callback:

buffer_->setDisplayCallback([this](DisplayEvent&& event) {
    if (event.type == DisplayEventType::AnswerEnd) {
        // Phase 1: process the AnswerEnd itself
        auto processed = postProcessor_.process(std::move(event));
        repl_->handleDisplayEvent(std::move(processed));
        
        // Phase 2: finalize — group tools, reorder traces, emit tombstones
        auto finalEvents = postProcessor_.finalize();
        for (auto& fe : finalEvents) {
            repl_->handleDisplayEvent(std::move(fe));
        }
        postProcessor_.reset();
    } else {
        auto processed = postProcessor_.process(std::move(event));
        repl_->handleDisplayEvent(std::move(processed));
    }
});
```

**File:** `include/claude/bootstrap/AgentRunner.hpp`

Add member:
```cpp
#include "claude/stream/AnswerPostProcessor.hpp"
// ...
AnswerPostProcessor postProcessor_;
```

**Expected behavior after wiring:**
1. Consecutive collapsible tool results (Read, Grep, Bash, etc.) → single `ToolGroup` with count summary
2. Individual tool results get `Tombstone` events; UI replaces them with group
3. Tool results that appear between text paragraphs → reordered after all text
4. AnswerEnd signaling triggers `finalize()` → final layout applied

### Fix 5: ToolProgress cleanup optimization

**File:** `src/ui/FtxuiRepl.cpp` — `handleDisplayEvent(ToolResult)`

Replace `std::remove_if` (O(n) scan) with direct index tracking:

```cpp
// Add member to FtxuiRepl:
std::map<String, size_t> toolProgressIndices_;  // toolCallId -> index in contentBlocks_

// In ToolProgress handler:
case DisplayEventType::ToolProgress: {
    // ... commit streaming text ...
    ContentBlock cb;
    cb.type = ContentBlock::ToolProgress;
    cb.toolCallId = ev.toolCallId;
    // ...
    toolProgressIndices_[ev.toolCallId] = contentBlocks_.size();
    contentBlocks_.push_back(std::move(cb));
    break;
}

// In ToolResult handler:
case DisplayEventType::ToolResult: {
    // ... commit streaming text ...
    // O(1) lookup and removal
    auto it = toolProgressIndices_.find(ev.toolCallId);
    if (it != toolProgressIndices_.end()) {
        size_t idx = it->second;
        if (idx < contentBlocks_.size() &&
            contentBlocks_[idx].type == ContentBlock::ToolProgress &&
            contentBlocks_[idx].toolCallId == ev.toolCallId) {
            contentBlocks_.erase(contentBlocks_.begin() + idx);
            // Update all indices after the removed position
            for (auto& [tid, i] : toolProgressIndices_) {
                if (i > idx) --i;
            }
        }
        toolProgressIndices_.erase(it);
    }
    // ... create ToolResult ContentBlock ...
    break;
}
```

**Guard at AnswerEnd:** Clean up any remaining ToolProgress blocks:
```cpp
case DisplayEventType::AnswerEnd: {
    // Convert orphaned ToolProgress to ToolResult("Interrupted")
    for (auto& [callId, idx] : toolProgressIndices_) {
        if (idx < contentBlocks_.size() &&
            contentBlocks_[idx].type == ContentBlock::ToolProgress) {
            contentBlocks_[idx].type = ContentBlock::ToolResult;
            contentBlocks_[idx].summary = ToolResultSummary::dim("Interrupted");
            contentBlocks_[idx].resultStatus = ToolResultStatus::Cancelled;
        }
    }
    toolProgressIndices_.clear();
    // ...
    break;
}
```

### Fix 6: Per-turn content block lifecycle

**File:** `src/ui/FtxuiRepl.cpp`

Add turn boundary tracking:

```cpp
// Members:
size_t currentTurnStartIndex_ = 0;  // Index in contentBlocks_ where current turn starts

// In AnswerStart handler:
case DisplayEventType::AnswerStart: {
    currentStreamingBlocks_ = 0;
    currentTurnStartIndex_ = contentBlocks_.size();
    isFirstAnswerBlock_ = true;
    // ...
    break;
}

// In AnswerEnd handler:
case DisplayEventType::AnswerEnd: {
    // Turn complete — tool grouping and reordering already applied
    // by AnswerPostProcessor. Mark turn boundary.
    turnBoundaries_.push_back(contentBlocks_.size());
    // If contentBlocks_ exceeds a reasonable limit, trigger compaction
    // (handled by existing VirtualScroll/OffscreenFreeze)
    break;
}
```

**Rationale:** contentBlocks_ keeps all history (scrollback). Turn boundaries enable virtual scrolling to skip rendering off-screen blocks.

**Hard cap for safety:** If `contentBlocks_.size()` exceeds `MAX_BLOCKS` (e.g., 2000), trim oldest blocks at AnswerEnd:

```cpp
static constexpr size_t MAX_BLOCKS = 2000;

// In AnswerEnd handler, after processing:
if (contentBlocks_.size() > MAX_BLOCKS) {
    size_t toRemove = contentBlocks_.size() - MAX_BLOCKS / 2;
    contentBlocks_.erase(contentBlocks_.begin(), contentBlocks_.begin() + toRemove);
    // Rebuild toolProgressIndices_ with adjusted positions
    toolProgressIndices_.clear();
    for (size_t i = 0; i < contentBlocks_.size(); ++i) {
        if (contentBlocks_[i].type == ContentBlock::ToolProgress) {
            toolProgressIndices_[contentBlocks_[i].toolCallId] = i;
        }
    }
}
```

---

## Layout Alignment with TS

### Prefix/Icons Alignment

| Element | Before | After (matches TS) |
|---------|--------|---------------------|
| First answer text | `⏺ ` | `⏺ ` (keep, correct) |
| Subsequent answer text | `  ` | `  ` (keep, correct) |
| Tool result | `  ⎿ ` | `  ⎿ ` (keep, correct) |
| Thinking collapsed | `  ◈ Thinking` | `  ∴ Thinking` |
| Thinking expanded | `  ◈ Thinking...` | `  ∴ Thinking...` |
| Error | `  ✕ error` | `  ✕ error` (keep, correct) |
| Turn duration | `  ● Pondered 23s` | `  ● Pondered in 23s · 4.2K tokens · $0.08` |

### Rendering Order Per Turn

Guaranteed order (enforced by AnswerPostProcessor):
1. **AnswerText** blocks (in API response order)
2. **ThinkingBlock** (collapsed, "∴ Thinking" indicator)
3. **ToolGroup** (grouped collapsible tool results, reordered after text)
4. **ToolResult** (individual non-collapsible tool results)
5. **TurnDuration** (metadata line with time/tokens/cost)

---

## Implementation Order

| Step | Fix | Risk | Rationale |
|------|-----|------|-----------|
| 1 | **B1**: Per-token TextPartial removal | Medium | Highest user-impact. Must verify paragraph detection for long paragraphs (>256 chars). |
| 2 | **B3**: Enhanced thinking tag filtering | Low | Cross-chunk state machine; fullwidth variants in FtxuiRepl secondary filter. |
| 3 | **B4**: Wire AnswerPostProcessor | Medium | Changes event flow. Verify `finalize()` doesn't double-emit. Independent of B1/B3. |
| 4 | **B5**: ToolProgress O(1) map-based cleanup | Low | Map lookup is strictly faster. Guard against orphaned progress at AnswerEnd. |
| 5 | **B6**: contentBlocks_ lifecycle + MAX_BLOCKS cap | Low | Purely additive. Hard cap prevents unbounded memory growth. |

**Recommended**: B1 + B3 first (direct user-facing impact), then B4 (structural grouping), then B5 + B6 (optimizations).

## Files Modified

| File | Change |
|------|--------|
| `src/stream/StreamBuffer.cpp` | **Fix B1**: Remove per-token TextPartial, emit only at boundary/threshold |
| `include/claude/stream/StreamBuffer.hpp` | **Fix B1**: Add `lastEmittedPos_` member for delta tracking |
| `src/ui/FtxuiRepl.cpp` | **Fix B3**: Thinking defensive guard; **Fix B5**: O(1) tool progress cleanup; **Fix B6**: Turn boundary tracking; Layout alignment |
| `src/ui/renderers/ContentBlockFtxui.cpp` | **Fix B3**: ThinkingBlock render guard; Layout alignment (prefix/icons) |
| `src/bootstrap/AgentRunner.cpp` | **Fix B4**: Wire AnswerPostProcessor between StreamBuffer and FtxuiRepl |
| `include/claude/bootstrap/AgentRunner.hpp` | **Fix B4**: Add AnswerPostProcessor member |

---

## Verification Strategy

### Unit Tests

- **StreamBuffer**: Verify TextPartial NOT emitted for tokens under threshold; verify TextParagraph emitted at block boundaries (blank lines, headers); verify TextPartial emitted when threshold exceeded without boundary
- **AnswerPostProcessor**: Verify tool grouping (consecutive Read/Read/Grep → ToolGroup), verify tool reordering (text before tools), verify Tombstone emission
- **ContentBlockFtxui**: Verify ThinkingBlock never renders raw thinking text when collapsed

### Integration Test

Full pipeline simulation: SSE events → StreamBuffer → AnswerPostProcessor → ContentBlock[] → verify structure (text blocks before tool blocks, groups formed, no orphaned progress blocks)

### Manual Verification

```bash
# 1. Build
make build

# 2. Run against Claude API
./build/claude -p "Read the file CMakeLists.txt and search for 'CXX' in it"

# 3. Verify:
# - No per-token flicker during text output
# - Thinking shown as collapsed "∴ Thinking" (not raw text)
# - Tool results grouped and reordered after answer text
# - No orphaned "● Running..." blocks
# - Turn duration shows time, tokens, and cost
```

---

## What This Does NOT Change

- The 5-layer architecture is preserved exactly as designed
- `StreamBuffer`, `IncrementalBlockParser`, `AnswerPostProcessor` interfaces unchanged
- `ContentBlock` type definition unchanged
- `TypedStreamEvent` / `DisplayEvent` types unchanged
- Tool implementations (BashTool, ReadTool, etc.) unchanged
- API client (AnthropicClient, OpenAIClient) unchanged
- The Phase 1/2/3 streaming alignment from 2026-06-11 spec is NOT re-implemented (already done)
