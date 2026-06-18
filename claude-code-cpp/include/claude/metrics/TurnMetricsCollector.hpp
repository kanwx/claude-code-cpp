#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <nlohmann/json.hpp>

namespace claude {

struct ContentBlock;

/// Meta-narrative pattern counts (M1 — diagnostic)
struct TurnMetaPatterns {
    // English patterns
    int let_me = 0;          // "Let me..."
    int ill = 0;              // "I'll..." or "I will..."
    int first_sentence = 0;   // sentence starting with "First," / "Firstly,"
    int now_sentence = 0;     // sentence starting with "Now," / "Now let me"
    int next_sentence = 0;    // sentence starting with "Next,"
    int need_should = 0;      // "I need to" / "I should"
    int lets = 0;             // "Let's"

    // Chinese patterns
    int rang_wo = 0;          // "让我" (Let me)
    int wo_lai = 0;           // "我来" (I'll)
    int wo_jiang = 0;         // "我将" (I will)
    int jie_xia_lai = 0;      // "接下来" (Next)
    int xianzai_rangwo = 0;   // "现在让我" (Now let me)
    int jixu_duqu = 0;        // "继续读取" / "继续查看" (Continue reading/viewing)
    int zuihou_zai = 0;       // "最后再" (Finally)
    int yijing_shouji = 0;    // "我已经收集" (I have collected)

    int totalEn() const {
        return let_me + ill + first_sentence + now_sentence +
               next_sentence + need_should + lets;
    }

    int totalZh() const {
        return rang_wo + wo_lai + wo_jiang + jie_xia_lai +
               xianzai_rangwo + jixu_duqu + zuihou_zai + yijing_shouji;
    }

    int total() const {
        return totalEn() + totalZh();
    }

    /// Returns names of patterns that have count > 0.
    std::vector<std::string> activePatterns() const {
        std::vector<std::string> names;
        if (let_me) names.push_back("let_me");
        if (ill) names.push_back("ill");
        if (first_sentence) names.push_back("first");
        if (now_sentence) names.push_back("now");
        if (next_sentence) names.push_back("next");
        if (need_should) names.push_back("need_should");
        if (lets) names.push_back("lets");
        if (rang_wo) names.push_back("rang_wo");
        if (wo_lai) names.push_back("wo_lai");
        if (wo_jiang) names.push_back("wo_jiang");
        if (jie_xia_lai) names.push_back("jie_xia_lai");
        if (xianzai_rangwo) names.push_back("xianzai_rangwo");
        if (jixu_duqu) names.push_back("jixu_duqu");
        if (zuihou_zai) names.push_back("zuihou_zai");
        if (yijing_shouji) names.push_back("yijing_shouji");
        return names;
    }
};

/// Per-inter-tool-block text sample for human inspection.
struct InterToolTextSample {
    int word_count = 0;
    int char_count = 0;
    std::string text;              // first 120 chars
    std::string prev_block_type;
    std::string next_block_type;
    std::vector<std::string> matched_meta_patterns;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["word_count"] = word_count;
        j["char_count"] = char_count;
        j["text"] = text;
        j["prev_block_type"] = prev_block_type;
        j["next_block_type"] = next_block_type;
        j["matched_meta_patterns"] = matched_meta_patterns;
        return j;
    }
};

/// Per-turn (per-API-round) metrics collected at AnswerEnd.
struct TurnMetrics {
    // ---- Turn identification ----
    std::string timestamp;
    std::string promptVersion = "baseline";
    std::string taskId;
    std::string runId;
    int userTurnIndex = 0;    // logical user turn id, incremented only at UserMessage
    int snapshotIndex = 0;    // JSONL line number, incremented at each AnswerEnd write
    int apiRoundIndex = 0;    // which API round within the current user turn (0-based)
    std::string modelId;

    // ---- M1: Meta-narrative patterns (all AnswerText blocks) ----
    TurnMetaPatterns metaPatterns;

    // ---- M1b: Meta-narrative patterns (inter-tool AnswerText only) ----
    // Only blocks immediately followed by a ToolResult/ToolGroup/CollapsedGroup.
    TurnMetaPatterns metaPatternsInterTool;

    // ---- M2: Inter-tool word statistics ----
    std::vector<int> interToolWordCounts;

    // ---- M2c: Inter-tool char statistics (for CJK accuracy) ----
    std::vector<int> interToolCharCounts;

    // ---- M3: Block counts ----
    int answerTextBlocks = 0;
    int toolResultBlocks = 0;
    int toolGroupBlocks = 0;
    int collapsedGroupBlocks = 0;
    int agentProgressBlocks = 0;

    // ---- M4: Total user-visible words ----
    int totalUserWords = 0;

    // ---- M5: Silent round flag (observation only) ----
    bool isSilentRound = false;

    // ---- M6: Tool call density (raw data) ----
    int toolCallCount = 0;       // real tool calls (Read/Grep/Bash/Edit/...)
    int agentProgressCount = 0;  // AgentProgress / Explore Agent blocks
    int toolLikeBlockCount = 0;  // toolCallCount + agentProgressCount (all visible tool-like blocks)

    // ---- Raw text samples (for human verification) ----
    std::vector<std::string> answerTextSamples;  // first 200 chars of each block
    std::string combinedAnswerText;

    // ---- Inter-tool text samples (for debugging inter-tool narration) ----
    std::vector<InterToolTextSample> interToolTextSamples;

    nlohmann::json toJson() const;
};

/// Read-only analyzer that produces TurnMetrics from a ContentBlock tree.
///
/// Used at AnswerEnd to collect per-turn statistics without modifying any
/// rendering state. Writes one JSON line per turn to a JSONL file.
class TurnMetricsCollector {
public:
    /// @param outputPath  Full path to the JSONL output file.
    explicit TurnMetricsCollector(const std::string& outputPath);
    ~TurnMetricsCollector();

    /// Analyze blocks from [turnStartIndex, blocks.size()) and return metrics.
    /// Pure function — no side effects on the block tree.
    TurnMetrics analyze(const std::vector<ContentBlock>& blocks,
                        size_t turnStartIndex,
                        const std::string& modelId,
                        int userTurnIndex);

    /// Append one turn's metrics to the JSONL file. Thread-safe.
    void write(const TurnMetrics& m);

private:
    std::string outputPath_;
    std::mutex writeMutex_;
    std::ofstream file_;
    int snapshotCount_ = 0;

    // ---- Pattern matching helpers ----
    static TurnMetaPatterns countMetaPatterns(const std::string& text);
    static int countWords(const std::string& text);
    static std::string timestampNow();
    static std::string truncateSample(const std::string& text, size_t maxChars = 200);

    // ---- Block type classification ----
    static bool isToolLikeBlock(const ContentBlock& block);
};

} // namespace claude
