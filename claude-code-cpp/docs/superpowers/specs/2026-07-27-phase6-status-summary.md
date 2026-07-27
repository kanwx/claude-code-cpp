# Phase 6 Status Summary

Date: 2026-07-27
Branch: `ui-polish-ftxui`
HEAD: `4f2d43b` fix(ftxui): dim narration consistently across tool contexts

## Status: P0–P2 complete

```
P0a  Read narration grouping        DONE  4a409cd
P0b  Exploration multi-kind group   DONE  1aeab4a
P0c  ToolProgress in-place replace  DONE  7d15377
P1a  Narration dimming (group only) DONE  dc81988
P1b  Phase header prefix            DONE  074de0e
P1c  Tool marker / gutter alignment DONE  d147824
P1d  TurnDuration tool count        DONE  374228e
P2a  Phase header eligibility       DONE  ccc9537
P2b  Global narration dimming       DONE  4f2d43b
```

## 1. Gaps Resolved

### Tool grouping (P0a, P0b)
- Read groups collapse across narration (P0a)
- Multi-kind exploration tools (Read+Grep+Glob) collapse into single group (P0b)
- Narration does not break collapsible groups

### Narration visibility (P1a, P2b)
- P1a: Narration dimmed within collapsible sequences (`collapseReadSearchGroups`)
- P2b: Narration dimmed globally across all tool contexts (`dimToolNarration`)
- Non-narration AnswerText unaffected

### Phase markers (P1b, P2a)
- Phase headers get ● prefix (first AnswerText after tool batch)
- Continuations get ⏺ prefix
- P2a gates eligibility: transitional tool-intro text ("Let me read...") does not get ●

### Tool hierarchy (P1c)
- All tool results / ToolGroup / CollapsedGroup use unified ⎿ prefix
- 3-char gutter alignment preserved across all block types

### Tool count (P1d)
- TurnDuration shows tool count: "Worked for 45s · 8 tools"
- Current-turn scoped, not cumulative transcript

### Progress replacement (P0c)
- ToolProgress blocks replaced in-place by their paired ToolResult
- Eliminates visual flicker during tool execution

## 2. Remaining Residuals

### 2.1 `isToolNarration()` false positive boundary (Classified: P2c candidate)

The `isToolNarration()` classifier (rule 5: starts with narration prefix) may
misclassify short substantive text that happens to start with a prefix:

| Text | Classified as narration? | Actual intent |
|---|---|---|
| "Looking at the code, there's a bug." | YES (prefix "looking at") | Substantive finding |
| "First, the pipeline has three stages." | YES (prefix "first") | Structured content |
| "Next, we need to fix the auth bug." | YES (prefix "next") | Task / action item |

These are rare in practice (the system prompt tells the model not to write
narration — `Prompts.cpp:73`), but the classifier has no content-quality
awareness. A false positive would dim text that should remain visible.

**User impact**: Low-Medium (dimmed but still readable).  
**Risk of fixing**: Medium (classifier tuning is brittle, each new exclusion
creates new blind spots).  
**Tree change required**: No.

### 2.2 Narration absorption (TS parity gap)

TS absorbs inter-tool narration into group summaries. The text "Let me read
the key files" never appears as a standalone line — it's folded into the
tool batch context. C++ treats narration as separate dimmed AnswerText blocks.

**User impact**: Medium (dimmed text is still visible noise vs. TS where it
disappears entirely).  
**Risk of fixing**: High (changes ContentBlock tree, affects transcript/API
safety, requires MessagePipeline restructuring).  
**Tree change required**: Yes (AnswerText merged into ToolGroup/CollapsedGroup metadata).

### 2.3 Non-consecutive exploration batch merging

C++ groups consecutive exploration tools (P0b). TS can appear to merge
non-consecutive exploration batches by absorbing the intervening narration.
C++ does not do this — non-consecutive batches remain separate CollapsedGroups.

