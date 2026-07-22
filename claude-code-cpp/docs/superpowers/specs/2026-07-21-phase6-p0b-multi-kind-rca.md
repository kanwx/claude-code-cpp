# P6-P0b RCA: Multi-Kind Exploration Phase Grouping

## 1. Current `collapseReadSearchGroups` Kind Classification

The pipeline classifies each collapsible tool into a `Category`, then maps it to a `GroupKind`:

| Tool | Category | GroupKind |
|------|----------|-----------|
| Grep | Search | SearchGroup |
| Glob | Search | SearchGroup |
| Read | FileRead | ReadGroup |
| LS | FileList | ReadGroup (merged) |
| Bash | Bash | BashGroup |
| WebSearch | WebSearch | WebGroup |
| WebFetch | WebFetch | WebGroup |

Definition: `MessageTypes.hpp:167-201`, mapping: `MessagePipeline.cpp:215-238`.

The critical logic is at `MessagePipeline.cpp:439-442`:

```cpp
// Flush if this block starts a different kind of group
if (acc.kind != GroupAccumulator::GroupKind::None && acc.kind != gk) {
    flushGroup();
}
```

Every transition SearchGroup → ReadGroup or ReadGroup → SearchGroup forces a flush, producing a new CollapsedGroup.

## 2. Why Glob/Grep/Read Fragment Due to Kind Change

The tool sequence in a typical exploration phase is:

```
Glob → Glob → Glob → Glob → Read → Read → Read → Read → Glob → Read → Glob → ...
```

With the current kind-based flush rule:

```
Glob×4 (SearchGroup) → [FLUSH: SearchGroup→ReadGroup] → Read×4 → [FLUSH: ReadGroup→SearchGroup] → ...
```

Each Glob↔Read transition produces a separate CollapsedGroup. The model naturally interleaves search and read operations — first finding files, then reading them, then searching for more patterns, then reading more. This produces many small CollapsedGroups instead of one semantically coherent exploration group.

**Concrete example from Baseline Task B apiRound=4:**
```
Read → Glob → Glob → Read
→ CollapsedGroup "Read 1 file"
→ CollapsedGroup "Searched 2 patterns"  
→ CollapsedGroup "Read 1 file"
```
4 tools → 3 CollapsedGroups. TS reference would produce 1 exploration group.

## 3. Baseline Task B Actual AFTER Trace

From `/tmp/phase6-p0a-validator-fixed.log`:

| Round | BEFORE | AFTER | Delta | Notes |
|-------|--------|-------|-------|-------|
| 2 | 7 | 4 | -3 | 4 Globs → 1 SearchGroup |
| 3 | 13 | 6 | -7 | 8 Reads → 1 ReadGroup (narration pass-through works) |
| 4 | 12 | 10 | -2 | Read→Glob→Glob→Read, kind changes → 3 CGs |
| 5 | 14 | 11 | -3 | 5 Globs + 3 Reads → 2 CGs (Search→Read flush) |
| 6 | 15 | 13 | -2 | Read→Glob → 2 CGs |
| 12 | 28 | 23 | -5 | Larger consolidation in later rounds |
| 13 | 40 | 35 | -5 | Final answer, mostly AnswerText |

Cumulative across all rounds: 108 CollapsedGroup instances in AFTER traces (including carry-over from previous rounds after re-processing), 0 top-level ToolResult.

## 4. Which CollapsedGroups Are Search

The Globs in apiRound=2 produce a SearchGroup. Mixed rounds (4, 5, 6) produce interleaved SearchGroup and ReadGroup fragments.

Example from apiRound=4 tool sequence: `Read, Glob, Glob, Read`
- CG #1: ReadGroup ("Read 1 file")
- CG #2: SearchGroup ("Searched 2 patterns")
- CG #3: ReadGroup ("Read 1 file")

## 5. Which CollapsedGroups Are Read

The Reads in apiRound=3 produce a single ReadGroup of 8 files. Later rounds produce ReadGroups interleaved with SearchGroups.

## 6. Can We Safely Allow Glob+Grep+Read in Same Phase Grouping?

**Yes, with clear constraints.**

Glob, Grep, and Read are all **read-only exploration tools**. They don't modify state. The model uses them in a tight loop to understand the codebase:

1. Glob/Grep → find relevant files
2. Read → understand contents
3. Glob/Grep → pivot (find related files based on what was read)
4. Read → understand more

The TS reference treats this as one coherent "exploration phase." The GroupAccumulator already tracks searchCount and readOperationCount separately, so the summary can combine them:

```json
"Read 8 files and Searched for 5 patterns"
```

**Implementation**: Add a new `GroupKind::ExplorationGroup` that accepts both Search and FileRead/FileList categories, or simply merge `SearchGroup` and `ReadGroup` into one by making `toGroupKind` return the same kind for both:

