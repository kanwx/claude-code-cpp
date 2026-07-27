# P6-P1 RCA: Narration Noise + Phase-Aware AnswerText Rendering

Date: 2026-07-22
Branch: `ui-polish-ftxui`
Status: RCA — no implementation
Depends on: P6-P0 (all closed)

## 1. Baseline Task B Narration Analysis (Post-P0)

Task: "Analyze the project output pipeline. Find all files related to StreamBuffer,
MessagePipeline and FtxuiRepl. Summarize their responsibilities and explain the data flow."

### AnswerText Blocks Observed (7 total, 6 narration, 1 substantive)

| # | Text (abbreviated) | Classification |
|---|--------------------|----------------|
| 1 | "Let me search for all files related to these three components." | **narration → dim** |
| 2 | "Now let me read all the source files." | **narration → dim** |
| 3 | "Now let me read the implementation files and related types." | **narration → dim** |
| 4 | "Let me also look at the related event types and the AnswerPostProcessor to complete the picture." | **narration → dim** |
| 5 | "Let me also check how `runMessagePipeline` and `runIncrementalPipeline` are implemented in FtxuiRepl.cpp:" | **narration → dim** |
| 6 | "Let me check a couple more supporting files:" | **narration → dim** |
| 7 | "Now I have all the information needed. Here is the complete analysis." [followed by structured markdown] | **phase header → keep** |

### Post-P0 Pipeline Effectiveness

- `isToolNarration()` in MessagePipeline correctly identifies all 6 narration blocks
- Narration passes through without breaking collapse groups (P6-P0a)
- Tool results are correctly grouped into CollapsedGroups
- **BUT**: narration blocks are rendered as visible AnswerText with " ⏺ " prefix — identical to the final answer

## 2. Root Cause Analysis

### G5: Inter-tool Narration Noise

**Root cause:** `isToolNarration()` detects narration for pipeline grouping purposes only — it
prevents narration from breaking collapse groups — but it does not MARK the ContentBlock.
The renderer has no way to distinguish narration from substantive text.

**Current flow:**
```
StreamBuffer → DisplayEvent::TextPartial → FtxuiRepl → AnswerText ContentBlock
  → MessagePipeline::collapseReadSearchGroups:
      isToolNarration(block) == true → pass through (don't break group)
  → Renderer: " ⏺ " + text  (same prefix as final answer)
```

**What TS does:** Narration text ("Let me check...", "Now I'll read...") is either:
- Absorbed into the collapsed group summary (not shown as separate text)
- Or dimmed with a subtle prefix to indicate it's meta-commentary, not answer content

**What needs to change:**
1. MessagePipeline should mark narration blocks with `dimmed = true` (or a new `isNarration` flag)
2. Renderer should render dimmed narration with reduced visual weight

### G6: ⎿ Margin Prefix Visual Hierarchy

**Root cause:** `contentMargin()` function exists in ContentBlockFtxui.cpp (lines 75-80) but is
**dead code** — never called by any render case. Each block type hardcodes its own prefix.

```cpp
// Dead code — exists but never called:
String contentMargin(const ContentBlock& block, bool isResponse = true) {
    if (block.type == ContentBlock::UserMessage) return "";
    if (block.type == ContentBlock::AnswerText && block.isFirst) return "● ";
    if (isResponse) return "  ⎿ ";
    return "  ";
}
```

ToolResult/CollapsedGroup/ToolProgress each hardcode "  ⎿ " or use their own badge style.
There's no unified prefix system.

**What TS does:** Consistent prefix system:
- `●` for phase header text (introduces a batch of tools)
- `⎿` for tool results (subordinate to the phase header)
- Dimmed/grey for narration (or absorbed entirely)

### G7: Phase-Aware AnswerText Prefix

**Root cause:** All AnswerText blocks use identical " ⏺ " prefix (ContentBlockFtxui.cpp:182).
The `isFirst` flag on ContentBlock exists but is NOT used in the current renderer code
(the previous "● for first, bare for continuation" was intentionally replaced with "all ⏺ "
to fix gutter alignment at line 182).