**User impact**: Low (separate collapsed groups still reduce noise,
just less aggressively than TS).  
**Risk of fixing**: Medium-High (requires absorption or cross-group merging
in pipeline).  
**Tree change required**: Potentially (if merging non-consecutive groups).

### 2.4 Token / cost display parity

TS shows token count and cost in the TurnDuration line. C++ P1d added tool
count but not token/cost — those depend on TurnMetricsCollector which is
currently headless-only.

**User impact**: Low (tool count already provides useful signal; token/cost
is supplementary).  
**Risk of fixing**: Low-Medium (requires TurnMetricsCollector integration
into FTXUI path).  
**Tree change required**: No.

### 2.5 Phase auto-naming

TS uses semantic labels: `● Searching…` / `● Reading…` / `● Summary`.
C++ uses raw model text as the phase header. There is no semantic label
generation.

**User impact**: Low (raw text is usually adequate; auto-naming is cosmetic).  
**Risk of fixing**: Low (pure presentation layer).  
**Tree change required**: No.

## 3. Residual Priority Ranking

Sorted by user-visible benefit × feasibility:

| # | Residual | Benefit | Risk | Tree Change | Recommendation |
|---|---|---|---|---|---|
| 1 | `isToolNarration()` FP tuning | Medium | Medium | No | P2c — evaluate classifier precision |
| 2 | Token/cost display parity | Low | Low-Medium | No | P3a — integrate TurnMetricsCollector |
| 3 | Phase auto-naming | Low | Low | No | P3b — semantic label generation |
| 4 | Narration absorption | Medium | High | **Yes** | P3c+ — separate design cycle |
| 5 | Non-consecutive batch merge | Low | Medium-High | Maybe | Defer — depends on absorption |

### Recommended next step: P2c — Classifier precision evaluation

Not implementation. Just evaluate whether `isToolNarration()` false positives
are observable in practice:

1. Audit actual model outputs from real sessions
2. Collect false positive samples (if any)
3. Decide whether tuning is justified vs. accepting current behavior
4. If tuning is justified, propose minimal rule adjustments

Rationale: P2c is the last item that doesn't require tree changes or new
infrastructure. Everything beyond P2c either requires ContentBlock tree
changes (absorption) or new subsystem integration (token/cost display).

## 4. Non-Touch Zones (all stages)

These have been preserved through P0–P2 and remain invariant:

```
[✓] API validator / ContentBlockParam
[✓] AutoCompact / PostCompactCleanup
[✓] BashTool / Process / StreamingToolExecutor
[✓] ESC cancel / Ctrl+C double-press
[✓] Message history repair
[✓] Transcript serialization format
[✓] Headless mode (HeadlessContentBlockAccumulator)
```

## 5. Test Coverage

```
ctest: 794 tests, 100% pass (sequential + parallel)
Key test areas:
  - MessagePipeline grouping (P0a, P0b, P1a, P2b)
  - ContentBlock phase detection (P1d, P2a)
  - Renderer output (P1c, P1b prefix)
  - Bash cancel / process lifecycle
  - Auto compact continuity
  - API client retry / error recovery
```

## 6. Key Architectural Decisions

1. **dimmed flag + isFirst flag as two orthogonal axes**: `dimmed` controls
   visibility (renderer dims), `isFirst` controls marker (● vs ⏺). Both are
   set before rendering, not guessed by the renderer.

2. **Pipeline owns classification, UI owns presentation**: `isToolNarration`
   and `dimToolNarration` live in MessagePipeline. `isPhaseHeaderEligible`
   lives in FtxuiRepl. The renderer (`ContentBlockFtxui.cpp`) does neither —
   it only reads flags.

3. **Current-turn scoping**: P1d tool count and P2b dimming both use
   `currentTurnStartIndex_` to avoid mutating or re-interpreting historical
   turns. Each AnswerEnd pass processes only the current assistant turn.

4. **No ContentBlock tree changes**: P0–P2 achieved all visual improvements
   without adding new ContentBlock types, children, or structural changes.
   This kept transcript safety and API protocol intact.
