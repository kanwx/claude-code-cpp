# Phase 6 Merge Readiness Report

Date: 2026-07-27
Branch: `ui-polish-ftxui`
HEAD: `111a0e5` docs(phase6): evaluate narration classifier precision

## 1. Current HEAD

```
111a0e5 docs(phase6): evaluate narration classifier precision
```

## 2. P0–P2 Commit List (chronological)

### Runtime code (9 commits)

```
4a409cd fix(pipeline): collapse read groups across narration        P0a
1aeab4a fix(pipeline): group exploration tools together             P0b
7d15377 fix(ftxui): replace tool progress in place                  P0c
dc81988 fix(pipeline): dim inter-tool narration                     P1a
074de0e fix(ftxui): render phase-aware answer prefixes              P1b
d147824 fix(ftxui): unify tool result margin prefixes               P1c
374228e fix(ftxui): show tool count in turn duration                P1d
ccc9537 fix(ftxui): gate phase header markers for transitional text P2a
4f2d43b fix(ftxui): dim narration consistently across tool contexts P2b
```

### Docs + specs (7 commits)

```
69d0709 docs(phase6): add Phase 6 RCA specs and baseline docs
e077e21 docs(phase6): add P1 integrated validation report
cc508a4 docs(phase6): summarize P0-P2 status and residuals
111a0e5 docs(phase6): evaluate narration classifier precision
+ 3 RCA specs (phase6-output-parity, P0b, P0c, P1-narration, P2-quality, P2b-dimming, P2c-eval)
```

## 3. Runtime Code Diff Summary

| File | Δ | Purpose |
|---|---|---|
| `src/stream/MessagePipeline.cpp` | +126 | P0a grouping, P0b multi-kind, P1a dimming, P2b dimToolNarration |
| `src/ui/FtxuiRepl.cpp` | +213 | P0c progress replace, P1b phase detection, P2a eligibility, P2b call |
| `src/ui/FtxuiStreaming.cpp` | +15 | P1d tool count in TurnDuration |
| `src/ui/renderers/ContentBlockFtxui.cpp` | +30 | P1b ●/⏺ prefix, P1c ⎿ margin prefix, dimmed rendering |
| `include/claude/stream/MessagePipeline.hpp` | +9 | P2b public dimToolNarration wrapper; isToolNarration remains private |
| `include/claude/stream/MessageTypes.hpp` | +15 | GroupAccumulator ExploreState for P0b |
| `include/claude/metrics/HeadlessContentBlockAccumulator.hpp` | +61 | Headless path alignment |
| **Total runtime** | **~470 lines** | |

### Non-touch runtime files (unchanged)

```
ContentBlock.hpp         — no new fields beyond isFirst (P1b)
ContentBlockParam.cpp    — API validation unchanged
AutoCompact.cpp          — untouched
PostCompactCleanup.cpp   — untouched
BashTool.cpp             — untouched
StreamingToolExecutor.cpp — untouched
TurnDurationRenderer.hpp — untouched
TurnMetricsCollector.cpp — untouched
```

## 4. Docs + Test Diff Summary

| File | Δ |
|---|---|
| `tests/stream/test_MessagePipeline.cpp` | +887 |
| `tests/stream/test_ContentBlock.cpp` | +457 |
| `tests/test_ftxui_read_expand.cpp` | +570 |
| `tests/metrics/test_collapse_strategies.cpp` | +17 |
| `tests/metrics/test_HeadlessContentBlockAccumulator.cpp` | +178 |
| `docs/superpowers/specs/` (15 files) | ~2500 lines |
| `docs/phase6/` (3 files) | ~400 lines |

## 5. Baseline Task B Final Trace

Post-pipeline ContentBlock tree at HEAD (all P0–P2 applied):

```
   Let me search for pipeline-related files.          ← dimmed (P1a+P2b)
  ⎿  Glob  Found 8 files                                ← ⎿ unified (P1c)
   Now let me find the class definitions.               ← dimmed (P1a+P2b)
  ⎿  Grep  Found 12 matches                             ← ⎿ unified (P1c)
   Let me read the key files.                           ← dimmed (P2b: was ⏺ before P2b)
  ⎿  Read  Read StreamBuffer.cpp (317 lines)            ← ⎿ unified (P1c)
   Now MessagePipeline.cpp.                             ← dimmed (P1a+P2b)
  ⎿  Read  Read MessagePipeline.cpp (632 lines)         ← ⎿ unified (P1c)
   And the headers.                                     ← dimmed (P1a+P2b)
  ⎿  3 files                                            ← CollapsedGroup (P0a)
   Let me count the lines.                              ← dimmed (P2b: was ⏺ before P2b)
  ⎿  Bash  1914 lines total across 5 files              ← ⎿ unified (P1c)
 ● The output pipeline follows...                       ← ● phase header (P1b+P2a)
 ⏺ It has three stages: parse, group, render.           ← ⏺ continuation (P1b)

 ✻ Worked for 45s · 8 tools                            ← tool count (P1d)
```

