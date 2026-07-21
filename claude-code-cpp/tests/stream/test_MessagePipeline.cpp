#include <catch2/catch_test_macros.hpp>
#include "claude/stream/MessagePipeline.hpp"

using namespace claude;

TEST_CASE("Stable IDs are assigned to pipeline-created blocks", "[MessagePipeline][stableId]") {
    std::vector<ContentBlock> blocks;

    ContentBlock b1;
    b1.type = ContentBlock::ToolResult;
    b1.toolName = "Read";
    b1.toolCallId = "toolu_001";
    b1.summary = ToolResultSummary::success("Read 42 lines");
    blocks.push_back(b1);

    ContentBlock b2;
    b2.type = ContentBlock::ToolResult;
    b2.toolName = "Read";
    b2.toolCallId = "toolu_002";
    b2.summary = ToolResultSummary::success("Read 15 lines");
    blocks.push_back(b2);

    ContentBlock b3;
    b3.type = ContentBlock::ToolResult;
    b3.toolName = "Grep";
    b3.toolCallId = "toolu_003";
    b3.summary = ToolResultSummary::success("Found 5 matches");
    blocks.push_back(b3);

    MessagePipeline pipeline;

    // process() should assign stableIds to pipeline-created blocks
    auto result = pipeline.process(blocks);

    // ALL blocks get IDs in post-processing
    bool foundWithId = false;
    for (auto& b : result) {
        REQUIRE(b.stableId != 0);
        foundWithId = true;
    }
    REQUIRE(foundWithId);
}

TEST_CASE("Stable IDs from FtxuiRepl are preserved by pipeline", "[MessagePipeline][stableId]") {
    std::vector<ContentBlock> blocks;

    // Use an ErrorMessage block which is never collapsible/grouppable
    ContentBlock b1;
    b1.type = ContentBlock::ErrorMessage;
    b1.text = "Something went wrong";
    b1.stableId = 100;  // Already assigned by FtxuiRepl
    blocks.push_back(b1);

    MessagePipeline pipeline;

    auto result = pipeline.process(blocks);

    // Pre-assigned ID should be preserved (not overwritten to 1)
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].stableId == 100);
}