```cpp
// Current code — all AnswerText gets identical prefix:
String prefix = " ⏺ ";
```

**What TS does:** Distinguishes:
- First AnswerText in a new phase → `●` prefix (bold, introduces topic)
- Continuation text (same phase) → no special marker
- Narration text → dimmed or absorbed

**What needs to change:**
1. ContentBlock needs a way to signal "this is a phase header" vs "this is continuation"
2. Renderer needs to use `●` for phase headers and dimmed/no-prefix for narration

### G8: TurnDuration Tool Count

**Root cause:** TurnDuration currently shows: `Pondered in 12s · 40.1K tokens` — no tool count.
The `tool_call_count` field is already collected by TurnMetricsCollector but not passed to
the TurnDuration ContentBlock.

**What TS does:** `Pondered in 12s · 40.1K tokens · 8 tools`

**What needs to change:**
1. ContentBlock needs a `toolCount` field (or we count children in renderer)
2. FtxuiRepl needs to count tools at AnswerEnd and include in TurnDuration text

## 3. ContentBlock Metadata Gap Analysis

Current ContentBlock fields relevant to narration/phase:

| Field | Type | Used? | Purpose |
|-------|------|-------|---------|
| `isFirst` | bool | **Dead** — renderer ignores it | Was for first-AnswerText ● prefix |
| `dimmed` | bool | Yes (AnswerText:170-171) | Dim rendering for dimmed text |
| `text` | String | Yes | Primary text content |

Missing fields needed for P1:

| Field | Type | Purpose |
|-------|------|---------|
| `isNarration` | bool | Mark tool-narration AnswerText for dim/absorb |
| `isPhaseHeader` | bool | Mark phase-introducing AnswerText for ● prefix |

**Alternative:** Use existing `dimmed` field for narration (set by pipeline), and repurpose
`isFirst` for phase header detection (set by FtxuiRepl based on position). This avoids
adding new fields to ContentBlock.

## 4. Where Narration Should Be Marked

**Recommendation: MessagePipeline marks narration, NOT the renderer.**

Reasoning:
- `isToolNarration()` already exists in MessagePipeline (lines 240-297) with the correct classifier
- The pipeline already passes narration through without breaking groups (lines 505-509)
- Adding `block.dimmed = true` at the narration pass-through point is a one-line change
- The renderer already handles `dimmed` blocks (ContentBlockFtxui.cpp:170-171)
- This avoids duplicating the narration classifier in the renderer

## 5. Where Phase Headers Should Be Detected

**Recommendation: FtxuiRepl at AnswerEnd, based on ContentBlock tree structure.**

Reasoning:
- Phase header = first AnswerText after a CollapsedGroup/ToolGroup that is NOT narration
- This detection requires knowledge of the block tree structure
- FtxuiRepl already has the ContentBlock tree at AnswerEnd (after pipeline)
- Pipeline's job is grouping/collapsing — phase detection is a rendering concern

Detection algorithm:
```
for each AnswerText block in contentBlocks_:
    if isToolNarration(block): continue  // skip narration
    prev = block at index-1
    if prev is CollapsedGroup or ToolGroup or AnswerText:
        // This AnswerText follows tool output → phase header candidate
        block.isFirst = true  // repurpose isFirst for ● prefix
    else:
        block.isFirst = false
```

## 6. Recommended Split: P1a / P1b / P1c / P1d

### P6-P1a: Mark and Dim Inter-Tool Narration

**Scope:** MessagePipeline + Renderer
**Changes:**
1. MessagePipeline::collapseReadSearchGroups — set `block.dimmed = true` when `isToolNarration()`
2. ContentBlockFtxui — ensure dimmed AnswerText renders with reduced visual weight (already has handler at line 170-171, currently renders as `text | dim | color(MacCream)`)
3. Optionally: render dimmed narration without the " ⏺ " prefix (cleaner)

**Files:** `MessagePipeline.cpp`, `ContentBlockFtxui.cpp`
**ContentBlock metadata:** No new fields — uses existing `dimmed`
**Risk:** Low — dim flag already exists and renderer already handles it
**Regression risk for P0:** None — doesn't change grouping logic
**Baseline Task B acceptance:** 6 narration lines dimmed, 1 phase header + final answer visible

