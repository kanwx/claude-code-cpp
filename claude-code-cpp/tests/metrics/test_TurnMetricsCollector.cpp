#include <catch2/catch_test_macros.hpp>
#include "claude/metrics/TurnMetricsCollector.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <fstream>
#include <filesystem>

using namespace claude;
namespace fs = std::filesystem;

static ContentBlock makeAnswerText(const std::string& text) {
    ContentBlock cb;
    cb.type = ContentBlock::AnswerText;
    cb.text = text;
    return cb;
}

static ContentBlock makeToolResult(const std::string& name, const std::string& id) {
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = name;
    cb.toolCallId = id;
    cb.summary = ToolResultSummary::success("Done");
    return cb;
}

static ContentBlock makeCollapsedGroup(const std::string& name,
                                        const std::vector<std::string>& ids) {
    ContentBlock cb;
    cb.type = ContentBlock::CollapsedGroup;
    cb.toolName = name;
    cb.toolUseIds = ids;
    cb.summary = ToolResultSummary::success("Read 3 files");
    return cb;
}

static ContentBlock makeToolGroup(const std::string& name,
                                  const std::vector<std::string>& ids) {
    ContentBlock cb;
    cb.type = ContentBlock::ToolGroup;
    cb.toolName = name;
    cb.toolUseIds = ids;
    cb.summary = ToolResultSummary::success("Ran command");
    return cb;
}

static ContentBlock makeToolProgress(const std::string& name, const std::string& id) {
    ContentBlock cb;
    cb.type = ContentBlock::ToolProgress;
    cb.toolName = name;
    cb.toolCallId = id;
    cb.activity = "Running " + name + "...";
    return cb;
}