TEST_CASE("Incremental pipeline preserves all blocks", "[MessagePipeline][incremental]") {
    // The anchor-based incremental approach re-processes the tail on each
    // new ToolResult/ToolGroup arrival. Because collapseReadSearchGroups
    // eagerly wraps even single items, incremental and batch paths can
    // produce different grouping structures. This test validates core
    // correctness: no blocks are lost, and stableIds are preserved.

    uint64_t nextId = 1;
    auto makeBlock = [&nextId](ContentBlock::Type type, const String& text) {
        ContentBlock b;
        b.type = type;
        b.text = text;
        b.stableId = nextId++;
        return b;
    };
    auto makeRead = [&nextId](const String& tcId, const String& text) {
        ContentBlock b;
        b.type = ContentBlock::ToolResult;
        b.toolName = "Read";
        b.toolCallId = tcId;
        b.summary = ToolResultSummary::success(text);
        b.stableId = nextId++;
        return b;
    };

    // Build a realistic mid-turn scenario: AnswerText, then ToolResults arrive
    std::vector<ContentBlock> allBlocks;
    allBlocks.push_back(makeBlock(ContentBlock::AnswerText, "Let me check those files."));
    allBlocks.push_back(makeRead("toolu_001", "42 lines"));
    allBlocks.push_back(makeRead("toolu_002", "15 lines"));
    allBlocks.push_back(makeBlock(ContentBlock::AnswerText, "Now let me search."));
    allBlocks.push_back(makeRead("toolu_003", "8 lines"));

    std::vector<ContentBlock> batchCopy = allBlocks;
    MessagePipeline pipeline;
    auto batchResult = pipeline.process(batchCopy);

    // Count total blocks INCLUDING children of grouped blocks (leaf-level count)
    std::function<size_t(const std::vector<ContentBlock>&)> countLeaves;
    countLeaves = [&](const std::vector<ContentBlock>& blocks) -> size_t {
        size_t n = 0;
        for (auto& b : blocks) {
            if (!b.children.empty()) {
                n += countLeaves(b.children);
            } else {
                n++;
            }
        }
        return n;
    };

    size_t batchLeaves = countLeaves(batchResult);

    // Incremental: simulate streaming by feeding one at a time
    std::vector<ContentBlock> incremental;
    size_t stableIdx = 0;

    for (size_t i = 0; i < allBlocks.size(); i++) {
        // deep-copy since allBlocks was consumed
        ContentBlock copy;
        copy.type = allBlocks[i].type;
        copy.toolName = allBlocks[i].toolName;
        copy.toolCallId = allBlocks[i].toolCallId;
        copy.summary = allBlocks[i].summary;
        copy.text = allBlocks[i].text;
        copy.stableId = allBlocks[i].stableId;
        incremental.push_back(std::move(copy));

        // Process only [stableIdx, end) — simulating anchor-based incremental
        std::vector<ContentBlock> tail(incremental.begin() + stableIdx, incremental.end());
        auto processed = pipeline.process(std::move(tail));
        incremental.resize(stableIdx);
        for (auto& b : processed) incremental.push_back(std::move(b));
    }

    // Final full pass (simulating AnswerEnd)
    incremental = pipeline.process(incremental);

    size_t incrLeaves = countLeaves(incremental);

    // Core invariant: no leaf blocks are lost through incremental processing
    REQUIRE(batchLeaves > 0);
    REQUIRE(incrLeaves == batchLeaves);

    // Also verify all blocks in the incremental result have valid stableIds
    std::function<void(const std::vector<ContentBlock>&)> checkIds;
    checkIds = [&](const std::vector<ContentBlock>& blocks) {
        for (auto& b : blocks) {
            REQUIRE(b.stableId != 0);
            if (!b.children.empty()) checkIds(b.children);
        }
    };
    checkIds(incremental);
}

TEST_CASE("groupToolResultPairs wraps ToolProgress+ToolResult into ToolGroup", "[MessagePipeline][pairing]") {
    std::vector<ContentBlock> blocks;

    ContentBlock progress;
    progress.type = ContentBlock::ToolProgress;
    progress.toolCallId = "toolu_001";
    progress.toolName = "Read";
    progress.activity = "Reading src/main.cpp";

    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolCallId = "toolu_001";
    result.toolName = "Read";
    result.summary = ToolResultSummary::success("42 lines", true);

    blocks.push_back(progress);
    blocks.push_back(result);

    MessagePipeline pipeline;
    auto processed = pipeline.groupToolResultPairs(blocks);

    REQUIRE(processed.size() == 1);
    REQUIRE(processed[0].type == ContentBlock::ToolGroup);
    REQUIRE(processed[0].toolName == "Read");
    REQUIRE(processed[0].children.size() == 2);
    REQUIRE(processed[0].children[0].type == ContentBlock::ToolProgress);
    REQUIRE(processed[0].children[1].type == ContentBlock::ToolResult);
    REQUIRE(processed[0].summary.primaryText == "42 lines");
}

TEST_CASE("groupToolResultPairs does not pair non-adjacent blocks", "[MessagePipeline][pairing]") {
    std::vector<ContentBlock> blocks;

    ContentBlock progress;
    progress.type = ContentBlock::ToolProgress;
    progress.toolCallId = "toolu_001";
    progress.toolName = "Read";

    ContentBlock text;
    text.type = ContentBlock::AnswerText;
    text.text = "Let me read that file...";

    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolCallId = "toolu_001";
    result.toolName = "Read";
    result.summary = ToolResultSummary::success("42 lines");

    blocks.push_back(progress);
    blocks.push_back(text);   // interleaving text breaks adjacency
    blocks.push_back(result);

    MessagePipeline pipeline;
    auto processed = pipeline.groupToolResultPairs(blocks);

    // All three blocks should pass through unchanged
    REQUIRE(processed.size() == 3);
    REQUIRE(processed[0].type == ContentBlock::ToolProgress);
    REQUIRE(processed[1].type == ContentBlock::AnswerText);
    REQUIRE(processed[2].type == ContentBlock::ToolResult);
}

