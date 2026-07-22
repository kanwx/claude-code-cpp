# Phase 6 RCA: Output Experience Parity — Baseline Diff Report

Date: 2026-07-20
Branch: `ui-polish-ftxui`
Scope: Analysis only — no implementation, no stability-zone changes

## Baseline Files

| File | Content |
|------|---------|
| [`docs/phase6/baseline-task-b.md`](../phase6/baseline-task-b.md) | Fixed golden B task, tool timeline, trace capture commands, per-patch answer template |
| [`docs/phase6/ts-output-reference.md`](../phase6/ts-output-reference.md) | What original TS Claude Code produces — observable characteristics, rendering rules |
| [`docs/phase6/cpp-output-current.md`](../phase6/cpp-output-current.md) | What C++ currently produces — DisplayEvent trace, ContentBlock trees, identified problems |

## 1. Mapped C++ Output Pipeline

```
API SSE (AnthropicClientSync)
  → AgentLoopImpl::executeLoop() — streams TypedStreamEvent
    → StreamBuffer — text accumulation, thinking tag stripping (6 variants),
                    IncrementalBlockParser for paragraph detection
      → DisplayEvent (12 types) emitted per-paragraph / per-tool-event
        → AnswerPostProcessor — per-event: cleanThinkingTags, buffer events
                                at finalize(): groupConsecutiveToolResults + reorderToolTrails
                                → emits Tombstones + ToolGroups for ≥2 consecutive same-type tools
          → FtxuiRepl::handleDisplayEvent() — runs on UI thread via screen_->Post()
            │
            │ DURING STREAMING:
            │   TextPartial → streamingText_ accumulation (+ streaming spinner)
            │   TextParagraph → flush to AnswerText ContentBlock
            │   ToolProgress → flush streaming text, push ToolProgress, track in toolProgressIndices_
            │   ToolResult → flush streaming text, remove matching ToolProgress, push ToolResult,
            │                 runIncrementalPipeline() ← currently NO-OP (defers to AnswerEnd)
            │   ToolGroup → push ToolGroup, runIncrementalPipeline()
            │   Tombstone → remove matching block
            │
            │ AT AnswerEnd:
            │   → commit remaining streamingText_ as AnswerText
            │   → clean orphan ToolProgress → "Interrupted"
            │   → run FULL MessagePipeline.process() (7 passes)
            │   → TurnDuration deferred to finishStream() (post tool-execution)
            │
            ▼
        MessagePipeline::process() — 7 passes at AnswerEnd:
          P1: reorderToolTrails — ensure ToolProgress→ToolResult adjacency
          P2: groupToolResultPairs — pair adjacent ToolProgress+ToolResult into ToolGroup
          P3: groupConsecutiveToolUses — merge ≥2 consecutive same-type ToolResults into ToolGroup
          P4: collapseReadSearchGroups — GroupAccumulator: collapse Read/Grep/Glob/LS/Bash/Web*
              into CollapsedGroup with summary ("Read 3 files · Searched 2 patterns")
              STOP CONDITION: AnswerText with content → group breaker
          P5: collapseBackgroundBash — STUB (pass-through)
          P6: collapseHookSummaries — STUB (pass-through)
          P7: collapseTeammateShutdowns — STUB (pass-through)
          + buildLookups — O(1) query tables for rendering

  → ContentBlock tree (12 types, recursive)
    → syncLayoutState() — pushes to AppLayout
      → renderFtxuiElement() per block:
          AnswerText:  "● " (first) / "⏺ " (subsequent) + text
          ToolResult:  "[toolName]" badge + summary + [Ctrl+O]
          ToolGroup:   dim summary line
          CollapsedGroup: dim summary line (e.g., "Read 3 files") + expand
          TurnDuration: "Pondered in 23s · 4.2K tokens · $0.08"
          ThinkingBlock: expandable (collapsed: 1 line "Thinking…")
  → FTXUI Screen (alternate buffer)
```

### Key Observation: Pipeline IS running at AnswerEnd

Line 495 of `FtxuiRepl.cpp`: `contentBlocks_ = messagePipeline_.process(std::move(contentBlocks_));`

