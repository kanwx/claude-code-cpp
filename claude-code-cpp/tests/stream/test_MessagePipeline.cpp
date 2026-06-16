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