TEST_CASE("groupToolResultPairs does not pair mismatched toolCallIds", "[MessagePipeline][pairing]") {
    std::vector<ContentBlock> blocks;

    ContentBlock progress;
    progress.type = ContentBlock::ToolProgress;
    progress.toolCallId = "toolu_001";
    progress.toolName = "Read";

    ContentBlock result;
    result.type = ContentBlock::ToolResult;
    result.toolCallId = "toolu_002";  // different ID
    result.toolName = "Grep";
    result.summary = ToolResultSummary::success("Found 5 matches");

    blocks.push_back(progress);
    blocks.push_back(result);

    MessagePipeline pipeline;
    auto processed = pipeline.groupToolResultPairs(blocks);

    REQUIRE(processed.size() == 2);
    REQUIRE(processed[0].type == ContentBlock::ToolProgress);
    REQUIRE(processed[1].type == ContentBlock::ToolResult);
}

TEST_CASE("collapseReadSearchGroups separates Read and Grep into different groups", "[MessagePipeline][groupkind]") {
    std::vector<ContentBlock> blocks;
    uint64_t nextId = 1;

    auto makeBlock = [&nextId](const String& toolName, const String& tcId, const String& summaryText) {
        ContentBlock b;
        b.type = ContentBlock::ToolResult;
        b.toolName = toolName;
        b.toolCallId = tcId;
        b.summary = ToolResultSummary::success(summaryText);
        b.resultStatus = ToolResultStatus::Success;
        b.stableId = nextId++;
        return b;
    };

    // Interleave Read and Grep — should produce separate groups
    blocks.push_back(makeBlock("Read",  "t1", "42 lines"));
    blocks.push_back(makeBlock("Read",  "t2", "15 lines"));
    blocks.push_back(makeBlock("Grep",  "t3", "Found 5 matches"));
    blocks.push_back(makeBlock("Grep",  "t4", "Found 2 matches"));
    blocks.push_back(makeBlock("Bash",  "t5", "Done"));

    MessagePipeline pipeline;
    // Call collapseReadSearchGroups directly to test GroupKind subdivision
    // in isolation (process() would run groupConsecutiveToolUses first, which
    // wraps consecutive same-name results into ToolGroup blocks that
    // collapseReadSearchGroups then ignores).
    auto result = pipeline.collapseReadSearchGroups(blocks);

    // Count CollapsedGroup blocks — expect 3 (Read x2, Grep x2, Bash x1)
    int collapsedCount = 0;
    for (auto& b : result) {
        if (b.type == ContentBlock::CollapsedGroup) collapsedCount++;
    }
    REQUIRE(collapsedCount == 3);
}

// ========== P6-P0a: isToolNarration classifier tests ==========

static ContentBlock makeAnswerText(const String& text) {
    ContentBlock b;
    b.type = ContentBlock::AnswerText;
    b.text = text;
    return b;
}

static ContentBlock makeRead(const String& tcId, const String& summaryText) {
    ContentBlock b;
    b.type = ContentBlock::ToolResult;
    b.toolName = "Read";
    b.toolCallId = tcId;
    b.summary = ToolResultSummary::success(summaryText);
    b.resultStatus = ToolResultStatus::Success;
    return b;
}

static ContentBlock makeGrep(const String& tcId, const String& summaryText) {
    ContentBlock b;
    b.type = ContentBlock::ToolResult;
    b.toolName = "Grep";
    b.toolCallId = tcId;
    b.summary = ToolResultSummary::success(summaryText);
    b.resultStatus = ToolResultStatus::Success;
    return b;
}

static ContentBlock makeBash(const String& tcId, const String& summaryText) {
    ContentBlock b;
    b.type = ContentBlock::ToolResult;
    b.toolName = "Bash";
    b.toolCallId = tcId;
    b.summary = ToolResultSummary::success(summaryText);
    b.resultStatus = ToolResultStatus::Success;
    return b;
}