The earlier gap (documented in `test_runtime_replay.cpp`) where MessagePipeline was skipped in FTXUI mode **has been fixed**. The incremental pipeline (`runIncrementalPipeline()`) is now explicitly a no-op that defers all grouping to AnswerEnd.

However, the pipeline's effectiveness is limited by a **structural problem**: `isGroupBreaker()` treats ANY AnswerText with non-whitespace content as a group boundary. Since the model naturally interleaves text narration with tool calls, most multi-tool turns escape grouping.

---

## 2. A/B/C Task Coverage Targets

### A: Sanity Baseline (confirms basic display works)

```
read CMakeLists.txt
run echo hello
```

Checks: Read expand/collapse, Bash exit code display, AnswerText prefix (●/⏺), TurnDuration presence, no blank/duplicate markers.

### B: Primary Benchmark (Phase 6 main target)

```
Analyze the project output pipeline.
Find all files related to StreamBuffer, MessagePipeline and FtxuiRepl.
Summarize their responsibilities and explain the data flow.
```

Expected natural behavior:
- Glob for `*StreamBuffer*` / `*MessagePipeline*` / `*FtxuiRepl*`
- Grep for class/function references
- Read key files (multiple, interleaved with narration)
- Bash `wc -l` on found files
- Final summary synthesizing findings

Triggers: Glob, Grep, Read, Bash multi-tool mix; multiple tool_use/tool_result cycles; interleaved narration text; staged exploration; final synthesis.

Baseline task definition: [`docs/phase6/baseline-task-b.md`](../phase6/baseline-task-b.md)

### C: Later Stress Benchmark (deferred)

```
refactor a small module or fix a small bug with tests
```

Multi-turn, long context, planning, verification — for Phase 6 late-stage regression.

---

## 3. Original TS Claude Code — Ideal Output Structure (B-type task)

For a multi-tool exploration task, the original TS Claude Code produces:

```
● Searching for StreamBuffer and auto-compact related files…
  ⎿ Found 8 files (Glob *.cpp *.hpp)

● Reading key pipeline files…
  ⎿ Read StreamBuffer.cpp (317 lines)
  ⎿ Read MessagePipeline.cpp (632 lines)
  ⎿ Read AutoCompact.cpp (298 lines)
  ⎿ Read 5 files (Read ×5 collapsed)

● Counting lines per file…
  ⎿ Ran wc -l (Bash) — 1.2K lines total

● Summarizing responsibilities…
  [answer text with structured summary table]

  Pondered in 45s · 8.2K tokens · $0.12 · 8 tools
```

**Key characteristics of the TS output:**

1. **Phase headers** (`● Searching…`, `● Reading…`, `● Counting…`) — semantic grouping of tools by exploration phase, driven by model's natural paragraph breaks between tool batches
2. **Collapsed tool groups** with verb-based summaries ("Read 5 files", "Searched 2 patterns") — past tense when followed by answer, present continuous ("Reading 3 files…") when more tools follow
3. **`⎿` margin prefix** — consistent visual hierarchy: `●` for assistant text, `⎿` for tool results
4. **Streaming progress** — ToolProgress shows spinner + activity description during tool execution, replaced by result on completion
5. **Completion summary** — TurnDuration line at end with timing, token count, cost, tool count
6. **Minimal noise** — no repeated meta-commentary, no blank paragraphs between tools, no raw JSON/escape sequences
7. **Staged reveal** — during streaming, user sees phases accumulate naturally (not all tools appear at once at the end)

---

## 4. Current C++ Output Structure (B-type task)

For the same task, current C++ produces:

```
⏺ Let me search for files related to auto-compact and StreamBuffer.

Glob  Found 8 files  [Ctrl+O to expand]
⏺ Now let me read the key files.

Read  Read StreamBuffer.cpp (317 lines)  [Ctrl+O to expand]
Read  Read MessagePipeline.cpp (632 lines)  [Ctrl+O to expand]
Read  Read AutoCompact.cpp (298 lines)  [Ctrl+O to expand]
Read  Read AgentLoopCompact.cpp (240 lines)  [Ctrl+O to expand]
Read  Read PostCompactCleanup.cpp (49 lines)  [Ctrl+O to expand]
⏺ Let me count the lines.

Bash  wc -l *.cpp → 1.2K lines  [Ctrl+O to expand]
⏺ Here's a summary of responsibilities:

[answer text]

Pondered in 45s · 8.2K tokens · $0.12
```

