# P6-P1 Integrated Validation Report

Date: 2026-07-24
Branch: `ui-polish-ftxui`
HEAD: `374228e` fix(ftxui): show tool count in turn duration
Status: CLOSED

## P6-P1 Commits

```
dc81988 P1a: dim inter-tool narration
074de0e P1b: phase-aware AnswerText prefix
d147824 P1c: unified tool result margin prefixes
374228e P1d: TurnDuration tool count
```

## Baseline Task B Renderer Trace

Post-pipeline ContentBlock tree rendered through `renderFtxuiElement()` at HEAD:

```
   Let me search for pipeline-related files.          ← dimmed (P1a)
  ⎿  Glob  Found 8 files                                ← ⎿ unified (P1c)
   Now let me find the class definitions.               ← dimmed (P1a)
  ⎿  Grep  Found 12 matches                             ← ⎿ unified (P1c)
 ● Let me read the key files.                           ← ● phase header (P1b)
  ⎿  Read  Read StreamBuffer.cpp (317 lines)            ← ⎿ unified (P1c)
   Now MessagePipeline.cpp.                             ← dimmed (P1a)
  ⎿  Read  Read MessagePipeline.cpp (632 lines)         ← ⎿ unified (P1c)
   And the headers.                                     ← dimmed (P1a)
  ⎿  3 files                                            ← CollapsedGroup w/⎿ (P1c)
   Let me count the lines.                              ← dimmed (P1a)
  ⎿  Bash  1914 lines total across 5 files              ← ⎿ unified (P1c)
 ● The output pipeline follows...                       ← ● phase header (P1b)

 ✻ Worked for 45s · 8 tools                            ← tool count (P1d)
```

## Metrics

| Metric | Count |
|---|---|
| Total AnswerText blocks | 7 |
| Dimmed narration (P1a) | 5 |
| Phase headers with ● (P1b) | 2 |
| Continuations with ⏺ (P1b) | 0 |
| Individual ToolResult blocks | 5 |
| CollapsedGroup blocks | 1 (3 internal tools) |
| TurnDuration tool count | 8 |

## Before vs After

| Aspect | Before | After | Fix |
|---|---|---|---|
| Narration visibility | Full brightness | Dimmed, recedes visually | P1a |
| Phase markers | All AnswerText same `⏺` | `●` for phase headers, `   ` for narration | P1b |
| Tool margin prefix | `focusMarker` only, CollapsedGroup uses `⏺` | All tools use `⎿` | P1c |
| Gutter alignment | 2/3/4/5 char mixed | 3-char uniform (●/⏺/⎿ at column 1) | P1c |
| TurnDuration | `✻ Worked for 45s` | `✻ Worked for 45s · 8 tools` | P1d |

## Acceptance

```
[✓] narration dimmed (P1a)
[✓] phase headers use ● (P1b)
[✓] continuations use ⏺ (P1b)
[✓] tool results / ToolGroup / CollapsedGroup use ⎿ (P1c)
[✓] CollapsedGroup no longer misuses ⏺ (P1c)
[✓] TurnDuration shows tool count (P1d)
[✓] sequential ctest: 769/769 PASS
[✓] parallel ctest: 769/769 PASS
[✓] MessagePipeline / compact / cancel / Bash kill / API protocol unchanged
```

## Non-Touch Zones Confirmed

```
[✓] ContentBlock.hpp — no new fields since P1b (isFirst)
[✓] ContentBlockFtxui.cpp — renderer only, no structural changes
[✓] ContentBlockParam.cpp — API validation unchanged
[✓] MessagePipeline — 7-pass grouping unchanged
[✓] TurnDurationRenderer.hpp — non-interactive path unchanged
[✓] HeadlessContentBlockAccumulator.hpp — headless path unchanged
[✓] TurnMetricsCollector.cpp — independent counting path unchanged
[✓] AutoCompact / PostCompactCleanup — untouched
[✓] Process / BashTool / StreamingToolExecutor — untouched
[✓] ESC / Ctrl+C lifecycle — untouched
```

## Remaining Gaps vs TS Reference

| Feature | Gap | Scope |
|---|---|---|
| Token count in FTXUI TurnDuration | Not present; requires TurnMetricsCollector refactor | Out of P1 scope |
| Cost in FTXUI TurnDuration | Not present (headless only) | Out of P1 scope |
| Phase auto-naming (`● Searching…`) | Uses raw AnswerText; no semantic label generation | Out of P1 scope |
| Phase spacing (blank lines between phases) | Not implemented | Out of P1 scope |
| Phase header text quality | P1b promotes first non-dimmed AnswerText after tool batches to ●. Short narration like "Let me read the key files" can still appear as a phase header. This is acceptable for P1 but remains a known residual — future work may improve via prompt guidance or heuristic suppression of short transitional text. | Known residual |
| Inter-tool narration absorption | Narration stays as separate dimmed blocks; not absorbed into phase headers or tool summaries | Out of P1 scope |
| Multi-kind exploration grouping (non-consecutive) | C++ supports consecutive multi-kind exploration grouping after P0b, but it does not yet absorb inter-tool narration or merge non-consecutive exploration batches the way TS can appear to do. The gap is non-consecutive exploration grouping / narration absorption, not "only same-type grouped." | Out of P1 scope |
| Right-aligned TurnDuration | C++ uses inline `✻` marker | Minor |

## Conclusion

P6-P1 is closed. The visual hierarchy has moved from:

> narration + fragmented tools + wrong markers + no tool count

to:

> dimmed narration + grouped tools + ●/⏺/⎿ hierarchy + tool count

Known residuals:
1. Phase header text quality still depends on model output
2. Inter-tool narration remains a separate dimmed block instead of being fully absorbed
3. Non-consecutive exploration grouping is not fully TS-equivalent
4. Token/cost in FTXUI TurnDuration remains out of scope
5. Phase spacing / auto-naming remains out of scope