TEST_CASE("TurnMetricsCollector detects meta-narrative patterns", "[metrics][M1]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-m1.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file first."));
    blocks.push_back(makeAnswerText("I'll check the implementation now."));
    blocks.push_back(makeAnswerText("Next, let me verify the test results."));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    CHECK(m.answerTextBlocks == 3);
    // "Let me read..." + "Next, let me verify..." both match
    CHECK(m.metaPatterns.let_me == 2);
    CHECK(m.metaPatterns.ill == 1);
    CHECK(m.metaPatterns.next_sentence == 1);
    CHECK(m.metaPatterns.total() >= 3);

    collector.write(m);
    CHECK(fs::exists(tmp));
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector correctly identifies inter-tool text", "[metrics][M2]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-m2.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Simulate: AnswerText before a ToolResult = inter-tool text
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me find the definition of ContentBlock."));
    blocks.push_back(makeToolResult("Grep", "toolu_001"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    CHECK(m.answerTextBlocks == 1);
    CHECK(m.toolResultBlocks == 1);
    CHECK(m.interToolWordCounts.size() == 1);
    // "Let me find the definition of ContentBlock." = 7 words
    CHECK(m.interToolWordCounts[0] == 7);

    collector.write(m);
    fs::remove(tmp);
}

// ========== M2 edge-case coverage ==========

TEST_CASE("M2: AnswerText adjacent to ToolGroup is inter-tool", "[metrics][M2][ToolGroup]") {
    auto tmp = fs::temp_directory_path() / "test-m2-toolgroup.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me check that."));
    blocks.push_back(makeToolGroup("Bash", {"toolu_001"}));

    auto m = collector.analyze(blocks, 0, "test-model", 0);
    CHECK(m.interToolWordCounts.size() == 1);
    CHECK(m.interToolWordCounts[0] > 0);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: AnswerText adjacent to CollapsedGroup is inter-tool", "[metrics][M2][CollapsedGroup]") {
    auto tmp = fs::temp_directory_path() / "test-m2-collapsed.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read these files."));
    blocks.push_back(makeCollapsedGroup("Read", {"toolu_001", "toolu_002"}));

    auto m = collector.analyze(blocks, 0, "test-model", 0);
    CHECK(m.interToolWordCounts.size() == 1);
    CHECK(m.interToolWordCounts[0] > 0);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: AnswerText adjacent to ToolProgress is inter-tool", "[metrics][M2][ToolProgress]") {
    auto tmp = fs::temp_directory_path() / "test-m2-progress.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Running the build..."));
    blocks.push_back(makeToolProgress("Bash", "toolu_001"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);
    CHECK(m.interToolWordCounts.size() == 1);
    CHECK(m.interToolWordCounts[0] > 0);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: final AnswerText after all tools is NOT inter-tool", "[metrics][M2][final]") {
    auto tmp = fs::temp_directory_path() / "test-m2-final.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Final summary text: preceded by tool, but NOT followed by tool → NOT inter-tool
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeToolResult("Read", "toolu_001"));
    blocks.push_back(makeAnswerText("The bug is on line 42. The fix is simple."));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // Fixed: next-only check. Final AnswerText is NOT followed by a tool → not inter-tool.
    CHECK(m.interToolWordCounts.empty());

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: consecutive AnswerText blocks between tools", "[metrics][M2][consecutive]") {
    auto tmp = fs::temp_directory_path() / "test-m2-consec.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Simulate: ToolResult → AnswerText → AnswerText → ToolResult
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeToolResult("Read", "toolu_001"));
    blocks.push_back(makeAnswerText("Let me check something."));
    blocks.push_back(makeAnswerText("Now let me look deeper."));
    blocks.push_back(makeToolResult("Grep", "toolu_002"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // Next-only check on ±1 adjacency:
    //   Block 1 ("Let me check..."): next is AnswerText → not inter-tool
    //   Block 2 ("Now let me..."):   next is ToolResult → inter-tool ✓
    // Only the last AnswerText in the run is counted.
    // Consecutive AnswerText blocks don't occur post-pipeline (streaming
    // text is committed as a single block at AnswerEnd), so this is a
    // theoretical edge case with acceptable behavior.
    CHECK(m.interToolWordCounts.size() == 1);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: AnswerText → ToolResult is inter-tool", "[metrics][M2][pre-tool]") {
    auto tmp = fs::temp_directory_path() / "test-m2-pre-tool.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));
    blocks.push_back(makeToolResult("Read", "toolu_001"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // AnswerText is followed by ToolResult → inter-tool
    CHECK(m.interToolWordCounts.size() == 1);
    CHECK(m.interToolWordCounts[0] > 0);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: ToolResult → AnswerText → ToolResult (sandwiched)", "[metrics][M2][sandwich]") {
    auto tmp = fs::temp_directory_path() / "test-m2-sandwich.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // AnswerText between two tool calls — the canonical inter-tool case
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeToolResult("Read", "toolu_001"));
    blocks.push_back(makeAnswerText("Now let me check the implementation."));
    blocks.push_back(makeToolResult("Grep", "toolu_002"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // AnswerText is followed by ToolResult → inter-tool
    CHECK(m.interToolWordCounts.size() == 1);
    CHECK(m.interToolWordCounts[0] > 0);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("M2: AnswerText-only round (explain) has no inter-tool", "[metrics][M2][explain]") {
    auto tmp = fs::temp_directory_path() / "test-m2-explain.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("The codebase uses a layered pipeline architecture."));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // No tools anywhere → no inter-tool text
    CHECK(m.interToolWordCounts.empty());

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector computes M4 total user-visible words", "[metrics][M4]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-m4.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("The ContentBlock tree has 12 types."));
    blocks.push_back(makeAnswerText("Each type maps to a rendering strategy."));
    blocks.push_back(makeToolResult("Read", "toolu_001"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // "The ContentBlock tree has 12 types." = 6 words
    // "Each type maps to a rendering strategy." = 7 words
    CHECK(m.totalUserWords == 13);
    CHECK(m.answerTextBlocks == 2);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector counts tool calls for M6", "[metrics][M6]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-m6.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeToolResult("Read", "toolu_001"));
    blocks.push_back(makeToolResult("Grep", "toolu_002"));
    blocks.push_back(makeToolResult("Bash", "toolu_003"));
    blocks.push_back(makeToolResult("Read", "toolu_004"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    CHECK(m.toolCallCount == 4);
    // M6 = tool_calls / (answer_words + 50) = 4 / (0 + 50) = 0.08
    double m6 = static_cast<double>(m.toolCallCount) / (m.totalUserWords + 50);
    CHECK(m6 > 0.07);
    CHECK(m6 < 0.09);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector M6 increases with tool density", "[metrics][M6][density]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-m6-density.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Low-density case: many words, few tools
    std::vector<ContentBlock> lowBlocks;
    lowBlocks.push_back(makeAnswerText("Let me carefully examine the codebase to understand the architecture. "
                                       "I will look at several files and analyze the patterns."));
    lowBlocks.push_back(makeToolResult("Read", "toolu_001"));

    auto low = collector.analyze(lowBlocks, 0, "test-model", 0);
    double m6_low = static_cast<double>(low.toolCallCount) / (low.totalUserWords + 50);

    // High-density case: few words, many tools
    std::vector<ContentBlock> highBlocks;
    highBlocks.push_back(makeToolResult("Read", "toolu_001"));
    highBlocks.push_back(makeToolResult("Read", "toolu_002"));
    highBlocks.push_back(makeToolResult("Grep", "toolu_003"));
    highBlocks.push_back(makeToolResult("Bash", "toolu_004"));

    auto high = collector.analyze(highBlocks, 0, "test-model", 0);
    double m6_high = static_cast<double>(high.toolCallCount) / (high.totalUserWords + 50);

    CHECK(m6_high > m6_low);
    CHECK(high.isSilentRound == true);  // no AnswerText blocks

    collector.write(low);
    collector.write(high);
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector marks silent rounds", "[metrics][M5]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-m5.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Round with text
    std::vector<ContentBlock> withText;
    withText.push_back(makeAnswerText("Found the bug."));
    auto m1 = collector.analyze(withText, 0, "claude", 0);
    CHECK(m1.isSilentRound == false);

    // Round without text (only tools)
    std::vector<ContentBlock> toolOnly;
    toolOnly.push_back(makeToolResult("Read", "toolu_001"));
    toolOnly.push_back(makeToolResult("Grep", "toolu_002"));
    auto m2 = collector.analyze(toolOnly, 0, "claude", 0);
    CHECK(m2.isSilentRound == true);

    // Empty round
    std::vector<ContentBlock> empty;
    auto m3 = collector.analyze(empty, 0, "claude", 0);
    CHECK(m3.isSilentRound == true);

    collector.write(m1);
    collector.write(m2);
    collector.write(m3);
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector respects turnStartIndex", "[metrics][slicing]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-slice.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Previous turn text."));  // idx 0: old turn
    blocks.push_back(makeToolResult("Read", "toolu_old"));     // idx 1: old turn
    blocks.push_back(makeAnswerText("Current turn text."));    // idx 2: start of current turn
    blocks.push_back(makeToolResult("Read", "toolu_new"));     // idx 3: current turn

    auto m = collector.analyze(blocks, 2, "test-model", 0);

    // Should only count blocks from index 2 onwards
    CHECK(m.answerTextBlocks == 1);
    CHECK(m.toolResultBlocks == 1);
    CHECK(m.toolCallCount == 1);
    CHECK(m.totalUserWords > 0);

    collector.write(m);
    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector JSONL output is valid and contains all fields", "[metrics][jsonl]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-jsonl.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));
    blocks.push_back(makeToolResult("Read", "toolu_001"));
    blocks.push_back(makeAnswerText("The file contains the bug at line 42."));
    blocks.push_back(makeToolResult("Grep", "toolu_002"));

    auto m = collector.analyze(blocks, 0, "claude-sonnet-4-6", 0);
    collector.write(m);

    // Read back and verify
    std::ifstream f(tmp);
    REQUIRE(f.is_open());
    std::string line;
    std::getline(f, line);
    f.close();

    auto j = nlohmann::json::parse(line);

    CHECK(j.contains("ts"));
    CHECK(j["prompt_version"] == "baseline");
    CHECK(j["model"] == "claude-sonnet-4-6");
    CHECK(j.contains("meta_patterns"));
    CHECK(j["meta_patterns"]["let_me"] == 1);
    CHECK(j.contains("block_counts"));
    CHECK(j["block_counts"]["answer_text"] == 2);
    CHECK(j["block_counts"]["tool_result"] == 2);
    CHECK(j["total_user_words"] > 0);
    CHECK(j.contains("is_silent_round"));
    CHECK(j["tool_call_count"] == 2);
    // M6 density: 2 / (8 + 50) = 2/58 ≈ 0.0345
    CHECK(j["m6_density"] > 0.03);
    CHECK(j["m6_density"] < 0.04);
    CHECK(j.contains("text_samples"));
    CHECK(j["text_samples"].size() == 2);

    fs::remove(tmp);
}

TEST_CASE("TurnMetricsCollector handles explain-type tasks (T3/T6)", "[metrics][explain]") {
    auto tmp = fs::temp_directory_path() / "test-metrics-explain.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Simulate T3: pure explanation, no tools
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText(
        "The ContentBlock Type enum defines the visual presentation of each "
        "message in the conversation. UserMessage represents input from the user, "
        "AnswerText is the assistant's response, ThinkingBlock holds extended "
        "reasoning content that can be collapsed. ToolProgress shows an in-progress "
        "tool execution, ToolResult displays the result, and ToolGroup wraps a "
        "logical grouping of tool call and result pairs."
    ));

    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // T3/T6 characteristics: high word count, zero tools, not silent
    CHECK(m.toolCallCount == 0);
    CHECK(m.totalUserWords > 30);  // substantial explanation
    CHECK(m.isSilentRound == false);
    CHECK(m.answerTextBlocks == 1);

    // M6 should be very low (no tools / many words)
    double m6 = static_cast<double>(m.toolCallCount) / (m.totalUserWords + 50);
    CHECK(m6 < 0.01);

    collector.write(m);
    fs::remove(tmp);
}

// ========== Instrumentation validation ==========

TEST_CASE("JSONL output validates experiment metadata completeness", "[metrics][validation][metadata]") {
    auto tmp = fs::temp_directory_path() / "test-v-metadata.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));
    blocks.push_back(makeToolResult("Read", "toolu_001"));
    blocks.push_back(makeAnswerText("Found the issue."));
    blocks.push_back(makeToolGroup("Bash", {"toolu_002"}));
    blocks.push_back(makeCollapsedGroup("Read", {"toolu_003", "toolu_004"}));

    auto m = collector.analyze(blocks, 0, "deepseek-v4-pro", 0);
    collector.write(m);

    // Read back and validate
    std::ifstream f(tmp);
    std::string line;
    std::getline(f, line);
    f.close();

    auto j = nlohmann::json::parse(line);

    // Required experiment metadata
    CHECK(j.contains("ts"));
    CHECK_FALSE(j["ts"].get<std::string>().empty());
    CHECK(j["prompt_version"] == "baseline");
    CHECK(j["model"] == "deepseek-v4-pro");
    CHECK(j["snapshot_index"] == 0);
    CHECK(j["user_turn_index"] == 0);

    // M1 meta patterns (always present)
    CHECK(j.contains("meta_patterns"));
    CHECK(j["meta_patterns"]["total"] >= 0);

    // M2 inter-tool words (may be empty object)
    CHECK(j.contains("inter_tool_words"));
    // First AnswerText is before ToolResult → inter-tool
    // Second AnswerText is between ToolResult and ToolGroup → inter-tool
    CHECK(j["inter_tool_words"]["values"].size() == 2);

    // M3 block counts
    CHECK(j["block_counts"]["answer_text"] == 2);
    CHECK(j["block_counts"]["tool_result"] == 1);
    CHECK(j["block_counts"]["tool_group"] == 1);
    CHECK(j["block_counts"]["collapsed_group"] == 1);

    // M4 total user-visible words
    CHECK(j["total_user_words"] > 0);

    // M5 silent round flag
    CHECK(j.contains("is_silent_round"));
    CHECK(j["is_silent_round"] == false);

    // M6 density (computed, always present)
    CHECK(j.contains("m6_density"));
    CHECK(j["m6_density"].is_number());
    CHECK(j["tool_call_count"] == 4);  // 1 ToolResult + 1 ToolGroup(1) + 1 CollapsedGroup(2)

    // Raw text samples for human verification
    CHECK(j.contains("text_samples"));
    CHECK(j["text_samples"].size() == 2);

    // Combined text
    CHECK(j.contains("combined_text"));

    fs::remove(tmp);
}

TEST_CASE("Multi-round boundary isolation: no cross-round pollution", "[metrics][validation][isolation]") {
    auto tmp = fs::temp_directory_path() / "test-v-isolation.jsonl";
    TurnMetricsCollector collector(tmp.string());

    // Simulate the real-world flow: contentBlocks_ grows over time.
    // Each round's blocks are appended after the previous round's AnswerEnd.
    //
    // Round 1: 3 tool results, no text → silent round
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeToolResult("Read", "r1_t1"));
    blocks.push_back(makeToolResult("Grep", "r1_t2"));
    blocks.push_back(makeToolResult("Glob", "r1_t3"));

    auto r1 = collector.analyze(blocks, 0, "test-model", 0);
    CHECK(r1.isSilentRound == true);
    CHECK(r1.answerTextBlocks == 0);
    CHECK(r1.toolCallCount == 3);
    CHECK(r1.totalUserWords == 0);

    // Round 2: new blocks appended (turnStartIndex = 3 skips R1 blocks)
    blocks.push_back(makeAnswerText("Let me fix the bug."));
    blocks.push_back(makeToolResult("Edit", "r2_t1"));

    auto r2 = collector.analyze(blocks, 3, "test-model", 0);
    CHECK(r2.isSilentRound == false);
    CHECK(r2.answerTextBlocks == 1);
    CHECK(r2.toolCallCount == 1);
    CHECK(r2.interToolWordCounts.size() == 1);

    // Round 3: final text block appended (turnStartIndex = 5 skips R1+R2)
    blocks.push_back(makeAnswerText("Fixed the issue with a null check."));

    auto r3 = collector.analyze(blocks, 5, "test-model", 0);
    CHECK(r3.isSilentRound == false);
    CHECK(r3.answerTextBlocks == 1);
    CHECK(r3.toolCallCount == 0);
    CHECK(r3.interToolWordCounts.empty());  // no tools adjacent → not inter-tool

    // Verify snapshot_index increments independently
    CHECK(r1.snapshotIndex == 0);
    CHECK(r2.snapshotIndex == 1);
    CHECK(r3.snapshotIndex == 2);

    // Verify no cross-round leakage in word counts
    CHECK(r2.totalUserWords > 0);
    CHECK(r3.totalUserWords > 0);

    collector.write(r1);
    collector.write(r2);
    collector.write(r3);
    fs::remove(tmp);
}

TEST_CASE("M6 density: explain task has near-zero density", "[metrics][validation][M6]") {
    auto tmp = fs::temp_directory_path() / "test-v-m6-explain.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText(
        "The ContentBlock tree uses a recursive structure where ToolGroup nodes "
        "hold child blocks, while leaf nodes carry text, tool metadata, or error "
        "information. The MessagePipeline processes these blocks through numbered "
        "passes: reorder, group, collapse, etc."
    ));

    auto m = collector.analyze(blocks, 0, "test-model", 0);
    collector.write(m);

    std::ifstream f(tmp);
    std::string line;
    std::getline(f, line);
    f.close();

    auto j = nlohmann::json::parse(line);

    // Explain task: 0 tools, many words → density near 0
    CHECK(j["tool_call_count"] == 0);
    CHECK(j["m6_density"] == 0.0);
    CHECK(j["total_user_words"] > 20);
    CHECK(j["inter_tool_words"] == nlohmann::json::object());  // empty → no inter-tool

    fs::remove(tmp);
}

TEST_CASE("M6 density: silent tool-only round has high density", "[metrics][validation][M6]") {
    auto tmp = fs::temp_directory_path() / "test-v-m6-silent.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::vector<ContentBlock> blocks;
    blocks.push_back(makeToolResult("Read", "t1"));
    blocks.push_back(makeToolResult("Read", "t2"));
    blocks.push_back(makeToolResult("Grep", "t3"));
    blocks.push_back(makeToolResult("Bash", "t4"));
    blocks.push_back(makeToolResult("Edit", "t5"));

    auto m = collector.analyze(blocks, 0, "test-model", 0);
    collector.write(m);

    std::ifstream f(tmp);
    std::string line;
    std::getline(f, line);
    f.close();

    auto j = nlohmann::json::parse(line);

    // Silent round: 5 tools, 0 words → density = 5/50 = 0.10
    CHECK(j["tool_call_count"] == 5);
    CHECK(j["m6_density"] == 0.1);
    CHECK(j["is_silent_round"] == true);

    fs::remove(tmp);
}
