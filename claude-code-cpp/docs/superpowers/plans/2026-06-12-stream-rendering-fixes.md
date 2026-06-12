# Stream Rendering Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix output flicker (per-token UI redraws), thinking content flashing, and layout misalignment by correcting the StreamBuffer rendering pipeline to match the 2026-06-09 design spec.

**Architecture:** Six targeted fixes across 6 files. No architecture changes — only aligning implementation with already-approved design. Implementation order: B1 (TextPartial throttling) → B3 (thinking tag hardening) → B4 (AnswerPostProcessor wiring) → B5 (ToolProgress O(1)) → B6 (contentBlocks lifecycle).

**Tech Stack:** C++17, ftxui

---

### Task 1: Fix B1 — Eliminate per-token TextPartial emission

**Files:**
- Modify: `include/claude/stream/StreamBuffer.hpp`
- Modify: `src/stream/StreamBuffer.cpp`

**Background:** `StreamBuffer::feed(TextDelta)` currently emits `DisplayEvent::TextPartial` on every single SSE token. This triggers `screen_->Post()` (full terminal redraw) per token — a 500-token paragraph causes 500 redraws. The design spec defines TextPartial as a rare safety-net, not a per-token event.

- [ ] **Step 1: Add `lastEmittedPos_` member to StreamBuffer**

In `include/claude/stream/StreamBuffer.hpp`, add the new member inside the `private:` section, after the existing `lastThinkingEmit_` line:

```cpp
    // Thinking bypass
    String thinkingAccumulator_;
    std::chrono::steady_clock::time_point lastThinkingEmit_;
    static constexpr auto THINKING_MIN_INTERVAL = std::chrono::milliseconds(50);
    static constexpr size_t THINKING_MIN_CHARS = 256;
    size_t thinkingCharsSinceEmit_ = 0;

    // TextPartial delta tracking (Fix B1)
    size_t lastEmittedPos_ = 0;  // position in textAccumulator_ already sent via TextPartial
```

- [ ] **Step 2: Rewrite TextDelta handler in StreamBuffer::feed**

In `src/stream/StreamBuffer.cpp`, replace the TextDelta case in `feed(TypedStreamEvent&& event)`. Locate the `case StreamEventType::TextDelta:` block (approximately lines 48-156) and replace the core emit logic. The thinking tag stripping code remains unchanged; only the emit logic changes.

Find this pattern (after the thinking tag stripping, around line 144):
```cpp
            if (cleanText.empty()) break;

            textAccumulator_ += cleanText;
            emit(DisplayEvent{.type = DisplayEventType::TextPartial, .text = cleanText});
            if (blockParser_.append(cleanText)) {
                flushTextBuffer(false);
            } else if (textAccumulator_.size() >= FLUSH_THRESHOLD) {
                // Size-based flush: even without paragraph boundaries,
                // commit accumulated text periodically to avoid
                // dumping everything at StreamEnd.
                flushTextBuffer(false);
            }
            break;
```

Replace with:
```cpp
            if (cleanText.empty()) break;

            textAccumulator_ += cleanText;
            bool hasBoundary = blockParser_.append(cleanText);

            if (hasBoundary && blockParser_.lastBoundaryPos() > 0) {
                // Paragraph boundary detected: flush the complete paragraph.
                lastEmittedPos_ = 0;
                flushTextBuffer(false);
            } else if (textAccumulator_.size() >= FLUSH_THRESHOLD) {
                // Safety-net: long paragraph with no block boundary detected.
                // Emit ONLY the incremental delta since the last emit.
                // The UI (FtxuiRepl) APPENDS TextPartial to streamingText_,
                // so we must not re-send already-emitted text.
                String delta = textAccumulator_.substr(lastEmittedPos_);
                lastEmittedPos_ = textAccumulator_.size();
                if (!delta.empty()) {
                    emit(DisplayEvent{
                        .type = DisplayEventType::TextPartial,
                        .text = std::move(delta)
                    });
                }
                // Do NOT clear textAccumulator_ — the full paragraph is sent
                // as TextParagraph at the next boundary or StreamEnd.
            }
            break;
```

