# Claude Code C++ — Output Pipeline

Phase 6: output experience parity with TypeScript Claude Code.

## Directory Layout

```
docs/
├── phase6/
│   ├── baseline-task-b.md       # Golden benchmark task for Phase 6
│   ├── ts-output-reference.md   # What the original TS produces
│   └── cpp-output-current.md    # What C++ currently produces
└── superpowers/
    ├── README.md                # This file
    ├── specs/                   # Design docs and RCAs
    └── plans/                   # Implementation plans
```

## Phase 6 Work Items

Status: `x` = implemented, `·` = RCA only, ` ` = pending

| # | Item | Status | Commit | Scope |
|---|------|--------|--------|-------|
| **Blocker** | stableId validator false positive | x | `32dd367` | `ContentBlockParam.cpp` — structural JSON key check |
| **P6-P0a** | Inter-tool narration absorption | x | `4a409cd` | `MessagePipeline::isToolNarration()` classifier — allows Read grouping across narration |
| **P6-P0b** | Multi-kind exploration grouping | x | `1aeab4a` | `SearchGroup` + `ReadGroup` → `ExplorationGroup`; combined summaries |
| **P6-P0c** | ToolProgress flicker/replacement | x | `7d15377` | In-place replacement with AnswerText ordering fix in `FtxuiRepl` + `HeadlessContentBlockAccumulator` |
| P6-P1a | Narration noise dimming | · RCA | — | `MessagePipeline` marks narration `dimmed`; renderer dims |
| P6-P1b | Phase-aware AnswerText prefix | · RCA | — | ● for phase headers, ⏺ for continuation |
| P6-P1c | Unified ⎿ margin prefix | · RCA | — | Activate dead `contentMargin()` code; consistent tool prefix |
| P6-P1d | TurnDuration tool count | · RCA | — | Add tool count to TurnDuration line |

## Commit Stack (current branch `ui-polish-ftxui`)

```
e623098 docs: rewrite top-level README with three-project overview
7d15377 fix(ftxui): replace tool progress in place           ← P6-P0c
1aeab4a fix(pipeline): group exploration tools together       ← P6-P0b
4a409cd fix(pipeline): collapse read groups across narration  ← P6-P0a
32dd367 fix(api): avoid stableId validator false positives    ← blocker
47e19c0 fix(compact): preserve tool turn continuity
```

## Specs

| Date | Title | Type |
|------|-------|------|
| 2026-07-22 | [P6-P1: Narration Noise + Phase-Aware AnswerText RCA](specs/2026-07-22-p6-p1-narration-phase-aware-rca.md) | RCA |
| 2026-07-22 | [P6-P0c: ToolProgress Flicker RCA](specs/2026-07-22-p6-p0c-tool-progress-flicker-rca.md) | RCA |
| 2026-07-21 | [P6-P0b: Multi-Kind Exploration Phase Grouping RCA](specs/2026-07-21-phase6-p0b-multi-kind-rca.md) | RCA |
| 2026-07-20 | [Phase 6 Output Parity — Baseline Diff Report](specs/2026-07-20-phase6-output-parity-rca.md) | RCA |
| 2026-06-16 | [Message Pipeline Alignment](specs/2026-06-16-message-pipeline-alignment-design.md) | Design |
| 2026-06-12 | [Stream Rendering Fixes](specs/2026-06-12-stream-rendering-fixes-design.md) | Design |
| 2026-06-11 | [Message Prompt Stream Alignment](specs/2026-06-11-message-prompt-stream-alignment-design.md) | Design |
| 2026-06-02 | [Output Rendering Redesign](specs/2026-06-02-output-rendering-redesign-design.md) | Design |
| 2026-05-28 | [UI Alignment](specs/2026-05-28-ui-alignment-design.md) | Design |

## Plans

| Date | Title |
|------|-------|
| 2026-06-12 | [Stream Rendering Fixes](plans/2026-06-12-stream-rendering-fixes.md) |
| 2026-06-02 | [Output Rendering Redesign](plans/2026-06-02-output-rendering-redesign.md) |
| 2026-05-28 | [UI Alignment](plans/2026-05-28-ui-alignment.md) |

## Key Source Files

| File | Role |
|------|------|
| `src/stream/StreamBuffer.cpp` | Text accumulation, thinking tag stripping, DisplayEvent emission |
| `src/stream/MessagePipeline.cpp` | 7-pass post-processing at AnswerEnd |
| `src/stream/AnswerPostProcessor.cpp` | Per-event cleanup, tool result grouping |
| `src/ui/FtxuiRepl.cpp` | ContentBlock construction during streaming, pipeline invocation |
| `src/ui/renderers/ContentBlockFtxui.cpp` | FTXUI rendering of all ContentBlock types |
| `src/ui/components/AppLayout.cpp` | Main layout — iterates contentBlocks_ and renders |
| `include/claude/stream/ContentBlock.hpp` | ContentBlock type definitions |
| `include/claude/stream/MessageTypes.hpp` | UIMessage, GroupAccumulator, MessageLookups |
| `include/claude/metrics/HeadlessContentBlockAccumulator.hpp` | Headless mode mirror of FtxuiRepl event handling |

## Trace Capture Commands

```bash
# ANSI headless (full trace including metric counters)
CLAUDE_CODE_DEBUG_METRICS=1 CLAUDE_CODE_HEADLESS=1 \
  ./build/cmake-build-debug/claude-cli -p "Analyze the project output pipeline..." \
  > /tmp/phase6-ansi-trace.log 2>&1

# FTXUI TTY (content block dump at AnswerEnd)
CLAUDE_CODE_DEBUG_METRICS=1 script -q /tmp/phase6-tty-trace.log \
  ./build/cmake-build-debug/claude-cli -p "Analyze the project output pipeline..."
```