### P6-P1b: Phase-Aware AnswerText Prefix (● vs ⏺)

**Scope:** FtxuiRepl + ContentBlockFtxui
**Changes:**
1. FtxuiRepl::handleDisplayEvent (AnswerEnd) — after pipeline, detect phase headers:
   - First non-narration AnswerText after tool group → `isFirst = true`
   - Others → `isFirst = false`
2. ContentBlockFtxui — use `isFirst` for prefix: `●` for phase headers, `⏺` for continuation

**Files:** `FtxuiRepl.cpp`, `ContentBlockFtxui.cpp`
**ContentBlock metadata:** Repurposes existing `isFirst` (currently dead code)
**Risk:** Low — `isFirst` is unused by renderer, no pipeline impact
**Regression risk for P0:** None — no grouping changes
**Baseline Task B acceptance:** One ● phase header before final answer, ⏺ for other non-narration text

### P6-P1c: Unify ⎿ Margin Prefix for Tool Results

**Scope:** ContentBlockFtxui only
**Changes:**
1. Refactor renderer to use `contentMargin()` consistently (activate dead code)
2. Ensure all tool result types (ToolResult, ToolGroup, CollapsedGroup) use "  ⎿ " prefix
3. Remove hardcoded prefixes from individual render cases

**Files:** `ContentBlockFtxui.cpp`
**ContentBlock metadata:** None
**Risk:** Medium — visual-only change but touches many render cases
**Regression risk for P0:** None — pure rendering, no pipeline impact

### P6-P1d: TurnDuration Tool Count

**Scope:** FtxuiRepl + possibly ContentBlock
**Changes:**
1. Count tool calls in current turn at AnswerEnd (use `toolProgressIndices_` history or count ToolResult blocks)
2. Add count to TurnDuration text: `Pondered in 12s · 40.1K tokens · 6 tools`

**Files:** `FtxuiRepl.cpp` (turn duration text assembly), possibly `ContentBlock.hpp` (new field)
**ContentBlock metadata:** May need `int toolCount` field on ContentBlock
**Risk:** Low — additive change, no logic change
**Regression risk for P0:** None

## 7. Risk Summary

| Stage | Files | Adds Field? | Touches Pipeline? | Touches Renderer? | P0 Regression? |
|-------|-------|-------------|-------------------|-------------------|----------------|
| P1a | 2 | No | Yes (dim flag) | Yes (dim render) | No |
| P1b | 2 | No | No | Yes (prefix) | No |
| P1c | 1 | No | No | Yes (refactor) | No |
| P1d | 2 | Maybe | No | No | No |

## 8. Non-Touch Zones Confirmed

```
[✓] API validator / ContentBlockParam
[✓] AutoCompact / PostCompactCleanup
[✓] BashTool / Process / StreamingToolExecutor
[✓] ESC cancel lifecycle
[✓] Ctrl+C double-press
[✓] Message history repair
[✓] P6-P0 grouping behavior (no changes to isToolNarration, isGroupBreaker, collapseReadSearchGroups logic)
[✓] StreamBuffer / AnswerPostProcessor
[✓] HeadlessContentBlockAccumulator (but may need mirror for P1a dim flag)
```

## 9. Implementation Order Recommendation

**P1a first** (narration dim) — highest impact, lowest risk, tests the dim rendering path
**P1b second** (phase prefix) — builds on P1a's cleaned-up output, uses existing `isFirst` field
**P1c third** (margin prefix) — visual polish, can be deferred
**P1d fourth** (tool count) — nice-to-have, trivial implementation

## 10. Headless Accumulator Mirror Note

P6-P1a requires mirroring the `dimmed = true` marking in `HeadlessContentBlockAccumulator`.
The accumulator doesn't run MessagePipeline until AnswerEnd, so the dim flag must be set
either:
- In the accumulator's pipeline pass (same as FtxuiRepl), OR
- In the accumulator's ToolResult handler when the matching narration text is flushed

Option A (pipeline pass) is simpler and keeps the mirror consistent with FtxuiRepl.