- [ ] **Step 3: Reset lastEmittedPos_ in flushTextBuffer**

In `src/stream/StreamBuffer.cpp`, in the `flushTextBuffer` method, add `lastEmittedPos_ = 0;` after the `textAccumulator_.clear();` line:

```cpp
void StreamBuffer::flushTextBuffer(bool isComplete) {
    if (textAccumulator_.empty()) return;
    emit(DisplayEvent{
        .type = DisplayEventType::TextParagraph,
        .text = std::move(textAccumulator_)
    });
    textAccumulator_.clear();
    lastEmittedPos_ = 0;     // Reset delta tracker
    blockParser_.reset();
}
```

- [ ] **Step 4: Build and verify compilation**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -20
```
Expected: Build succeeds with no errors.

- [ ] **Step 5: Run existing StreamBuffer tests**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && ctest -R StreamBuffer --output-on-failure
```
Expected: All existing StreamBuffer tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/claude/stream/StreamBuffer.hpp src/stream/StreamBuffer.cpp
git commit -m "fix: eliminate per-token TextPartial emission (B1)

TextPartial is now emitted only as a safety-net when the text buffer
exceeds FLUSH_THRESHOLD (256 chars) without a paragraph boundary.
Uses lastEmittedPos_ to track incremental delta, preventing duplicate
text in the UI. UI redraws drop from O(tokens) to O(paragraphs)."
```

---

### Task 2: Fix B3 — Enhanced thinking tag filtering

**Files:**
- Modify: `include/claude/stream/StreamBuffer.hpp`
- Modify: `src/stream/StreamBuffer.cpp`
- Modify: `src/ui/FtxuiRepl.cpp`

**Background:** Two gaps in thinking tag filtering: (1) tags split across SSE chunks (`"<thin"` + `"king>"`) aren't detected; (2) FtxuiRepl's secondary filter only handles standard `<think>`/`<thinking>` but not CJK fullwidth variants.

- [ ] **Step 1: Add `thinkingTagBuffer_` member**

In `include/claude/stream/StreamBuffer.hpp`, add the new member alongside `inThinkingTag_`:

```cpp
    // Internal state
    bool answerStarted_ = false;
    bool inThinkingTag_ = false;       // Track unclosed thinking tags across chunks
    String thinkingTagBuffer_;         // Buffer partial tag prefixes across chunks (Fix B3)
    std::chrono::steady_clock::time_point answerStartTime_;
```

- [ ] **Step 2: Implement enhanced stripThinkingTags with prefix buffering**

In `src/stream/StreamBuffer.cpp`, replace the existing `stripThinkingTags` free function (lines 10-35) with an enhanced version that handles cross-chunk partial tags. The function signature changes to accept the `thinkingTagBuffer_` by reference:

```cpp
// All known thinking tag patterns (open, close pairs)
static const std::vector<std::pair<String, String>> THINKING_TAG_PAIRS = {
    {"<thinking>", "</thinking>"},
    {"<think>", "</think>"},
    {"\xef\xbc\x9c" "thinking" "\xef\xbc\x9e", "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e"},
    {"\xef\xbc\x9c" "think" "\xef\xbc\x9e", "\xef\xbc\x9c" "/think" "\xef\xbc\x9e"},
    {"&lt;thinking&gt;", "&lt;/thinking&gt;"},
    {"&lt;think&gt;", "&lt;/think&gt;"},
};

// Check if `prefix` is a valid prefix of any known tag (open or close).
// Returns true if the prefix could complete to a tag with more characters.
static bool isTagPrefix(const String& prefix) {
    if (prefix.empty()) return false;
    for (const auto& [openTag, closeTag] : THINKING_TAG_PAIRS) {
        if (openTag.size() >= prefix.size() && openTag.compare(0, prefix.size(), prefix) == 0)
            return true;
        if (closeTag.size() >= prefix.size() && closeTag.compare(0, prefix.size(), prefix) == 0)
            return true;
    }
    return false;
}