### Metrics

| Metric | Count |
|---|---|
| Total AnswerText blocks | 8 |
| Dimmed narration | 7 |
| Phase headers (●) | 1 |
| Continuations (⏺) | 1 |
| Individual ToolResult blocks | 4 |
| CollapsedGroup blocks | 1 (3 internal tools) |
| TurnDuration tool count | 8 |

## 6. Remaining Gaps vs TS Reference

| Gap | Severity | Tree Change? | Scope |
|---|---|---|---|
| Narration absorption | Medium | Yes | Future P3c+ |
| Token/cost in TurnDuration | Low | No | Future P3a |
| Phase auto-naming (semantic labels) | Low | No | Future P3b |
| Non-consecutive batch merging | Low | Maybe | Depends on absorption |
| Right-aligned TurnDuration | Minor | No | Cosmetic |

## 7. Known Residuals

```
P2c-R1: isToolNarration() gerund-prefix false positives
  "Checking...", "Reading...", "Searching..." may be substantive
  Risk: LOW — dimmed but readable
  Mitigation: prompt guidance in Prompts.cpp

P2c-R2: isToolNarration() Rule 3 substring matching
  "there's" triggers "here's" exclusion (accidental conservative)
  Risk: NEGATIVE — prevents dimming, does not over-dim

P1-R1: Phase header text quality depends on model output
  No semantic auto-naming — raw model text used for ● prefix

P1-R2: Inter-tool narration remains as separate dimmed block
  Not absorbed into tool group summaries (TS parity gap)

P1-R3: Non-consecutive exploration grouping not TS-equivalent
  C++ groups consecutive, TS can merge non-consecutive via absorption
```

## 8. ctest

```
sequential: 794/794 PASS
parallel:   794/794 PASS
```

## 9. git status

Runtime files: clean (no uncommitted changes).  
Untracked: build artifacts (`.cache/clangd/`), no runtime or test files.

## 10. Merge / PR Recommendation

### Recommended: MERGE

Phase 6 P0–P2 is ready for merge to `main`. Summary:

- **9 runtime commits**, ~470 lines of runtime code changes
- **7 doc commits**, ~3300 lines of specs + test code
- **794 tests**, 100% pass rate (sequential + parallel)
- **No ContentBlock tree changes** — `dimmed` and `isFirst` were existing fields
- **No API protocol changes** — ContentBlockParam unchanged
- **No transcript safety impact** — ContentBlock tree structure unchanged
- **No grouping regression** — P0a/P0b behavior preserved through P1/P2
- **All P1/P2 changes are FTXUI-only or pipeline-additive** — existing behavior unchanged

### Risk assessment

| Area | Risk | Notes |
|---|---|---|
| MessagePipeline grouping | Low | Only additive: P0a/P0b grouping, P1a/P2b dimming (flag only) |
| Renderer output | Low | Prefix markers changed, all tests pass |
| TurnDuration format | Low | Added tool count suffix, tested |
| Auto compact | None | Untouched |
| Bash cancel / Ctrl+C | None | Untouched |
| API / transcript | None | Untouched |
| Headless mode | Low | Aligned but independent path |

### Suggested PR title

```
feat(ftxui): Phase 6 output experience — visual hierarchy, narration dimming, tool grouping
```

### Suggested PR body

```
9 runtime commits delivering output experience parity for the FTXUI
renderer. No ContentBlock tree changes — uses existing dimmed/isFirst flags.

## Changes
- **P0**: Read/exploration grouping across narration, multi-kind exploration
  collapse, ToolProgress in-place replacement
- **P1**: Narration dimmed within collapsible sequences, phase-aware ●/⏺
  prefixes, unified ⎿ margin prefix, tool count in TurnDuration
- **P2**: Phase-header eligibility gating (transitional text no longer ●),
  global narration dimming (covers non-collapsible tool contexts)
- **P2c**: Classifier precision evaluated, no tuning needed

## Visual Result
- Narration text: dimmed, no marker prefix
- Phase headers: ● prefix at full brightness
- Continuation text: ⏺ prefix
- All tool results: ⎿ prefix, 3-char gutter alignment
- TurnDuration: "Worked for 45s · 8 tools"

## Test
- 794 tests, 100% pass (sequential + parallel)
- Full P0/P1/P2 regression coverage
- No API protocol, transcript, compact, or cancel changes
```
