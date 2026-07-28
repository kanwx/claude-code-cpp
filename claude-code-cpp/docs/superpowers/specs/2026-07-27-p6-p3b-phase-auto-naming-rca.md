# P6-P3b RCA: Phase Auto-Naming

Date: 2026-07-27
Branch: `ui-polish-ftxui-p3`
HEAD: `66a3d8a` (P3a tokens-only)
Status: DEFER / NO-OP — no implementation

## 1. Current State

### 1.1 What the user sees

```
● Let me read the key files to understand the architecture.
  (raw model text as phase header)

⏺ The main entry point is in main.cpp:42 which calls...
  (continuation, same phase)

● Here's a summary of the changes needed:
  (raw model text after tools — classified as new phase header)
```

### 1.2 What TS Claude Code shows (per status summary doc)

```
● Searching...
● Reading...
● Summary
```

TS uses **semantic labels** that replace or summarize the raw model text. C++ uses raw model text verbatim with `●`/`⏺` prefix markers.

### 1.3 Current phase header mechanism

| Component | File | Lines | Role |
|---|---|---|---|
| Phase detection | `FtxuiRepl.cpp` | 644-673 | Sets `isFirst=true` on AnswerText after tool-like blocks |
| Eligibility gate | `FtxuiRepl.cpp` | 557-622 | `isPhaseHeaderEligible()` — denies ● for short transitional text |
| Previous-block lookup | `FtxuiRepl.cpp` | 627-642 | `findPrevSignificant()` — skips dimmed/non-eligible blocks |
| Rendering | `ContentBlockFtxui.cpp` | 180-184 | `block.isFirst ? " ● " : " ⏺ "` + `block.text` verbatim |
| `isFirst` field | `ContentBlock.hpp` | 60 | `bool isFirst = false` |

### 1.4 No semantic labeling exists

Search for `phaseLabel`, `phase_name`, `PhaseSummary`, `autoName`, `auto_label`, `semanticLabel` across the entire codebase returned **zero results**. The only phase-adjacent field is `bool isFirst`.

## 2. Available Tool Context for Phase Inference

### 2.1 Tool classification infrastructure (already exists)

The codebase has two independent tool classification systems:

**a) `GroupAccumulator::Category`** (`MessageTypes.hpp:176`):
```cpp
enum Category {
    Search,       // Grep, Glob with patterns
    FileRead,     // Read
    FileList,     // LS / Glob without patterns
    Bash,         // Non-search bash commands
    WebSearch,    // WebSearch
    WebFetch,     // WebFetch
    MemorySearch, // Auto-managed memory reads
    MemoryWrite,  // Auto-managed memory writes
    MCP           // MCP tool calls
};
```

**b) `ActivityDescription` verb pairs** (`ActivityDescription.cpp:19-32`):
```cpp
{"Read",      {"Reading",       "Read",          ...}},
{"Write",     {"Writing",       "Wrote",         ...}},
{"Edit",      {"Editing",       "Edited",        ...}},
{"Bash",      {"Running",       "Ran",           ...}},
{"Grep",      {"Searching for", "Searched for",  ...}},
{"Glob",      {"Finding",       "Found",         ...}},
{"WebFetch",  {"Fetching",      "Fetched",       ...}},
{"WebSearch", {"Searching",     "Searched",      ...}},
```

### 2.2 Phase-to-tool mapping (proposed)

| Preceding tool activity | Phase label | TS equivalent |
|---|---|---|
| Grep, Glob | `Searching` | `● Searching...` |
| Read, LS | `Reading` | `● Reading...` |
| Edit, Write | `Editing` | (TS may not have this) |
| Bash (test commands) | `Testing` | (context-dependent) |
| Bash (general) | `Running` | `● Running...` |
| WebSearch | `Searching web` | (N/A in TS) |
| WebFetch | `Fetching` | (N/A in TS) |
| Multiple tool kinds | Primary category label | — |
| No tools (first answer) | _(no label, raw text)_ | — |

### 2.3 When phase context IS available

The phase header detection loop (line 644-673) iterates all `contentBlocks_` AFTER pipeline processing. At the point it sets `isFirst=true`, it has already called `findPrevSignificant(i)` which returns the preceding significant block. For a phase header (where the preceding block is tool-like), this gives us access to:

```cpp
const ContentBlock* prev = findPrevSignificant(i);
// prev could be: ToolResult, ToolGroup, or CollapsedGroup
// For ToolGroup/CollapsedGroup: prev->toolUseIds gives us all tool IDs
// For ToolResult: prev->toolCallId gives us a single tool ID
// We can look up the tool name from the tool result blocks in contentBlocks_
```