// Enhanced strip: handles cross-chunk partial tags via tagBuffer.
// On entry, tagBuffer may contain a partial prefix from the previous chunk.
// On exit, any trailing partial prefix is saved back to tagBuffer.
static String stripThinkingTagsEnhanced(const String& text, bool& inTag, String& tagBuffer) {
    String result;
    String working = tagBuffer + text;
    tagBuffer.clear();

    size_t i = 0;
    while (i < working.size()) {
        if (inTag) {
            // Inside a thinking tag: scan for ANY close tag
            bool foundClose = false;
            for (const auto& [openTag, closeTag] : THINKING_TAG_PAIRS) {
                (void)openTag; // only close tag matters here
                if (working.compare(i, closeTag.size(), closeTag) == 0) {
                    inTag = false;
                    i += closeTag.size();
                    foundClose = true;
                    break;
                }
            }
            if (!foundClose) {
                // Still inside tag, discard this character
                ++i;
            }
        } else {
            // Outside tags: check for ANY open tag
            bool foundOpen = false;
            for (const auto& [openTag, closeTag] : THINKING_TAG_PAIRS) {
                (void)closeTag;
                if (working.compare(i, openTag.size(), openTag) == 0) {
                    inTag = true;
                    i += openTag.size();
                    foundOpen = true;
                    break;
                }
            }
            if (!foundOpen) {
                // Not a tag: keep this character. But first check if it starts
                // a partial prefix that spans to the next chunk.
                if (working[i] == '<' || working[i] == '\xef' || working[i] == '&') {
                    // Peek ahead: is there a partial tag prefix ending at this chunk boundary?
                    // Only buffer if we're near the end and the remainder looks like a tag prefix.
                    size_t remaining = working.size() - i;
                    String suffix = working.substr(i, std::min(remaining, size_t(20)));
                    if (isTagPrefix(suffix) && remaining < 20) {
                        // Might be a cross-chunk tag: save and check next chunk
                        tagBuffer = suffix;
                        break; // exit loop, will prepend on next call
                    }
                }
                result += working[i];
                ++i;
            }
        }
    }
    return result;
}
```

- [ ] **Step 3: Wire enhanced strip into TextDelta handler**

In `src/stream/StreamBuffer.cpp`, in the `feed(TypedStreamEvent&&)` TextDelta handler, replace the inline thinking tag logic (the existing `inThinkingTag_` detection and `stripThinkingTags` calls within the TextDelta case) with a call to the enhanced function. Specifically, after extracting `cleanText` from `event.text`, apply:

```cpp
// In the TextDelta handler, after String cleanText = event.text;
cleanText = stripThinkingTagsEnhanced(cleanText, inThinkingTag_, thinkingTagBuffer_);
```

Also update the ThinkingDelta handler to use the enhanced function:
```cpp
case StreamEventType::ThinkingDelta: {
    String clean = stripThinkingTagsEnhanced(event.text, inThinkingTag_, thinkingTagBuffer_);
    if (!clean.empty()) {
        thinkingAccumulator_ += clean;
        thinkingCharsSinceEmit_ += clean.size();
    }
    maybeEmitThinkingUpdate(false);
    break;
}
```

Remove the old `stripThinkingTags` free function (the one at lines 10-35) since it's replaced by `stripThinkingTagsEnhanced`.

- [ ] **Step 4: Add fullwidth variants to FtxuiRepl secondary filter**

In `src/ui/FtxuiRepl.cpp`, in `handleDisplayEvent`, the `TextPartial` case (around line 30), expand the `residualTags` list to include CJK fullwidth and HTML-entity variants. The current code has:

```cpp
static const std::vector<std::pair<String, String>> residualTags = {
    {"<think>", "</think>"},
    {"<thinking>", "</thinking>"},
};
```

Replace with:

```cpp
static const std::vector<std::pair<String, String>> residualTags = {
    {"<think>", "</think>"},
    {"<thinking>", "</thinking>"},
    // CJK fullwidth angle bracket variants (UTF-8 encoded)
    {"\xef\xbc\x9c" "think" "\xef\xbc\x9e",
     "\xef\xbc\x9c" "/think" "\xef\xbc\x9e"},
    {"\xef\xbc\x9c" "thinking" "\xef\xbc\x9e",
     "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e"},
    // HTML-entity encoded variants
    {"&lt;think&gt;", "&lt;/think&gt;"},
    {"&lt;thinking&gt;", "&lt;/thinking&gt;"},
};
```

- [ ] **Step 5: Build and run existing tests**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -20
```
Expected: Build succeeds.