```cpp
case Search:       return GroupKind::ExplorationGroup;
case FileRead:     return GroupKind::ExplorationGroup;
case FileList:     return GroupKind::ExplorationGroup;
```

## 7. How to Judge "Same Exploration Phase"

Since the pipeline runs at AnswerEnd (one API round at a time), all collapsible tools within a single pipeline invocation are part of the same exploration phase. The model has already decided what to search/read in this round. P6-P0a already handles narration pass-through between tools.

**No phase boundary detection needed** — the pipeline invocation at AnswerEnd IS the phase boundary. All collapsible tools in one round belong to one exploration phase.

## 8. Tools That Must NOT Be Cross-Kind Merged

| Tool | Reason |
|------|--------|
| **Bash** | Executes commands — can modify filesystem, install packages, run tests. Semantically different from passive exploration. |
| **Write** | Not currently collapsible. If made collapsible, must stay separate — modifies code. |
| **Edit** | Same as Write — code mutation. |
| **WebSearch** | Different data source (internet vs codebase). Keep in WebGroup. |
| **WebFetch** | Same as WebSearch — keep separate. |
| **TaskOutput** | Agent sub-task result — different semantics. |
| **SendMessage** | Agent communication — different semantics. |
| **AskUserQuestion** | UI interaction — different semantics. |

These remain in their own GroupKind (BashGroup, WebGroup) and continue to trigger flushes when transitioning from ExplorationGroup.

## 9. Minimal Implementation Plan

### Scope: `MessagePipeline.cpp`, `MessageTypes.hpp`, plus tests

**Step 1**: Add `ExplorationGroup` to `GroupKind` enum, or rename/simplify the mapping.

Option A (minimal): Change `toGroupKind` to merge Search and Read:
```cpp
case Search:
case FileRead:
case FileList:     return GroupKind::ExplorationGroup;
```

Option B (explicit): Add ExplorationGroup and update all switch statements.

**Recommendation: Option A** — minimal diff, clear semantics.

**Step 2**: Update `MessagePipeline.cpp:215-238` — `categorizeBlock` (no change needed, categories stay distinct for stats tracking).

**Step 3**: Update `MessagePipeline.cpp:440-442` — no code change needed. The kind-change flush automatically stops fragmenting Search↔Read transitions because they now share the same GroupKind.

**Step 4**: Update `buildFinalizedSummary` / `buildActiveSummary` to produce good combined text:
```
"Read 8 files and Searched 5 patterns"
```
instead of separate "Read 8 files" and "Searched 5 patterns".

**Step 5**: Update tests in `test_MessagePipeline.cpp`, `test_collapse_strategies.cpp`, `test_HeadlessContentBlockAccumulator.cpp`.

**Files to modify (estimated)**:
- `include/claude/stream/MessageTypes.hpp` — 3 lines (toGroupKind mapping)
- `src/stream/MessagePipeline.cpp` — ~20 lines (summary generation)
- `tests/stream/test_MessagePipeline.cpp` — ~30 lines (new test cases)
- `tests/metrics/test_collapse_strategies.cpp` — ~10 lines (updated expectations)
- `tests/metrics/test_HeadlessContentBlockAccumulator.cpp` — ~10 lines (updated expectations)

**Total estimated**: ~5 files, ~75 lines changed.

## 10. Risks and Acceptance Criteria

### Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Read details lost in combined summary | Medium | GroupAccumulator tracks both stats; summary combines them |
| Bash accidentally merged | Low | BashGroup is separate in toGroupKind, kind-change flush still fires |
| Over-grouping — too much collapsed | Low | P6-P0a narration pass-through still preserves text between groups; AnswerEnd is natural boundary |
| Summary text too long | Low | "Read N files and Searched M patterns" is still compact |

### Acceptance Criteria

1. Glob → Read → Glob sequence in same API round produces 1 CollapsedGroup, not 3
2. Read → Read produces 1 CollapsedGroup (P6-P0a regression guard)
3. Glob → Bash → Read produces 2 CollapsedGroups (Bash still flushes)
4. Glob → Read → Write produces discrete groups (Write is a breaker)
5. Summary text: "Read 8 files and Searched 5 patterns" when both present
6. Summary text: "Read 8 files" when only reads (backward compat)
7. Summary text: "Searched 5 patterns" when only searches (backward compat)
8. ctest: 723+ → all pass
9. Baseline Task B trace: fewer total CollapsedGroups, no regressions
10. Substantive AnswerText still breaks groups (P6-P0a regression guard)

### Expected improvement

Baseline Task B apiRound=4:
- **Current**: Read→Glob→Glob→Read → 3 CollapsedGroups
- **Expected**: 1 CollapsedGroup "Read 2 files and Searched 2 patterns"