However, looking up tool names from tool IDs requires scanning `contentBlocks_` again (O(n^2) worst case). A simpler approach: classify the phase from the tool block's `toolName` field (ToolResult, ToolGroup) or from the CollapsedGroup's children.

### 2.4 When phase context is NOT available

- First AnswerText in a turn (no preceding tools) — raw model text is the only signal
- AnswerText after another AnswerText (continuation) — by definition not a phase boundary
- AnswerText after AgentProgress — currently treated as tool-like, but agent type varies

## 3. Replace vs. Prefix: Design Decision

### Option A: Replace raw text with semantic label (TS-style)

```
● Searching...
  (model text hidden — only semantic label shown)
```

**Pros**: Maximally terse, matches TS behavior, consistent with collapsed group summaries.
**Cons**: Loses model's actual phrasing. Misleading if classification is wrong. Model may convey important context in phase-header text (e.g., "Let me check the failing tests" conveys more than "● Searching").

### Option B: Prefix label (badge) before raw text

```
● Searching  Let me look for the failing test pattern.
  (label is supplementary, model text preserved)
```

**Pros**: Model text preserved as authoritative. Label is a helpful hint. Less damaging if wrong.
**Cons**: Repetitive (model often says "Let me search for..." and label says "Searching"). Verbose.

### Option C: Semantic label only, with raw text as tooltip/secondary

```
● Searching  4 patterns in 12 files
  (label + machine-generated context; raw model text on expand/hover)
```

**Pros**: Clean. Quantitative (adds counts). Model-independent.
**Cons**: Requires significant new infrastructure (tooltip, expand). Over-engineered for P3b.

### Recommendation: Option B — Prefix badge

The raw model text is the authoritative description. A semantic badge adds value without risk of information loss. If the classification is wrong, the user still sees what the model intended. The badge is a hint, not a replacement.

Format: `● [Searching]  model text here...` or `● Searching — model text here...`

## 4. ContentBlock Field Impact

### Minimal approach: No new field

Compute the phase label in the renderer (`ContentBlockFtxui.cpp`) by passing preceding-block context. The renderer currently only receives the individual `ContentBlock` — it would need access to surrounding blocks.

**Problem**: The renderer's signature is `Element renderFtxuiElement(const ContentBlock& block, ...)`. Adding context requires an API change.

### Recommended approach: Optional String field

Add `String phaseLabel;` (empty by default) to `ContentBlock`. Set it during the phase header detection loop when `isFirst=true`.

**Pros**:
- Renderer reads it directly — no API change
- Set at the same time as `isFirst`, in the same loop, with the same context
- Empty string = no label (same as current behavior)
- Trivial serialization (empty string = no overhead)

**Cons**:
- New field on ContentBlock (minimal)
- Must be serialized/deserialized (but empty string is cheap)

### Fields that MUST NOT change

```
ContentBlock::type — unchanged
ContentBlock::text — unchanged (raw model text)
ContentBlock::isFirst — unchanged (still drives ● vs ⏺)
ContentBlock::dimmed — unchanged
MessagePipeline — unchanged
DisplayEvent — unchanged
API types — unchanged
```

Only addition: `String phaseLabel` on ContentBlock (default empty).

## 5. Transcript / API Impact

**If stored on ContentBlock**: The `phaseLabel` field would be serialized alongside other ContentBlock fields. However, since it's purely a presentation hint (not semantic content), it could be omitted from transcript serialization.

**Recommendation**: Exclude `phaseLabel` from transcript serialization. It's a UI rendering hint, not conversational content. Add a `[transient]` comment or skip it in `ContentBlockParam::fromContentBlock()` / `toJson()`.

## 6. Rendering Layer Scope

### Where the label is computed

In the phase header detection pass (`FtxuiRepl.cpp:644-673`), when `isFirst=true` is assigned. This pass already has access to `findPrevSignificant()` context and the full `contentBlocks_` array.

### Where the label is rendered

In `ContentBlockFtxui.cpp:180-184`, the prefix line changes from:
```cpp
auto prefix = block.isFirst ? " ● " : " ⏺ ";
```
to:
```cpp
auto prefix = block.isFirst
    ? (block.phaseLabel.empty() ? " ● " : " ● " + block.phaseLabel + " — ")
    : " ⏺ ";
```

### Non-touch zones