- [ ] **Step 6: Write unit test for cross-chunk tag detection**

Create file `tests/stream/test_thinking_tag_filter.cpp`:

```cpp
#include <gtest/gtest.h>
#include "claude/stream/StreamBuffer.hpp"

namespace claude {
namespace {

// Expose the enhanced strip function for testing
// (either via a test-only header or by making it a public static method)

TEST(ThinkingTagFilter, StripsStandardTags) {
    bool inTag = false;
    String tagBuf;
    // Access stripThinkingTagsEnhanced via test helper
    auto result = stripThinkingTagsEnhanced("Hello <thinking>secret</thinking> world", inTag, tagBuf);
    EXPECT_EQ(result, "Hello  world");
    EXPECT_FALSE(inTag);
}

TEST(ThinkingTagFilter, HandlesCrossChunkSplit) {
    bool inTag = false;
    String tagBuf;

    // First chunk: ends with partial open tag
    auto r1 = stripThinkingTagsEnhanced("text <thin", inTag, tagBuf);
    EXPECT_EQ(r1, "text ");
    EXPECT_FALSE(inTag);
    EXPECT_EQ(tagBuf, "<thin");

    // Second chunk: completes the tag
    auto r2 = stripThinkingTagsEnhanced("king>secret</thinking> more", inTag, tagBuf);
    EXPECT_EQ(r2, " more");
    EXPECT_FALSE(inTag);
    EXPECT_TRUE(tagBuf.empty());
}

TEST(ThinkingTagFilter, HandlesFullwidthTags) {
    bool inTag = false;
    String tagBuf;
    // CJK fullwidth: ＜thinking＞...＜/thinking＞
    String fullOpen = "\xef\xbc\x9c" "thinking" "\xef\xbc\x9e";
    String fullClose = "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e";
    auto result = stripThinkingTagsEnhanced("before " + fullOpen + "secret" + fullClose + " after", inTag, tagBuf);
    EXPECT_EQ(result, "before  after");
}

TEST(ThinkingTagFilter, HandlesHtmlEntityTags) {
    bool inTag = false;
    String tagBuf;
    auto result = stripThinkingTagsEnhanced("a &lt;think&gt;x&lt;/think&gt; b", inTag, tagBuf);
    EXPECT_EQ(result, "a  b");
}

} // namespace
} // namespace claude
```

- [ ] **Step 7: Add test to CMakeLists and run**

Add the test file to `tests/CMakeLists.txt`:
```cmake
add_executable(test_thinking_tag_filter
    stream/test_thinking_tag_filter.cpp
    ${STREAM_SOURCES}
)
target_link_libraries(test_thinking_tag_filter gtest_main pthread)
add_test(NAME ThinkingTagFilter COMMAND test_thinking_tag_filter)
```

Then build and run:
```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && cmake .. && make test_thinking_tag_filter && ./test_thinking_tag_filter
```
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add include/claude/stream/StreamBuffer.hpp src/stream/StreamBuffer.cpp src/ui/FtxuiRepl.cpp tests/stream/test_thinking_tag_filter.cpp tests/CMakeLists.txt
git commit -m "fix: enhance thinking tag filtering for cross-chunk and fullwidth variants (B3)

- Replaced stripThinkingTags with state-machine-based stripThinkingTagsEnhanced
  that buffers partial tag prefixes across SSE chunks
- Added CJK fullwidth (＜thinking＞) and HTML-entity (&lt;think&gt;) variants
  to both StreamBuffer primary filter and FtxuiRepl secondary filter
