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