```
MessagePipeline — no change
DisplayEvent — no change
groupConsecutiveToolUses — no change
collapseReadSearchGroups — no change
dimToolNarration — no change
StatusBar / AppLayout — no change
CostTracker / TokenTracker — no change
Compact / cancel / Bash kill — no change
API / transcript — phaseLabel excluded from serialization
HeadlessContentBlockAccumulator — no change (headless doesn't use ● prefix)
```

## 7. Will Auto-Naming Mislead Users?

### Risk scenarios

| Scenario | Risk | Mitigation |
|---|---|---|
| Bash is "npm test" → labeled "Running" instead of "Testing" | Medium | Accept "Running" as generic; model text clarifies |
| Multiple tool types precede header → labeled by first/largest category | Low | Use dominant category; model text clarifies |
| Model writes substantive analysis after Read → labeled "Reading" | Low | Model text is visible alongside label |
| Model writes a real phase intro e.g. "Now let's analyze" → labeled "Reading" | Medium | Prefix is a badge, not a replacement |
| "Summary" phase after all tools — how to detect? | High | Defer to future work; difficult to detect reliably |

### Verdict

Risk is **low** with Option B (prefix badge). The model text remains visible and authoritative. A wrong label is a minor annoyance, not a misinformation event. The user can ignore it.

The "Summary" phase is the hardest to detect and should be deferred. It requires understanding that ALL tool activity is complete and the model is synthesizing — which is a semantic judgment, not a tool-proximity heuristic.

## 8. Approach Comparison

| | A: Replace text | B: Prefix badge (RECOMMENDED) | C: Quantitative label |
|---|---|---|---|
| Info loss risk | High | None | Medium |
| Misleading if wrong | High | Low | Medium |
| ContentBlock field | `phaseLabel` (replaces text) | `phaseLabel` (badge) | `phaseLabel` + counts |
| Renderer change | New prefix logic | New prefix logic | New prefix logic + stats |
| Complex to implement | Low | Low | Medium |
| Matches TS | Yes | No (C++ is better) | No |
| Summary detection | Required | Deferred | Required |
| Lines changed (est.) | ~30 | ~25 | ~45 |

## 9. Recommendation: Option B — Prefix Badge

### Phase label computation (in phase header detection loop)

When `isFirst=true` and `findPrevSignificant(i)` returns a tool-like block:

1. Determine the tool's Category (from `toolName` or `CollapsedGroup` children)
2. Map Category → phase label string:
   - `Search` → `"Searching"`
   - `FileRead` → `"Reading"`
   - `FileList` → `"Listing"`
   - `Bash` → `"Running"`
   - `WebSearch` → `"Searching web"`
   - `WebFetch` → `"Fetching"`
   - Multiple → primary (most-counted) category
3. Set `block.phaseLabel`
4. If no preceding tool (first answer), leave `phaseLabel` empty

### Edge case: the "Summary" phase

When the model has finished tool work and writes a summary/conclusion, it should ideally show "● Summary". Detection heuristics:
- Preceded by tool-like blocks AND
- Text contains summary keywords (already detected by `isPhaseHeaderEligible`)

But this is fragile — the model might write analysis (not summary) after tools. **Defer to P3c or later.**

### Format specification

```
Current:  ● Let me search for the error pattern.
After:    ● Searching — Let me search for the error pattern.

Current:  ● Here's a summary of the changes needed:
After:    ● Here's a summary of the changes needed:
          (no tool preceding, or "Summary" detection deferred → raw text)

Current:  ⏺ The main entry point is in main.cpp:42...
After:    ⏺ The main entry point is in main.cpp:42...
          (continuation — no change)
```

### What we do NOT do

- Do NOT remove or hide raw model text
- Do NOT add a new ContentBlock type
- Do NOT add a MessagePipeline pass
- Do NOT change DisplayEvent or streaming protocol
- Do NOT auto-detect "Summary" phase (deferred)
- Do NOT auto-detect "Testing" vs "Running" for Bash (ambiguous)
- Do NOT include phaseLabel in transcript serialization

## 10. Files to Modify

| File | Change | Lines |
|---|---|---|
| `include/claude/stream/ContentBlock.hpp` | Add `String phaseLabel;` field (empty default) | +1 |
| `src/ui/FtxuiRepl.cpp` | Phase label inference in detection loop (after `isFirst=true`) | ~15 |
| `src/ui/renderers/ContentBlockFtxui.cpp` | Render phase label as badge prefix | ~5 |
| `src/core/ContentBlockParam.cpp` | Exclude `phaseLabel` from serialization (if needed) | ~3 |
| `tests/stream/test_ContentBlock.cpp` | Phase label computation + rendering tests | ~30 |

### Non-touch zones (same as P3a)