- Added unit tests for cross-chunk split, fullwidth, and HTML-entity tags"
```

---

### Task 3: Fix B4 — Wire AnswerPostProcessor into pipeline

**Files:**
- Modify: `src/bootstrap/AgentRunner.cpp`

**Background:** `AnswerPostProcessor` is compiled but never connected between `StreamBuffer` and `FtxuiRepl`. The StreamBuffer callback is set up in `setupCallbacks()` at line 359 of `AgentRunner.cpp`. Tool grouping and reordering never execute.

**Warning:** The current `FtxuiRepl::handleDisplayEvent(AnswerEnd)` (around line 204) calls `runMessagePipeline()` which duplicates some grouping logic. After B4, we disable that internal pipeline via `useExternalPostProcessor_` flag (added in Task 4 Step 4, but the flag check must be in place first or added here).

- [ ] **Step 1: Add AnswerPostProcessor include**

At the top of `src/bootstrap/AgentRunner.cpp` (around line 8 where other stream includes are), add:

```cpp
#include <claude/stream/AnswerPostProcessor.hpp>
```

- [ ] **Step 2: Insert AnswerPostProcessor in the FTXUI callback path**

In `src/bootstrap/AgentRunner.cpp`, locate the `setDisplayCallback` call at line 359. The current code structure is:

```cpp
    auto streamBuffer = std::make_shared<StreamBuffer>();

    streamBuffer->setDisplayCallback(
        [useFtxui, ftxuiRepl](DisplayEvent&& event) {
            if (useFtxui && ftxuiRepl) {
                // FTXUI: handleDisplayEvent posts to UI thread internally
                ftxuiRepl->handleDisplayEvent(std::move(event));
            } else {
                // ANSI mode: ... (unchanged)
            }
        });
```

Replace the FTXUI path (lines 361-363) to insert AnswerPostProcessor:

```cpp
    auto streamBuffer = std::make_shared<StreamBuffer>();
    auto postProcessor = std::make_shared<AnswerPostProcessor>();  // B4

    streamBuffer->setDisplayCallback(
        [useFtxui, ftxuiRepl, postProcessor](DisplayEvent&& event) {
            if (useFtxui && ftxuiRepl) {
                // B4: Route through AnswerPostProcessor for tool grouping/reordering
                if (event.type == DisplayEventType::AnswerEnd) {
                    // Phase 1: process the AnswerEnd event itself
                    auto proc = postProcessor->process(std::move(event));
                    ftxuiRepl->handleDisplayEvent(std::move(proc));
                    // Phase 2: finalize — group tools, reorder traces, emit tombstones
                    auto finalEvents = postProcessor->finalize();
                    for (auto& fe : finalEvents) {
                        ftxuiRepl->handleDisplayEvent(std::move(fe));
                    }
                    postProcessor->reset();
                } else {
                    auto proc = postProcessor->process(std::move(event));
                    ftxuiRepl->handleDisplayEvent(std::move(proc));
                }
            } else {
                // ANSI mode: ... (unchanged)
            }
        });
```

**Note:** The ANSI path is left unchanged for now. ANSI mode could also benefit from post-processing but is out of scope for this fix.
```

---

### Task 4: Fix B5 — ToolProgress O(1) cleanup with orphan guard

**Files:**
- Modify: `include/claude/ui/FtxuiRepl.hpp`
- Modify: `src/ui/FtxuiRepl.cpp`

- [ ] **Step 1: Add toolProgressIndices_ and useExternalPostProcessor_ members**

In `include/claude/ui/FtxuiRepl.hpp`, in the `private:` section near the other new pipeline members, add:

```cpp
    // ToolProgress index tracking (Fix B5) — O(1) lookup for ToolResult→ToolProgress replacement
    std::map<String, size_t> toolProgressIndices_;  // toolCallId -> index in contentBlocks_

    // External post-processor flag (Fix B4) — set true when AnswerPostProcessor handles grouping
    bool useExternalPostProcessor_ = false;
```

- [ ] **Step 2: Update ToolProgress handler to record index**

In `src/ui/FtxuiRepl.cpp`, in `handleDisplayEvent`, the `DisplayEventType::ToolProgress` case. After the `contentBlocks_.push_back(std::move(cb))` line, add:

```cpp
            case DisplayEventType::ToolProgress: {
                // ... existing code to commit streaming text ...
                ContentBlock cb;
                cb.type = ContentBlock::ToolProgress;
                cb.toolName = std::move(ev.toolName);
                cb.activity = std::move(ev.activity);
                cb.toolCallId = ev.toolCallId;
                toolProgressIndices_[ev.toolCallId] = contentBlocks_.size();  // B5: record index
                contentBlocks_.push_back(std::move(cb));
                break;
            }
```

- [ ] **Step 3: Update ToolResult handler to use O(1) removal**

In `src/ui/FtxuiRepl.cpp`, replace the existing `std::remove_if` based ToolProgress removal in the `DisplayEventType::ToolResult` case (around line 122-129) with map-based O(1) lookup:

```cpp
            case DisplayEventType::ToolResult: {
                // ... existing code to commit streaming text ...

                // B5: O(1) ToolProgress removal using index map
                String callId = ev.toolCallId;
                if (!callId.empty()) {
                    auto it = toolProgressIndices_.find(callId);
                    if (it != toolProgressIndices_.end()) {
                        size_t idx = it->second;
                        if (idx < contentBlocks_.size() &&
                            contentBlocks_[idx].type == ContentBlock::ToolProgress &&
                            contentBlocks_[idx].toolCallId == callId) {
                            contentBlocks_.erase(contentBlocks_.begin() + static_cast<long>(idx));
                            // Shift all indices after the removed position
                            for (auto& [tid, i] : toolProgressIndices_) {
                                if (i > idx) --i;
                            }
                        }
                        toolProgressIndices_.erase(it);
                    }
                }

                // ... existing code to create ContentBlock for ToolResult and push_back ...
                break;
            }
```

- [ ] **Step 4: Add orphan ToolProgress guard in AnswerEnd + skip duplicate pipeline**

In `src/ui/FtxuiRepl.cpp`, in the `DisplayEventType::AnswerEnd` case, make two changes:

**(a)** Skip `runMessagePipeline()` when external post-processor is active. Find the line `runMessagePipeline();` (around line 204) and guard it:

```cpp
                // Only run internal pipeline if no external post-processor is active (B4)
                if (!useExternalPostProcessor_) {
                    runMessagePipeline();
                }
```

**(b)** BEFORE the thinking block and turn duration insertion, add orphan cleanup:

```cpp
            case DisplayEventType::AnswerEnd: {
                // ... existing streaming text commit ...

                // B5: Clean up orphaned ToolProgress blocks (tools that never completed)
                for (auto& [callId, idx] : toolProgressIndices_) {
                    if (idx < contentBlocks_.size() &&
                        contentBlocks_[idx].type == ContentBlock::ToolProgress) {
                        contentBlocks_[idx].type = ContentBlock::ToolResult;
                        contentBlocks_[idx].summary = ToolResultSummary::dim("Interrupted");
                        contentBlocks_[idx].resultStatus = ToolResultStatus::Cancelled;
                    }
                }
                toolProgressIndices_.clear();

                // ... existing runMessagePipeline / thinking / turn duration ...
                break;
            }
```

- [ ] **Step 5: Set useExternalPostProcessor_ in AnswerStart, clear indices**

In `src/ui/FtxuiRepl.cpp`, in the `DisplayEventType::AnswerStart` case, add:

```cpp
            case DisplayEventType::AnswerStart:
                // ... existing code ...
                useExternalPostProcessor_ = true;  // B4: external post-processor handles grouping
                toolProgressIndices_.clear();        // B5: fresh indices for new turn
                break;
```

- [ ] **Step 6: Build and verify**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -20
```
Expected: Build succeeds.

- [ ] **Step 7: Commit**

```bash
git add include/claude/ui/FtxuiRepl.hpp src/ui/FtxuiRepl.cpp
git commit -m "fix: O(1) ToolProgress cleanup with orphan guard (B5)

- Added toolProgressIndices_ map for O(1) ToolProgress→ToolResult lookup
- Replaced std::remove_if (O(n) per removal) with direct index-based erase
- Added orphan guard: remaining ToolProgress blocks at AnswerEnd are
  converted to ToolResult('Interrupted') with Cancelled status
