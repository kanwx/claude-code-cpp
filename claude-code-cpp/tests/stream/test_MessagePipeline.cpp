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

TEST_CASE("collapseReadSearchGroups merges Read and Grep into ExplorationGroup",
          "[MessagePipeline][groupkind][p0b]") {
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

    // Interleave Read and Grep — should merge into one ExplorationGroup
    blocks.push_back(makeBlock("Read",  "t1", "42 lines"));
    blocks.push_back(makeBlock("Read",  "t2", "15 lines"));
    blocks.push_back(makeBlock("Grep",  "t3", "Found 5 matches"));
    blocks.push_back(makeBlock("Grep",  "t4", "Found 2 matches"));
    blocks.push_back(makeBlock("Bash",  "t5", "Done"));

    MessagePipeline pipeline;
    auto result = pipeline.collapseReadSearchGroups(blocks);

    // Should have 2 groups: ExplorationGroup (Read×2 + Grep×2) + BashGroup (Bash×1)
    int collapsedCount = 0;
    for (auto& b : result) {
        if (b.type == ContentBlock::CollapsedGroup) collapsedCount++;
    }
    REQUIRE(collapsedCount == 2);

    // Verify the ExplorationGroup summary combines Read and Search
    bool foundExploration = false;
    for (auto& b : result) {
        if (b.type == ContentBlock::CollapsedGroup &&
            b.summary.primaryText.find("Read") != String::npos &&
            b.summary.primaryText.find("Searched") != String::npos) {
            foundExploration = true;
        }
    }
    REQUIRE(foundExploration);
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

// ========== P6-P0b: Multi-kind exploration grouping ==========

static ContentBlock makeGlob(const String& tcId, const String& summaryText) {
    ContentBlock b;
    b.type = ContentBlock::ToolResult;
    b.toolName = "Glob";
    b.toolCallId = tcId;
    b.summary = ToolResultSummary::success(summaryText);
    b.resultStatus = ToolResultStatus::Success;
    return b;
}

static ContentBlock makeLS(const String& tcId, const String& summaryText) {
    ContentBlock b;
    b.type = ContentBlock::ToolResult;
    b.toolName = "LS";
    b.toolCallId = tcId;
    b.summary = ToolResultSummary::success(summaryText);
    b.resultStatus = ToolResultStatus::Success;
    return b;
}

static ContentBlock makeWebSearch(const String& tcId, const String& summaryText) {
    ContentBlock b;
    b.type = ContentBlock::ToolResult;
    b.toolName = "WebSearch";
    b.toolCallId = tcId;
    b.summary = ToolResultSummary::success(summaryText);
    b.resultStatus = ToolResultStatus::Success;
    return b;
}

TEST_CASE("P6-P0b: Glob Read Glob Read collapses into one ExplorationGroup",
          "[MessagePipeline][p0b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeRead("t2", "42 lines"));
    blocks.push_back(makeGlob("t3", "include/*.hpp"));
    blocks.push_back(makeRead("t4", "15 lines"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 4);
}

TEST_CASE("P6-P0b: Grep Glob Read collapses into one ExplorationGroup",
          "[MessagePipeline][p0b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGrep("t1", "Found 3 matches"));
    blocks.push_back(makeGlob("t2", "src/**/*.cpp"));
    blocks.push_back(makeRead("t3", "128 lines"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 3);
}

TEST_CASE("P6-P0b: Read LS Grep collapses into one ExplorationGroup",
          "[MessagePipeline][p0b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "42 lines"));
    blocks.push_back(makeLS("t2", "12 items"));
    blocks.push_back(makeGrep("t3", "Found 5 matches"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 3);
}

TEST_CASE("P6-P0b: Glob Bash Read produces ExplorationGroup + BashGroup",
          "[MessagePipeline][p0b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeBash("t2", "wc -l *.cpp"));
    blocks.push_back(makeRead("t3", "42 lines"));

    auto result = runPipeline(blocks);

    // Glob and Read cannot merge across Bash — two groups
    REQUIRE(countCollapsedGroups(result) >= 2);
    REQUIRE(countAllToolResults(result) == 3);

    // Verify Bash is in its own group
    int bashGroups = 0;
    for (auto& b : result) {
        if (b.type == ContentBlock::CollapsedGroup &&
            b.summary.primaryText.find("Ran") != String::npos) {
            bashGroups++;
        }
    }
    REQUIRE(bashGroups == 1);
}

TEST_CASE("P6-P0b: substantive AnswerText breaks exploration group",
          "[MessagePipeline][p0b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeAnswerText(
        "The codebase has 3 main directories. Each one contains a different "
        "layer of the pipeline. The src/ directory is the largest."));
    blocks.push_back(makeRead("t2", "42 lines"));

    auto result = runPipeline(blocks);

    // Substantive AnswerText must break the group
    REQUIRE(countCollapsedGroups(result) >= 2);
    REQUIRE(countAllToolResults(result) == 2);
}

TEST_CASE("P6-P0b: Glob Read WebSearch produces ExplorationGroup + WebGroup",
          "[MessagePipeline][p0b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeRead("t2", "42 lines"));
    blocks.push_back(makeWebSearch("t3", "Found docs"));

    auto result = runPipeline(blocks);

    // WebSearch is WebGroup, separate from ExplorationGroup
    REQUIRE(countCollapsedGroups(result) >= 2);
    REQUIRE(countAllToolResults(result) == 3);
}

TEST_CASE("P6-P0b: combined summary with Read and Search",
          "[MessagePipeline][p0b][summary]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "42 lines"));
    blocks.push_back(makeRead("t2", "15 lines"));
    blocks.push_back(makeGrep("t3", "Found 5 matches"));
    blocks.push_back(makeGrep("t4", "Found 2 matches"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);

    // Summary should combine Read + Search
    auto& cg = result[0];
    REQUIRE(cg.type == ContentBlock::CollapsedGroup);
    CHECK(cg.summary.primaryText.find("Read 2 files") != String::npos);
    CHECK(cg.summary.primaryText.find("Searched 2 patterns") != String::npos);
    CHECK(cg.summary.primaryText.find(" and ") != String::npos);
}

TEST_CASE("P6-P0b: read-only summary backward compatibility",
          "[MessagePipeline][p0b][summary]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "42 lines"));
    blocks.push_back(makeRead("t2", "15 lines"));
    blocks.push_back(makeRead("t3", "8 lines"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);
    CHECK(result[0].summary.primaryText == "Read 3 files");
}

TEST_CASE("P6-P0b: search-only summary backward compatibility",
          "[MessagePipeline][p0b][summary]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeGrep("t2", "Found 3 matches"));
    blocks.push_back(makeGlob("t3", "include/*.hpp"));

    auto result = runPipeline(blocks);

    REQUIRE(countCollapsedGroups(result) == 1);
    CHECK(result[0].summary.primaryText.find("Searched 3 patterns") != String::npos);
    // No "Read" text
    CHECK(result[0].summary.primaryText.find("Read") == String::npos);
}

TEST_CASE("P6-P0b: narration does not break exploration group (P6-P0a regression)",
          "[MessagePipeline][p0b][p0a][regression]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeGlob("t2", "include/*.hpp"));
    blocks.push_back(makeAnswerText("Let me read these files now."));
    blocks.push_back(makeRead("t3", "42 lines"));
    blocks.push_back(makeAnswerText("And now let me search for more."));
    blocks.push_back(makeGrep("t4", "Found 3 matches"));

    auto result = runPipeline(blocks);

    // All exploration tools should be in one group with narration pass-through
    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 4);
}

// ========== P6-P1a: narration dimmed flag ==========

// Helper: find all AnswerText blocks (top-level only, not in children)
static std::vector<const ContentBlock*> findAnswerTexts(const std::vector<ContentBlock>& blocks) {
    std::vector<const ContentBlock*> result;
    for (auto& b : blocks) {
        if (b.type == ContentBlock::AnswerText) result.push_back(&b);
    }
    return result;
}

static ContentBlock makeLongAnswer(const String& text) {
    ContentBlock b;
    b.type = ContentBlock::AnswerText;
    b.text = text;
    return b;
}

static ContentBlock makeSubstantiveAnswer() {
    return makeAnswerText(
        "The key finding is that the pipeline correctly handles interleaved text "
        "and tool calls. The collapse logic groups tools by category, and the "
        "narration classifier prevents tool-intro text from breaking groups.");
}

TEST_CASE("P6-P1a: tool narration AnswerText gets dimmed=true",
          "[MessagePipeline][p1a][narration]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the key files."));
    blocks.push_back(makeRead("t1", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeRead("t2", "MessagePipeline.cpp (632 lines)"));

    auto result = runPipeline(blocks);
    auto answers = findAnswerTexts(result);

    // "Let me read the key files." should be dimmed (isToolNarration)
    REQUIRE(answers.size() >= 1);
    bool foundNarration = false;
    for (auto* a : answers) {
        if (a->text.find("Let me read") != String::npos) {
            CHECK(a->dimmed == true);
            foundNarration = true;
        }
    }
    CHECK(foundNarration);
}

TEST_CASE("P6-P1a: tool narration still does not break exploration grouping",
          "[MessagePipeline][p1a][p0a][regression]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read these files."));
    blocks.push_back(makeRead("t1", "file1.cpp (317 lines)"));
    blocks.push_back(makeAnswerText("Now let me search."));
    blocks.push_back(makeGrep("t2", "Found 3 matches"));
    blocks.push_back(makeAnswerText("And the headers too."));
    blocks.push_back(makeRead("t3", "file3.hpp (89 lines)"));

    auto result = runPipeline(blocks);

    // All 3 explorations in one group — narration does not break grouping
    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 3);

    // All 3 narrations should be dimmed
    auto answers = findAnswerTexts(result);
    REQUIRE(answers.size() == 3);
    for (auto* a : answers) {
        CHECK(a->dimmed == true);
    }
}

TEST_CASE("P6-P1a: substantive AnswerText remains dimmed=false",
          "[MessagePipeline][p1a]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeSubstantiveAnswer());
    blocks.push_back(makeRead("t1", "data.txt (100 lines)"));

    auto result = runPipeline(blocks);
    auto answers = findAnswerTexts(result);

    REQUIRE(answers.size() >= 1);
    for (auto* a : answers) {
        CHECK(a->dimmed == false);
    }
}

TEST_CASE("P6-P1a: code block AnswerText remains dimmed=false",
          "[MessagePipeline][p1a]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me check:\n```\ncode here\n```"));
    blocks.push_back(makeRead("t1", "file.cpp (317 lines)"));

    auto result = runPipeline(blocks);
    auto answers = findAnswerTexts(result);

    REQUIRE(answers.size() >= 1);
    for (auto* a : answers) {
        CHECK(a->dimmed == false);
    }
}

TEST_CASE("P6-P1a: bullet list AnswerText remains dimmed=false",
          "[MessagePipeline][p1a]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me list:\n- item 1\n- item 2"));
    blocks.push_back(makeRead("t1", "file.cpp (317 lines)"));

    auto result = runPipeline(blocks);
    auto answers = findAnswerTexts(result);

    REQUIRE(answers.size() >= 1);
    for (auto* a : answers) {
        CHECK(a->dimmed == false);
    }
}

TEST_CASE("P6-P1a: P6-P0a Read grouping across narration regression",
          "[MessagePipeline][p1a][p0a][regression]") {
    // Same test as P6-P0a but verifies dimmed flag is the only new behavior
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

    // P0a: all 5 Reads should collapse into one group
    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 5);

    // P1a: narrations dimmed but grouping unchanged
    auto answers = findAnswerTexts(result);
    REQUIRE(answers.size() == 3);
    for (auto* a : answers) {
        CHECK(a->dimmed == true);
    }
}

TEST_CASE("P6-P1a: P6-P0b ExplorationGroup grouping regression",
          "[MessagePipeline][p1a][p0b][regression]") {
    // Same test as P6-P0b but verifies dimmed flag is the only new behavior
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me find files."));
    blocks.push_back(makeGlob("t1", "src/*.cpp"));
    blocks.push_back(makeGlob("t2", "include/*.hpp"));
    blocks.push_back(makeAnswerText("Let me read these files now."));
    blocks.push_back(makeRead("t3", "42 lines"));
    blocks.push_back(makeAnswerText("And now let me search for more."));
    blocks.push_back(makeGrep("t4", "Found 3 matches"));

    auto result = runPipeline(blocks);

    // P0b: all exploration tools in one group with narration pass-through
    REQUIRE(countCollapsedGroups(result) == 1);
    REQUIRE(countAllToolResults(result) == 4);

    // P1a: all 3 narrations dimmed
    auto answers = findAnswerTexts(result);
    REQUIRE(answers.size() == 3);
    for (auto* a : answers) {
        CHECK(a->dimmed == true);
    }
}

// ============================================================================
// P2b Tests: Centralized narration dimming pass (dimToolNarration)
// ============================================================================

// Test P2b.1: "Let me read the key files." between Bash and Read → dimmed
TEST_CASE("P2b: dimToolNarration dims narration between non-collapsible tools",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeBash("t1", "tests passed"));
    blocks.push_back(makeAnswerText("Let me read the key files."));
    blocks.push_back(makeRead("t2", "StreamBuffer.cpp (317 lines)"));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == true);
}

// Test P2b.2: "Now I'll edit the config." before Edit → dimmed
TEST_CASE("P2b: dimToolNarration dims narration before Edit",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "42 lines"));
    blocks.push_back(makeAnswerText("Now I'll edit the config."));
    // No Edit tool helper; use ToolResult with toolName "Edit"
    ContentBlock edit;
    edit.type = ContentBlock::ToolResult;
    edit.toolName = "Edit";
    edit.toolCallId = "t2";
    edit.summary = ToolResultSummary::success("edited");
    edit.resultStatus = ToolResultStatus::Success;
    blocks.push_back(edit);

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == true);
}

// Test P2b.3: "Let me run the tests." before Bash → dimmed
TEST_CASE("P2b: dimToolNarration dims narration before Bash",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeRead("t1", "42 lines"));
    blocks.push_back(makeAnswerText("Let me run the tests."));
    blocks.push_back(makeBash("t2", "all tests passed"));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == true);
}

// Test P2b.4: Already-dimmed narration remains dimmed (idempotent)
TEST_CASE("P2b: dimToolNarration idempotent on already-dimmed blocks",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    ContentBlock a = makeAnswerText("Let me search.");
    a.dimmed = true;
    blocks.push_back(a);

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == true);  // still dimmed
}

// Test P2b.5: "Here is the complete analysis:" → NOT dimmed
// (contains conclusion words, isToolNarration returns false)
TEST_CASE("P2b: phase headers not dimmed — Here is",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Here is the complete analysis:"));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == false);
}

// Test P2b.6: "The output pipeline follows:" → NOT dimmed
// (doesn't start with narration prefix)
TEST_CASE("P2b: phase headers not dimmed — output pipeline",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("The output pipeline follows:"));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == false);
}

// Test P2b.7: Multi-paragraph summary → NOT dimmed
TEST_CASE("P2b: multi-paragraph summary not dimmed",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText(
        "Here is a summary of the changes.\n\n"
        "First, we refactored the pipeline.\n"
        "Second, we added tests."));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == false);
}

// Test P2b.8: Short non-narration text "Done." → NOT dimmed
TEST_CASE("P2b: short non-narration text not dimmed",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Done."));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]->dimmed == false);
}

// Test P2b.9: AgentProgress text is not affected (not AnswerText)
TEST_CASE("P2b: AgentProgress not affected by dimToolNarration",
          "[MessagePipeline][p2b]") {
    std::vector<ContentBlock> blocks;
    ContentBlock agent;
    agent.type = ContentBlock::AgentProgress;
    agent.toolName = "general-purpose";
    agent.text = "Let me analyze the codebase...";
    blocks.push_back(agent);

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    // AgentProgress should not be dimmed (only AnswerText is checked)
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].dimmed == false);
}

// Test P2b.10: current-turn-only — previous turn blocks not mutated
TEST_CASE("P2b: dimToolNarration respects startIndex (current-turn scoped)",
          "[MessagePipeline][p2b]") {
    // Turn 1: user message + narration + tool
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));
    blocks.push_back(makeRead("t1", "42 lines"));

    // Turn 2 starts here (snapshot index)
    size_t turn2Start = blocks.size();
    ContentBlock user2;
    user2.type = ContentBlock::UserMessage;
    user2.text = "run tests";
    blocks.push_back(user2);
    blocks.push_back(makeAnswerText("Let me run the tests."));
    blocks.push_back(makeBash("t2", "tests passed"));

    // Dim only Turn 2's range
    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, turn2Start);

    // Turn 1's narration should NOT be dimmed
    auto allAnswers = findAnswerTexts(blocks);
    REQUIRE(allAnswers.size() == 2);
    CHECK(allAnswers[0]->dimmed == false);  // Turn 1: NOT dimmed (out of range)
    CHECK(allAnswers[1]->dimmed == true);   // Turn 2: dimmed (in range)
}

// Test P2b.11: P2a regression — dimmed narration never reaches ● eligibility.
// dimToolNarration covers ALL isToolNarration()==true blocks regardless of
// tool context. Combined with P2b dimming happening before P1b/P2a in
// FtxuiRepl, this guarantees dimmed narration is skipped by P2a.
TEST_CASE("P2b: dimmed narration skips P2a eligibility path",
          "[MessagePipeline][p2b]") {
    // This test verifies the structural guarantee: after dimToolNarration,
    // all isToolNarration()==true blocks are dimmed, so P1b skips them
    // and P2a eligibility check never runs on them.
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeBash("t1", "command output"));
    blocks.push_back(makeAnswerText("Let me read the key files."));
    blocks.push_back(makeAnswerText("Here is the analysis:"));

    MessagePipeline pipeline;
    pipeline.dimToolNarration(blocks, 0);

    auto answers = findAnswerTexts(blocks);
    REQUIRE(answers.size() == 2);
    // First AnswerText: isToolNarration → dimmed, P2a never sees it
    CHECK(answers[0]->dimmed == true);
    // Second AnswerText: has "here is" → not narration → not dimmed → P2a eligible
    CHECK(answers[1]->dimmed == false);
}

// Test P2b.12: P1a/P1b/P1c/P1d regression — full pipeline with dimToolNarration
TEST_CASE("P2b: full pipeline + dimToolNarration preserves grouping",
          "[MessagePipeline][p2b][regression]") {
    // Baseline Task B scenario with mixed collapsible and non-collapsible tools
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me search for pipeline files."));
    blocks.push_back(makeGrep("t1", "Found 12 matches"));
    blocks.push_back(makeAnswerText("Let me read the key files."));
    blocks.push_back(makeRead("t2", "StreamBuffer.cpp (317 lines)"));
    blocks.push_back(makeAnswerText("Now let me run the tests."));
    blocks.push_back(makeBash("t3", "all tests passed"));
    blocks.push_back(makeAnswerText("Here is the complete analysis:"));

    // Run full pipeline first (P1a dims narration in collapsible sequences)
    MessagePipeline pipeline;
    auto result = pipeline.process(std::move(blocks));

    // Then P2b pass over the current turn
    pipeline.dimToolNarration(result, 0);

    // All narrations should be dimmed
    auto answers = findAnswerTexts(result);
    REQUIRE(answers.size() == 4);

    // "Let me search for pipeline files." — between non-collapsible,
    // P1a may or may not dim depending on config. P2b dims it.
    CHECK(answers[0]->dimmed == true);

    // "Let me read the key files." — between collapsible (Grep+Read),
    // P1a dims it. P2b is idempotent.
    CHECK(answers[1]->dimmed == true);

    // "Now let me run the tests." — between Read+Bash, non-collapsible.
    // P2b dims it (this is the key fix).
    CHECK(answers[2]->dimmed == true);

    // "Here is the complete analysis:" — phase header, NOT narration
    CHECK(answers[3]->dimmed == false);
}