```
CostTracker / TokenTracker — no change
StatusBar / AppLayout — no change
MessagePipeline — no change
ContentBlock type enum — no change
DisplayEvent / streaming — no change
Compact / cancel / Bash kill — no change
P2 narration classifier — no change
HeadlessContentBlockAccumulator — no change
```

## 11. Test Plan

| # | Test | Expected |
|---|---|---|
| 1 | Phase header after Grep → phaseLabel = "Searching" | label set |
| 2 | Phase header after Read → phaseLabel = "Reading" | label set |
| 3 | Phase header after CollapsedGroup (Read+Grep) → primary label | label set to dominant category |
| 4 | Phase header after Bash → phaseLabel = "Running" | label set |
| 5 | Phase header with no preceding tools → phaseLabel empty | empty string |
| 6 | Continuation AnswerText → phaseLabel empty | empty string |
| 7 | Rendering: ● Searching — model text | prefix + label + " — " + text |
| 8 | Rendering: ● model text (no label) | ● + text only (no dash) |
| 9 | Rendering: ⏺ model text (continuation) | ⏺ + text (unchanged) |
| 10 | Dimmed AnswerText → no phase label set | empty string |
| 11 | PhaseLabel excluded from transcript serialization | no phaseLabel in JSON |
| 12 | P2a eligibility regression | existing tests pass |
| 13 | P1d tool count regression | existing tests pass |
| 14 | P3a token display regression | existing tests pass |

## 12. Risks

| Risk | Likelihood | Severity | Mitigation |
|---|---|---|---|
| Wrong label for ambiguous Bash | Medium | Low | Model text visible; "Running" is generic and usually correct |
| PhaseLabel serialized to transcript accidentally | Low | Medium | Explicit exclusion in ContentBlockParam |
| Label computation O(n^2) from tool lookup | Low | Low | Max blocks is 2000; loop is bounded |
| User finds labels distracting | Low | Low | Empty label = no badge (graceful degradation) |
| "Summary" detection missing | Always | Low | Deferred; raw text is adequate |

## 13. Acceptance Criteria

```
[ ] phaseLabel field on ContentBlock (default empty)
[ ] Phase label inferred from preceding tool Category
[ ] "Searching" for Grep/Glob
[ ] "Reading" for Read/LS
[ ] "Running" for Bash
[ ] "Searching web" for WebSearch
[ ] "Fetching" for WebFetch
[ ] Empty for first-answer and continuations
[ ] Empty for dimmed/non-eligible blocks
[ ] Rendered as "● Label — text" when label present
[ ] Rendered as "● text" when no label (current behavior)
[ ] Rendered as "⏺ text" for continuations (unchanged)
[ ] phaseLabel excluded from transcript serialization
[ ] All P2a/P1d/P3a tests pass
[ ] ctest sequential + parallel all pass
```

## 14. Decision: DEFER / NO-OP

Phase auto-naming is technically feasible, but deferred. The current best implementation requires a new ContentBlock `phaseLabel` field and serialization exclusion for a low-impact cosmetic feature. The added schema surface and maintenance cost outweigh the benefit at this stage. Existing raw model phase headers with `●` are acceptable after P2a/P2b. Revisit only if users explicitly request semantic phase labels.

### Reasons for deferral

1. New ContentBlock field expands core data structure surface for a cosmetic feature
2. Serialization exclusion requires ongoing maintenance even though `phaseLabel` is transient
3. Prefix badge (`● Searching — Let me search for...`) is often redundant with model text
4. Wrong labels, though non-destructive, add cognitive noise
5. Most valuable labels ("Summary", "Testing") are the hardest to detect and would be deferred anyway
6. Post-P3a output experience is adequate; incremental benefit does not justify the cost

### What we do NOT do

- Do NOT add `ContentBlock::phaseLabel`
- Do NOT modify `ContentBlockParam` serialization
- Do NOT modify the renderer (`ContentBlockFtxui.cpp`)
- Do NOT modify `FtxuiRepl` phase detection
- Do NOT implement phase label inference
- Do NOT change API / transcript
- No runtime patches

### Deferred items

- "Summary" phase detection (requires semantic understanding of model intent)
- "Testing" vs "Running" Bash disambiguation (requires command inspection)
- All phase label inference and rendering

### Revisit criteria

Revisit P3b only if:
1. Users explicitly request semantic phase labels, OR
2. A simpler implementation becomes possible (e.g., model provides phase hints in output), OR
3. The ContentBlock schema already expands for other reasons and adding `phaseLabel` becomes near-zero marginal cost