static int countCollapsedGroups(const std::vector<ContentBlock>& blocks) {
    int n = 0;
    for (auto& b : blocks) {
        if (b.type == ContentBlock::CollapsedGroup) n++;
    }
    return n;
}

static int countToolResults(const std::vector<ContentBlock>& blocks) {
    int n = 0;
    for (auto& b : blocks) {
        if (b.type == ContentBlock::ToolResult) n++;
        n += countToolResults(b.children);
    }
    return n;
}

static int countAnswerText(const std::vector<ContentBlock>& blocks) {
    int n = 0;
    for (auto& b : blocks) {
        if (b.type == ContentBlock::AnswerText) n++;
    }
    return n;
}

// Count all leaf ToolResult blocks, including those nested inside
// CollapsedGroup and ToolGroup children.
static int countAllToolResults(const std::vector<ContentBlock>& blocks) {
    int n = 0;
    for (auto& b : blocks) {
        if (b.type == ContentBlock::ToolResult) n++;
        n += countAllToolResults(b.children);
    }
    return n;
}

// Helper: run full process() on blocks and return result
static std::vector<ContentBlock> runPipeline(std::vector<ContentBlock> blocks) {
    MessagePipeline pipeline;
    return pipeline.process(std::move(blocks));
}

TEST_CASE("P6-P0a: Read tools separated by short narration collapse into one group",
          "[MessagePipeline][p0a][narration]") {
    // Simulates the core G2 scenario:
    //   AnswerText "Let me read the key files."
    //   Read toolu_001
    //   AnswerText "Now let me check MessagePipeline."
    //   Read toolu_002
    //   AnswerText "And the headers."
    //   Read toolu_003
    //   Read toolu_004
    //   Read toolu_005
    //
    // All 5 Read should collapse into one CollapsedGroup, not 3.
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the key files."));
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeAnswerText("Now let me check MessagePipeline."));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));
    blocks.push_back(makeAnswerText("And the headers."));
    blocks.push_back(makeRead("t3", "StreamBuffer.hpp (89 lines)"));
    blocks.push_back(makeRead("t4", "MessagePipeline.hpp (76 lines)"));
    blocks.push_back(makeRead("t5", "FtxuiRepl.cpp (~800 lines)"));

    auto result = runPipeline(blocks);

    // Should have exactly 1 CollapsedGroup containing all 5 reads.
    // The CollapsedGroup may nest reads through a P3 ToolGroup wrapper.
    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 5);  // all 5 reads accounted for

    // Verify summary mentions "Read 5 files"
    for (auto& b : result) {
        if (b.type == ContentBlock::CollapsedGroup) {
            REQUIRE(b.summary.primaryText.find("Read 5 files") != String::npos);
        }
    }

    // Narration text blocks should still be present (not deleted)
    REQUIRE(countAnswerText(result) >= 3);
}

TEST_CASE("P6-P0a: Substantive AnswerText still breaks group",
          "[MessagePipeline][p0a][breaker]") {
    // Long, substantive answer text with summary language should still break.
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));
    // This is a real answer, not narration
    blocks.push_back(makeAnswerText(
        "Based on my analysis, the StreamBuffer is responsible for text "
        "accumulation and thinking tag stripping. The MessagePipeline "
        "handles 7-pass post-processing at AnswerEnd. In summary, the "
        "data flow follows: API SSE -> AgentLoop -> StreamBuffer -> "
        "DisplayEvent -> FtxuiRepl -> MessagePipeline -> ContentBlock tree."));
    blocks.push_back(makeRead("t3", "FtxuiRepl.cpp (~800 lines)"));

    auto result = runPipeline(blocks);

    // Should have 2 groups: Readx2 before answer, Readx1 after
    REQUIRE(countCollapsedGroups(result) >= 2);
}

