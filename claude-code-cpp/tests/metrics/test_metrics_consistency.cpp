/// Metrics Consistency Verification
///
/// Feeds the exact ContentBlock tree from the Runtime Replay through
/// TurnMetricsCollector::analyze() and verifies:
/// 1. M1 captures "Let me" patterns visible in the tree
/// 2. M2 captures AnswerText -> CollapsedGroup adjacency
/// 3. Final AnswerText is NOT counted as inter-tool
/// 4. combined_text contains all AnswerText content

#include <catch2/catch_test_macros.hpp>
#include "claude/metrics/TurnMetricsCollector.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>

namespace claude {
namespace fs = std::filesystem;

static ContentBlock makeAnswerText(const std::string& text) {
    ContentBlock cb;
    cb.type = ContentBlock::AnswerText;
    cb.text = text;
    return cb;
}

static ContentBlock makeCollapsedGroup(const std::string& summary, int childCount) {
    ContentBlock cb;
    cb.type = ContentBlock::CollapsedGroup;
    cb.summary = ToolResultSummary::success(summary);
    for (int i = 0; i < childCount; i++) {
        ContentBlock child;
        child.type = ContentBlock::ToolResult;
        child.toolName = "Read";
        child.toolCallId = "call_" + std::to_string(i);
        cb.children.push_back(std::move(child));
        cb.toolUseIds.push_back("call_" + std::to_string(i));
    }
    return cb;
}

static ContentBlock makeTurnDuration() {
    ContentBlock cb;
    cb.type = ContentBlock::TurnDuration;
    cb.text = "3.2s · 1.5K tokens";
    return cb;
}

TEST_CASE("M1 captures Let me in final pipeline tree", "[MetricsConsistency]") {
    // Exact tree from Runtime Replay final state
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me search for handleDisplayEvent references."));
    blocks.push_back(makeCollapsedGroup("Read 1 files", 1));
    blocks.push_back(makeAnswerText("Now let me check StreamBuffer.cpp."));
    blocks.push_back(makeCollapsedGroup("Read 1 files", 1));
    blocks.push_back(makeAnswerText("And AnswerPostProcessor.cpp."));
    blocks.push_back(makeCollapsedGroup("Read 1 files", 1));
    blocks.push_back(makeAnswerText(
        "Found handleDisplayEvent called at: FtxuiRepl.cpp:72, "
        "StreamBuffer.cpp:45, and AnswerPostProcessor.cpp:89."));
    blocks.push_back(makeTurnDuration());

    auto tmpDir = fs::temp_directory_path() / "metrics-consistency";
    fs::create_directories(tmpDir);
    auto path = (tmpDir / "verify.jsonl").string();
    std::remove(path.c_str());

    TurnMetricsCollector collector(path);
    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // ===== M1: Let me patterns =====
    // Block [0]: "Let me search for handleDisplayEvent references." → let_me=1
    // Block [2]: "Now let me check StreamBuffer.cpp." → let_me=1, now_sentence=1
    // Block [4]: "And AnswerPostProcessor.cpp." → no meta patterns
    // Block [6]: "Found handleDisplayEvent called at: ..." → no meta patterns
    CHECK(m.metaPatterns.let_me == 2);
    CHECK(m.metaPatterns.now_sentence == 1);
    CHECK(m.metaPatterns.first_sentence == 0);
    CHECK(m.metaPatterns.next_sentence == 0);

    // ===== M2: Inter-tool word entries =====
    // Block [0] → next is CollapsedGroup → inter-tool ✓
    // Block [2] → next is CollapsedGroup → inter-tool ✓
    // Block [4] → next is CollapsedGroup → inter-tool ✓
    // Block [6] → next is TurnDuration → NOT inter-tool ✗
    CHECK(m.interToolWordCounts.size() == 3);

    // Verify the word counts for each inter-tool block
    REQUIRE(m.interToolWordCounts.size() == 3);
    // "Let me search for handleDisplayEvent references." = 6 words
    CHECK(m.interToolWordCounts[0] == 6);
    // "Now let me check StreamBuffer.cpp." = 6 words
    CHECK(m.interToolWordCounts[1] == 6);
    // "And AnswerPostProcessor.cpp." = 3 words
    CHECK(m.interToolWordCounts[2] == 3);

    // ===== M4: Total words =====
    // Block [0]: 6 + [2]: 6 + [4]: 3 + [6]: 14 = 29
    CHECK(m.totalUserWords == 29);

    // ===== M5: Not silent =====
    CHECK(m.isSilentRound == false);

    // ===== M6: Tool call count =====
    // 3 CollapsedGroups, each with 1 toolUseId
    CHECK(m.toolCallCount == 3);

    // ===== Block counts =====
    CHECK(m.answerTextBlocks == 4);
    CHECK(m.collapsedGroupBlocks == 3);
    CHECK(m.toolResultBlocks == 0);  // ToolResults are children of CollapsedGroups

    // ===== Text samples present =====
    CHECK(m.answerTextSamples.size() == 4);

    // ===== combined_text contains "Let me" =====
    CHECK(m.combinedAnswerText.find("Let me") != std::string::npos);
    CHECK(m.combinedAnswerText.find("Now let me") != std::string::npos);

    // ===== JSON serialization contains correct values =====
    auto j = m.toJson();
    CHECK(j["meta_patterns"]["let_me"] == 2);
    CHECK(j["inter_tool_words"]["values"].size() == 3);
    CHECK(j["total_user_words"] == 29);

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  METRICS CONSISTENCY VERIFICATION\n";
    std::cout << "============================================================\n";
    std::cout << "  Input: 4 AnswerText + 3 CollapsedGroup + 1 TurnDuration\n";
    std::cout << "  M1_let_me:          " << m.metaPatterns.let_me << " (expected 2)\n";
    std::cout << "  M1_now_sentence:    " << m.metaPatterns.now_sentence << " (expected 1)\n";
    std::cout << "  M2_entries:         " << m.interToolWordCounts.size() << " (expected 3)\n";
    std::cout << "  M2_values:          [";
    for (size_t i = 0; i < m.interToolWordCounts.size(); i++) {
        if (i > 0) std::cout << ", ";
        std::cout << m.interToolWordCounts[i];
    }
    std::cout << "]\n";
    std::cout << "  M4_total_words:     " << m.totalUserWords << " (expected 29)\n";
    std::cout << "  M6_tool_call_count: " << m.toolCallCount << " (expected 3)\n";
    std::cout << "  combined_text has 'Let me': "
              << (m.combinedAnswerText.find("Let me") != std::string::npos ? "YES" : "NO") << "\n";
    std::cout << "  combined_text has 'Now let me': "
              << (m.combinedAnswerText.find("Now let me") != std::string::npos ? "YES" : "NO") << "\n";
    std::cout << "============================================================\n";

    fs::remove_all(tmpDir);
}

TEST_CASE("M2 correctly skips final AnswerText before TurnDuration", "[MetricsConsistency]") {
    // Edge case: single tool, no inter-tool text
    std::vector<ContentBlock> blocks;
    blocks.push_back(makeAnswerText("Let me read the file."));    // pre-tool → inter-tool
    blocks.push_back(makeCollapsedGroup("Read 1 files", 1));
    blocks.push_back(makeAnswerText("The file contains the main entry point.")); // final → NOT inter-tool
    blocks.push_back(makeTurnDuration());

    TurnMetricsCollector collector("/tmp/m2-edge.jsonl");
    std::remove("/tmp/m2-edge.jsonl");
    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // Only block [0] is inter-tool (next is CollapsedGroup)
    // Block [2] is final answer (next is TurnDuration, not tool-like)
    CHECK(m.interToolWordCounts.size() == 1);
    CHECK(m.interToolWordCounts[0] == 5);  // "Let me read the file." = 5 words

    CHECK(m.metaPatterns.let_me == 1);
    CHECK(m.totalUserWords == 12); // 5 + 7 words
    std::remove("/tmp/m2-edge.jsonl");
}

TEST_CASE("M2 correctly handles AnswerText before ToolGroup and ToolResult", "[MetricsConsistency]") {
    // Verify all tool-like types are recognized
    std::vector<ContentBlock> blocks;

    // AnswerText → ToolResult
    blocks.push_back(makeAnswerText("Before tool result."));
    ContentBlock tr;
    tr.type = ContentBlock::ToolResult;
    tr.toolName = "Edit";
    tr.toolCallId = "e1";
    blocks.push_back(std::move(tr));

    // AnswerText → ToolGroup
    blocks.push_back(makeAnswerText("Before tool group."));
    ContentBlock tg;
    tg.type = ContentBlock::ToolGroup;
    tg.toolName = "Read";
    tg.toolUseIds.push_back("r1");
    blocks.push_back(std::move(tg));

    // AnswerText → CollapsedGroup (already tested above, included for completeness)
    blocks.push_back(makeAnswerText("Before collapsed group."));
    blocks.push_back(makeCollapsedGroup("Read 1 files", 1));

    // AnswerText → ToolProgress (edge case: during streaming)
    blocks.push_back(makeAnswerText("Before tool progress."));
    ContentBlock tp;
    tp.type = ContentBlock::ToolProgress;
    tp.toolName = "Bash";
    tp.toolCallId = "b1";
    blocks.push_back(std::move(tp));

    // Final AnswerText (not inter-tool)
    blocks.push_back(makeAnswerText("Final answer."));
    blocks.push_back(makeTurnDuration());

    TurnMetricsCollector collector("/tmp/m2-all-types.jsonl");
    std::remove("/tmp/m2-all-types.jsonl");
    auto m = collector.analyze(blocks, 0, "test-model", 0);

    // All 4 pre-tool AnswerTexts should be counted as inter-tool
    // The final AnswerText should NOT be counted
    CHECK(m.interToolWordCounts.size() == 4);
    CHECK(m.metaPatterns.let_me == 0);
    std::remove("/tmp/m2-all-types.jsonl");
}

} // namespace claude