**Key differences from TS:**

| # | Observation | Root Cause |
|---|-------------|------------|
| 1 | No phase headers | No phase-detection logic; AnswerText blocks are rendered independently |
| 2 | Read×5 NOT collapsed | AnswerText "Now let me read…" breaks `isGroupBreaker()` in P4 |
| 3 | Glob NOT grouped with Reads | Different tool categories → different GroupAccumulator kinds → flush on kind change |
| 4 | No `⎿` margin prefix | `ToolResult` uses badge style (e.g., `[Read]`) not margin prefix |
| 5 | AnswerText uses `⏺` not `●` | Only first AnswerText gets `●`; subsequent get `⏺` |
| 6 | Individual tool results visible | Pipeline P3 groups only ≥2 CONSECUTIVE same-type — text between breaks adjacency |
| 7 | No tool count in TurnDuration | TurnDuration shows time/tokens/cost but not tool count |

---

## 5. Gap Classification

### Layer 1: Prompt / Model Behavior (NOT in scope for Phase 6 code changes)

| Gap | Description | Severity |
|-----|-------------|----------|
| P1 | Model interleaves text narration between every tool call | Structural — cannot fix in pipeline alone |
| P2 | No model-side phase grouping signal | Would need prompt engineering to emit phase markers |

**Decision**: These are model behavior issues, not C++ pipeline bugs. We must handle the interleaved-text pattern in the pipeline rather than expecting the model to change.

### Layer 2: StreamBuffer / Event Shaping

| Gap | Description | Severity | Fix |
|-----|-------------|----------|-----|
| S1 | TextPartial→TextParagraph granularity is model-driven | Low | Already correct |
| S2 | Thinking tag stripping works for 6 tag variants | Low | Already adequate |
| S3 | No detection of "tool narration" vs "meaningful answer text" | **HIGH** | Need text classifier in pipeline to distinguish inter-tool narration |

**S3 is the key enabler**: If we can classify short text blocks between tools as "narration" (not group breakers), the existing `collapseReadSearchGroups` will correctly collapse tools across narration boundaries.

### Layer 3: AnswerPostProcessor (pre-pipeline)

| Gap | Description | Severity | Fix |
|-----|-------------|----------|-----|
| A1 | `groupConsecutiveToolResults` only fires on ≥2 consecutive same-type | Medium | Redundant with P3/P4; could simplify or remove |
| A2 | Tombstone mechanism erases ToolProgress → ToolResult transition | Low | Working correctly |

**Note**: AnswerPostProcessor runs BEFORE FtxuiRepl's ContentBlock construction. Its ToolGroups and Tombstones feed into the ContentBlock construction, THEN MessagePipeline runs at AnswerEnd. This dual-processing means some blocks are already grouped (by AnswerPostProcessor) before the pipeline sees them. The pipeline handles both raw ToolResults and pre-grouped ToolGroups.

### Layer 4: MessagePipeline (core grouping)

| Gap | Description | Severity | Fix |
|-----|-------------|----------|-----|
| M1 | `isGroupBreaker()` treats ALL non-whitespace AnswerText as breaker | **CRITICAL** | Add narration detection; short text between same-phase tools should not break |
| M2 | `categorizeBlock` flushes on kind change (Read vs Glob) | **HIGH** | Allow multi-kind groups within same exploration phase |
| M3 | P5/P6/P7 are STUBs | Low | Fill when needed; low user-visible impact |
| M4 | `hasContentAfterIndex` only checks remaining blocks in current turn | Medium | Can misclassify past vs present tense for turn-final groups |

**M1 root cause analysis**:

```
Actual stream:  AT("Let me read X") → Read → AT("Now Y") → Read → AT("And Z") → Read
                                     ↑                    ↑
                              isGroupBreaker = true  isGroupBreaker = true
                              → flush group         → flush group
Result: 3 separate Read ToolResults (or 3 separate CollapsedGroups with 1 Read each)
Desired: 1 CollapsedGroup "Read 3 files"
```

### Layer 5: FtxuiRepl State / Rendering

| Gap | Description | Severity | Fix |
|-----|-------------|----------|-----|
| F1 | No phase header rendering | **HIGH** | AnswerText between tool batches → render as phase header with `●` prefix |
| F2 | `●`/`⏺` prefix logic: only first AnswerText gets `●` | Medium | Phase-first AnswerText should also get `●` |
| F3 | TurnDuration deferred to finishStream() — correct but lacks tool count | Low | Add tool count to TurnDuration metadata |
| F4 | Streaming shows raw ToolProgress→ToolResult transitions (spinner flicker) | Low | Acceptable; no worse than TS |

### Layer 6: ContentBlockRenderer Layout

| Gap | Description | Severity | Fix |
|-----|-------------|----------|-----|
| R1 | No `⎿` margin prefix system | Medium | Add left margin prefix based on block type and nesting |
| R2 | ToolResult badge style `[Read]` vs TS `⎿` prefix | Medium | Unify with margin prefix system |
| R3 | CollapsedGroup expand hint is `[Ctrl+O to expand]` | Low | Adequate; matches TS prompt |

### Layer 7: Completion Summary

| Gap | Description | Severity | Fix |
|-----|-------------|----------|-----|
| C1 | No per-turn completion summary section | **HIGH** | Add summary block after last AnswerText showing tool count, file count, key findings |
| C2 | TurnDuration shows time/tokens/cost but not tool count | Medium | Extend TurnDuration metadata |
| C3 | No "phase completed" indicators | Low | Implicit from CollapsedGroup summaries |

---

## 6. Gap Matrix

### Format: | Area | TS | C++ Current | Root Layer | Severity |

| # | Area | TS Behavior | C++ Current | Layer | Severity |
|---|------|-------------|-------------|-------|----------|
| G1 | Phase headers | `● Searching...` / `● Reading...` semantic grouping | All AnswerText rendered identically with `⏺` prefix | Renderer | **P0** |
| G2 | Tool collapse across narration | Readx5 → "Read 5 files" even with interleaved "Let me..." text | Readx5 → 3 collapsed + 2 individual (narration breaks group) | MessagePipeline P4 | **P0** |
| G3 | Multi-kind grouping | Glob+Grep+Read → single "Searched 2 patterns, Read 5 files" | Glob, Grep, Read in separate groups (kind change flushes) | MessagePipeline P4 | **P0** |
| G4 | Streaming progress | Spinner + activity during tool execution, replaced on result | ToolProgress→ToolResult transition (spinner flicker, no persistent progress) | FtxuiRepl | **P0** |
| G5 | Inter-tool narration noise | "Let me..." absorbed into phase header or collapsed | Each "Let me read X" / "Now I'll..." visible as separate AnswerText block | MessagePipeline P4 | **P1** |
| G6 | Margin prefix | `⎿` for tool results, `●` for assistant text | `[Glob]` badge style for tool results | Renderer | **P1** |
| G7 | AnswerText prefix | First-in-phase `●`, continuation `⏺` (or none) | Only first AnswerText gets `●`, rest get `⏺` regardless of phase | Renderer | **P1** |
| G8 | Completion summary | TurnDuration with tool count | TurnDuration without tool count | FtxuiRepl + ContentBlock | **P1** |
| G9 | Read expand default | Collapsed by default, expand on demand | Same (collapsed by default) | Renderer | OK |
| G10 | Phase spacing | Blank line between phases, compact within phase | Uniform spacing, no phase awareness | Renderer | **P2** |
| G11 | Stub passes | Background bash, hook summaries, teammate shutdowns handled | P5/P6/P7 are pass-through stubs | MessagePipeline | **P2** |
| G12 | Thinking visibility | Hidden behind expand in final frame | Same (not persisted as ContentBlock) | FtxuiRepl | OK |