- Indices cleared on AnswerStart for fresh turn state"
```

---

### Task 5: Fix B6 — contentBlocks_ lifecycle with MAX_BLOCKS cap + layout alignment

**Files:**
- Modify: `src/ui/FtxuiRepl.cpp`
- Modify: `src/ui/renderers/ContentBlockFtxui.cpp`

- [ ] **Step 1: Add turn boundary members to FtxuiRepl**

In `include/claude/ui/FtxuiRepl.hpp`, in the `private:` section, add:

```cpp
    // Turn boundary tracking (Fix B6)
    size_t currentTurnStartIndex_ = 0;
    std::vector<size_t> turnBoundaries_;
    static constexpr size_t MAX_BLOCKS = 2000;
```

- [ ] **Step 2: Replace contentBlocks_.clear() with boundary marking**

In `src/ui/FtxuiRepl.cpp`, in the `DisplayEventType::AnswerStart` handler, replace:
```cpp
contentBlocks_.clear();
```
with:
```cpp
currentTurnStartIndex_ = contentBlocks_.size();  // B6: mark turn boundary instead of clearing
```

This preserves scrollback history across turns.

- [ ] **Step 3: Add MAX_BLOCKS cap in AnswerEnd**

In `src/ui/FtxuiRepl.cpp`, in the `DisplayEventType::AnswerEnd` handler, after all turn-end processing (after the turn duration block insertion), add:

```cpp
                // B6: Record turn boundary
                turnBoundaries_.push_back(contentBlocks_.size());

                // B6: Hard cap — trim oldest blocks if exceeding MAX_BLOCKS
                if (contentBlocks_.size() > MAX_BLOCKS) {
                    size_t toRemove = contentBlocks_.size() - MAX_BLOCKS / 2;
                    contentBlocks_.erase(
                        contentBlocks_.begin(),
                        contentBlocks_.begin() + static_cast<long>(toRemove));
                    // Adjust turn boundaries
                    for (auto& b : turnBoundaries_) {
                        b = (b > toRemove) ? (b - toRemove) : 0;
                    }
                    currentTurnStartIndex_ = (currentTurnStartIndex_ > toRemove)
                        ? (currentTurnStartIndex_ - toRemove) : 0;
                }
```

- [ ] **Step 4: Fix thinking icon alignment in ContentBlockFtxui**

In `src/ui/renderers/ContentBlockFtxui.cpp`, in the `ThinkingBlock` case (around line 134), change the icon from `◈` to `∴` to match the TS original:

Find:
```cpp
        case ContentBlock::ThinkingBlock:
            if (block.expanded) {
                Elements els;
                els.push_back(hbox({
                    text("  ◈ ") | color(MacLavender),
                    text("Thinking...") | dim | color(MacLavender),
                }));
                // ...
                els.push_back(hbox({
                    text("  ◈ ") | color(MacLavender),
                    text("(collapsed)") | dim | color(MacLavender),
                }));
                // ...
            }
            return hbox({
                text("  ◈ ") | color(MacLavender),
                text("Thinking") | dim | color(MacLavender),
            });
```

Replace all three occurrences of `"  ◈ "`  with `"  ∴ "`:

```cpp
        case ContentBlock::ThinkingBlock:
            if (block.expanded) {
                Elements els;
                els.push_back(hbox({
                    text("  ∴ ") | color(MacLavender),
                    text("Thinking...") | dim | color(MacLavender),
                }));
                // ... (detail rendering unchanged) ...
                els.push_back(hbox({
                    text("  ∴ ") | color(MacLavender),
                    text("(collapsed)") | dim | color(MacLavender),
                }));
                return vbox(std::move(els));
            }
            return hbox({
                text("  ∴ ") | color(MacLavender),
                text("Thinking") | dim | color(MacLavender),
            });