TEST_CASE("P6-P0a: Markdown code block AnswerText still breaks group",
          "[MessagePipeline][p0a][markdown]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeAnswerText(
        "The pipeline structure is:\n```\nAPI -> StreamBuffer -> FtxuiRepl\n```"));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));

    auto result = runPipeline(blocks);

    // Code block should prevent grouping across it
    REQUIRE(countCollapsedGroups(result) >= 2);
}

TEST_CASE("P6-P0a: Markdown bullet list AnswerText still breaks group",
          "[MessagePipeline][p0a][markdown]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeAnswerText("Key components:\n- StreamBuffer\n- MessagePipeline\n- FtxuiRepl"));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));

    auto result = runPipeline(blocks);

    // Bullet list should break group
    REQUIRE(countCollapsedGroups(result) >= 2);
}

TEST_CASE("P6-P0a: Short narration before Bash does not merge Read+Bash",
          "[MessagePipeline][p0a][crosskind]") {
    // Narration should not cause Read and Bash to merge across tool kinds.
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the files."));
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));
    blocks.push_back(makeAnswerText("Now let me count the lines."));
    blocks.push_back(makeBash("t3", "1914 lines total"));

    auto result = runPipeline(blocks);

    // Readx2 should be in one group, Bash in another (separate kinds).
    // Different GroupKinds (ReadGroup vs BashGroup) MUST trigger a flush.
    REQUIRE(countCollapsedGroups(result) >= 2);
    REQUIRE(countAllToolResults(result) == 3);

    // Verify Read and Bash are in separate groups
    int readGroups = 0;
    int bashGroups = 0;
    for (auto& b : result) {
        if (b.type == ContentBlock::CollapsedGroup) {
            if (b.summary.primaryText.find("Read") != String::npos) readGroups++;
            if (b.summary.primaryText.find("Ran") != String::npos) bashGroups++;
        }
    }
    REQUIRE(readGroups == 1);
    REQUIRE(bashGroups == 1);
}

TEST_CASE("P6-P0a: Existing consecutive Read collapse still works",
          "[MessagePipeline][p0a][regression]") {
    // Consecutive reads without narration should still collapse (existing behavior).
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));
    blocks.push_back(makeRead("t3", "FtxuiRepl.cpp (~800 lines)"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 3);  // all 3 reads accounted for
}

TEST_CASE("P6-P0a: isToolNarration classification rules", "[MessagePipeline][p0a][unit]") {
    MessagePipeline pipeline;

    // Narration patterns
    REQUIRE(pipeline.isToolNarration(makeAnswerText("Let me read that file.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("Now let me check the headers.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("I'll search for references.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("First, let me find the files.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("Next, I'll inspect the code.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("And the headers.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("Looking at the pipeline now.")));
    REQUIRE(pipeline.isToolNarration(makeAnswerText("Checking FtxuiRepl.cpp.")));

    // Single short sentence
    REQUIRE(pipeline.isToolNarration(makeAnswerText("Now the headers.")));

    // NOT narration: conclusion language
    REQUIRE_FALSE(pipeline.isToolNarration(
        makeAnswerText("In summary, the pipeline is well-structured.")));
    REQUIRE_FALSE(pipeline.isToolNarration(
        makeAnswerText("Here's a summary of the findings.")));

    // NOT narration: code block
    REQUIRE_FALSE(pipeline.isToolNarration(
        makeAnswerText("The code:\n```\nAPI -> StreamBuffer\n```")));

    // NOT narration: bullet list
    REQUIRE_FALSE(pipeline.isToolNarration(
        makeAnswerText("Components:\n- StreamBuffer\n- MessagePipeline")));

    // NOT narration: long multi-sentence paragraph
    REQUIRE_FALSE(pipeline.isToolNarration(
        makeAnswerText(
            "The StreamBuffer is the central component of the output pipeline. "
            "It receives TypedStreamEvents from the AgentLoop and converts them "
            "into DisplayEvents. The IncrementalBlockParser detects paragraph "
            "boundaries and the thinking tag stripper handles 6 tag variants. "
            "This class is essential for proper text formatting.")));
}