### Layer Attribution Summary

| Layer | Gaps |
|-------|------|
| Prompt / Model Behavior | (out of scope — model naturally interleaves text + tools) |
| StreamBuffer / Event Shaping | (adequate — thinking stripped, paragraph boundaries correct) |
| AnswerPostProcessor | (adequate — pre-grouping doesn't harm pipeline) |
| **MessagePipeline** | **G2, G3, G5, G11** — isGroupBreaker too aggressive, kind flush too strict, 3 stubs |
| **FtxuiRepl** | **G4, G8** — streaming progress UX, TurnDuration tool count |
| **ContentBlockRenderer** | **G1, G6, G7, G10** — phase headers, margin prefix, AnswerText prefix, spacing |

### What Already Works (Do Not Regress)

| Feature | Status |
|---------|--------|
| Pipeline invocation at AnswerEnd | `FtxuiRepl.cpp:495` — full 7-pass pipeline runs |
| P3 groupConsecutiveToolUses | Consecutive same-type ToolResults merged |
| P4 collapseReadSearchGroups (basic) | CollapsedGroup created when narration absent |
| P4 buildGroupSummary (verb tense) | "Read 3 files" / "Reading 3 files..." based on hasContentAfter |
| P1 reorderToolTrails | ToolProgress→ToolResult ordered correctly |
| P2 groupToolResultPairs | Adjacent pairs merged into ToolGroup |
| TurnDuration rendering | Time/tokens/cost shown (missing only tool count) |
| Thinking tag stripping | 6 tag variants handled in StreamBuffer |
| MAX_BLOCKS cap (2000) | Tombstone on overflow |

---

## 7. Implementation Priority

### P0a: Inter-Tool Narration Classifier + Read Collapse (FIRST — this round)

Gap: **G2** — narration between Read tools prevents collapse.

Scope: `MessagePipeline.cpp` only. Add `isToolNarration()` to `isGroupBreaker()`.
Does NOT touch renderer, FtxuiRepl, ContentBlock types, or multi-kind grouping.

### P0b: Multi-Kind Phase Grouping (NEXT)

Gap: **G3** — Glob + Grep + Read in same exploration phase should merge.

Scope: `MessagePipeline.cpp` — relax `GroupAccumulator` kind flush.

### P0c: Streaming Progress Polish (AFTER)

Gap: **G4** — ToolProgress spinner flicker.

Scope: `FtxuiRepl.cpp` — smoother progress transition.

### P1: Noise Filtering + Answer Formatting

Gaps: **G5, G6, G7, G8** — secondary polish.

- Absorb / dim inter-tool narration text
- `⎿` margin prefix for tool results
- Phase-aware `●`/`⏺` AnswerText prefix
- Tool count in TurnDuration

### P2: Phase Summary + Completion Summary

Gaps: **G10, G11** — lower impact, higher complexity.

- Phase-aware spacing
- Fill P5/P6/P7 stubs (background bash, hooks, teammates)

---

## 8. Per-Patch Acceptance Template

Every Phase 6 commit must answer these 5 questions:

```
1. Which layer(s) changed?
   → [StreamBuffer / AnswerPostProcessor / MessagePipeline / FtxuiRepl / Renderer]

2. Why does TS do it this way?
   → [reference to docs/phase6/ts-output-reference.md section]

3. What was the C++ gap?
   → [gap # from Section 6 matrix, with before trace metrics]

4. TTY before/after diff?
   → CLUAIDE_CODE_DEBUG_METRICS=1 trace:
     CollapsedGroup count, ToolResult count, AnswerText count

5. Stability zones unaffected?
   → [confirm: no changes to Bash kill / cancel lifecycle / auto-compact / API protocol]
```

### Trace Diff Template

```bash
# Run baseline task B before and after each patch
CLAUDE_CODE_DEBUG_METRICS=1 ./build/claude-cli 2>/tmp/before.log
# → paste: "Analyze the project output pipeline. Find all files related to..."

# Key metrics to diff
grep "AFTER\[" /tmp/before.log | grep -c "CollapsedGroup"
grep "AFTER\[" /tmp/before.log | grep -c "ToolResult"
grep "AFTER\[" /tmp/before.log | grep -c "AnswerText"
```

### Regression TTY Samples (must still pass after every Phase 6 patch)

```
read CMakeLists.txt
run echo hello
run sh -c 'sleep 30 & wait'  → ESC cancel  (P3/P4 stability zone)
/compact                        (auto-compact stability zone)
```

Verify no regressions:
- API 400: 0
- Cannot send: 0
- consecutive user/assistant: 0
- orphan tool_result: 0
- synthetic ack: 0
- ctest: all PASS

---

## 9. Stability Zones — DO NOT TOUCH

These are Path 7 / P4 areas that are validated and closed. Phase 6 changes must NOT modify:

| Zone | Files | Commits |
|------|-------|---------|
| API protocol guard | AgentLoopImpl, ApiTypes, ContentBlockParam | P0-P2 |
| TurnDuration timing | FtxuiRepl, TurnMetricsCollector | P1 |
| History tool_result repair | AgentLoopCompact, ContentBlockParam | P2 |
| ESC cancel lifecycle | FtxuiRepl (ESC handler), StreamingToolExecutor | P3 |
| Ctrl+C double-press | FtxuiRepl (signal handler) | P3b |
| Cancelled tool repair / serialized JSON | ContentBlockParam, StreamingToolExecutor | P3c |
| Read expanded preview + FTXUI hardening | FtxuiRepl (Read display), ContentBlockRenderer | P3d |
| Bash process group kill | BashTool, Process, StreamingToolExecutor | P4 |
| Auto-compact continuity | AutoCompact, PostCompactCleanup, AgentLoopCompact | Compact |

**Phase 6 touches only**: `MessagePipeline.cpp`, `ContentBlockFtxui.cpp`, `ContentBlock.hpp`, `FtxuiRepl.cpp` (AnswerEnd TurnDuration population only).

---

## Appendix A: Key Code Pointers

| What | Where |
|------|-------|
| Pipeline isGroupBreaker | `src/stream/MessagePipeline.cpp:240-279` |
| Pipeline collapseReadSearchGroups | `src/stream/MessagePipeline.cpp:323-452` |
| Pipeline groupConsecutiveToolUses | `src/stream/MessagePipeline.cpp:126-183` |
| AnswerPostProcessor group logic | `src/stream/AnswerPostProcessor.cpp:44-84` |
| FtxuiRepl AnswerEnd + pipeline call | `src/ui/FtxuiRepl.cpp:421-515` |
| FtxuiRepl runIncrementalPipeline (NO-OP) | `src/ui/FtxuiRepl.cpp:765-780` |
| ContentBlock type enum | `include/claude/stream/ContentBlock.hpp:13-26` |
| AnswerText rendering (●/⏺ prefix) | `src/ui/renderers/ContentBlockFtxui.cpp:169-186` |
| CollapsedGroup rendering | `src/ui/renderers/ContentBlockFtxui.cpp:390-411` |
| TurnDuration rendering | `src/ui/renderers/ContentBlockFtxui.cpp:484` |
| Test documenting FTXUI vs headless gap | `tests/metrics/test_runtime_replay.cpp` |
| Design doc with 8 original gaps | `docs/superpowers/specs/2026-06-16-message-pipeline-alignment-design.md` |

## Appendix B: What Changed Since Design Doc

The 2026-06-16 design doc listed the pipeline as "70% implemented" with pipeline-skip-at-AnswerEnd as the critical gap. Since then:

1. **Pipeline now runs at AnswerEnd** (`FtxuiRepl.cpp:495`) — this gap is CLOSED
2. **runIncrementalPipeline is now a deliberate no-op** — deferring all grouping to AnswerEnd for correctness. The doc's `LightIncremental` proposal is superseded by this simpler approach.
3. **The remaining gap is not pipeline invocation, but pipeline effectiveness**: `isGroupBreaker()` is too aggressive, preventing the existing `collapseReadSearchGroups` from doing its job across interleaved narration text.