```

- [ ] **Step 5: Add ThinkingBlock render-time guard**

In the same `ThinkingBlock` case in `ContentBlockFtxui.cpp`, add a guard at the top of the case to ensure thinking text is never rendered in collapsed mode:

```cpp
        case ContentBlock::ThinkingBlock: {
            // Guard: never render raw thinking text when collapsed
            // (detailText is only shown when user explicitly expands via Ctrl+O)
            if (!block.expanded) {
                return hbox({
                    text("  ∴ ") | color(MacLavender),
                    text("Thinking") | dim | color(MacLavender),
                });
            }
            // Expanded: user explicitly toggled, show detail
            Elements els;
            els.push_back(hbox({
                text("  ∴ ") | color(MacLavender),
                text("Thinking...") | dim | color(MacLavender),
            }));
            // ... (existing detail rendering) ...
            return vbox(std::move(els));
        }
```

- [ ] **Step 6: Build and verify**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -20
```
Expected: Build succeeds.

- [ ] **Step 7: Run full test suite**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && ctest --output-on-failure 2>&1 | tail -30
```
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add include/claude/ui/FtxuiRepl.hpp src/ui/FtxuiRepl.cpp src/ui/renderers/ContentBlockFtxui.cpp
git commit -m "fix: contentBlocks lifecycle management and layout alignment (B6)

- Replaced contentBlocks_.clear() with turn boundary tracking, preserving
  scrollback history across turns
- Added MAX_BLOCKS=2000 hard cap with oldest-block trimming at AnswerEnd
- Changed thinking icon from ◈ to ∴ to match TS original
- Added ThinkingBlock render-time guard: detailText never rendered when collapsed"
```

---

### Task 6: Integration verification

- [ ] **Step 1: Run StreamBuffer unit tests**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && ctest -R StreamBuffer --output-on-failure
```
Expected: All pass.

- [ ] **Step 2: Run AnswerPostProcessor unit tests**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && ctest -R AnswerPostProcessor --output-on-failure
```
Expected: All pass.

- [ ] **Step 3: Run thinking tag filter tests**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && ctest -R ThinkingTagFilter --output-on-failure
```
Expected: All pass.

- [ ] **Step 4: Run full test suite**

```bash
cd /Users/kankan/claude-code/claude-code-cpp/build-test && ctest --output-on-failure
```
Expected: All existing tests pass, no regressions.

- [ ] **Step 5: Manual smoke test**

```bash
cd /Users/kankan/claude-code/claude-code-cpp && make build
./build/claude -p "Read CMakeLists.txt and search for CXX" --max-turns 1
```

Expected behaviors:
- Text output appears in smooth paragraph chunks (not per-token flicker)
- No raw `<thinking>` or `<think>` tags visible in output
- Thinking (if model emits it) appears as collapsed "∴ Thinking"
- Tool results appear after answer text
- Consecutive Read/Grep results appear grouped (e.g., "Read 1 file, Grep 1 time")
- No orphaned "● Running..." blocks
- Turn duration shows time, tokens, cost

- [ ] **Step 6: Final commit (if any test fixes needed)**

```bash
git add -A
git commit -m "test: integration verification for stream rendering fixes"
```

---

## Summary of Changes

| File | Task | Changes |
|------|------|---------|
| `include/claude/stream/StreamBuffer.hpp` | T1, T2 | +`lastEmittedPos_`, +`thinkingTagBuffer_` |
| `src/stream/StreamBuffer.cpp` | T1, T2 | Rewrite TextDelta emit logic; enhanced `stripThinkingTagsEnhanced` |
| `include/claude/ui/FtxuiRepl.hpp` | T4, T5 | +`toolProgressIndices_`, +`currentTurnStartIndex_`, +`turnBoundaries_`, +`MAX_BLOCKS`, +`useExternalPostProcessor_` |
| `src/ui/FtxuiRepl.cpp` | T2, T3, T4, T5 | Fullwidth variant filter; O(1) tool progress cleanup; turn boundary tracking; MAX_BLOCKS cap; skip internal pipeline when external post-processor active |
| `src/ui/renderers/ContentBlockFtxui.cpp` | T5 | ◈→∴ icon; ThinkingBlock render guard |
| `src/bootstrap/AgentRunner.cpp` | T3 | Wire AnswerPostProcessor into callback chain |
| `tests/stream/test_thinking_tag_filter.cpp` | T2 | NEW: cross-chunk and variant tag tests |
