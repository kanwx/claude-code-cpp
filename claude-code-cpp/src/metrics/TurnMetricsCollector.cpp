#include "claude/metrics/TurnMetricsCollector.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <regex>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace claude {

namespace fs = std::filesystem;

// ========== TurnMetrics serialization ==========

nlohmann::json TurnMetrics::toJson() const {
    using json = nlohmann::json;
    json j;
    j["ts"] = timestamp;
    j["prompt_version"] = promptVersion;
    j["task_id"] = taskId;
    j["run_id"] = runId;
    j["user_turn_index"] = userTurnIndex;
    j["snapshot_index"] = snapshotIndex;
    j["api_round_index"] = apiRoundIndex;
    j["metric_scope"] = "cumulative_user_turn_snapshot";
    j["model"] = modelId;

    // M1: meta patterns (all AnswerText blocks)
    j["meta_patterns"] = {
        {"let_me", metaPatterns.let_me},
        {"ill", metaPatterns.ill},
        {"first", metaPatterns.first_sentence},
        {"now", metaPatterns.now_sentence},
        {"next", metaPatterns.next_sentence},
        {"need_should", metaPatterns.need_should},
        {"lets", metaPatterns.lets},
        {"rang_wo", metaPatterns.rang_wo},
        {"wo_lai", metaPatterns.wo_lai},
        {"wo_jiang", metaPatterns.wo_jiang},
        {"jie_xia_lai", metaPatterns.jie_xia_lai},
        {"xianzai_rangwo", metaPatterns.xianzai_rangwo},
        {"jixu_duqu", metaPatterns.jixu_duqu},
        {"zuihou_zai", metaPatterns.zuihou_zai},
        {"yijing_shouji", metaPatterns.yijing_shouji},
        {"total_en", metaPatterns.totalEn()},
        {"total_zh", metaPatterns.totalZh()},
        {"total", metaPatterns.total()}
    };

    // M1 inter-tool: only blocks immediately followed by a tool block
    j["meta_patterns_inter_tool"] = {
        {"let_me", metaPatternsInterTool.let_me},
        {"ill", metaPatternsInterTool.ill},
        {"first", metaPatternsInterTool.first_sentence},
        {"now", metaPatternsInterTool.now_sentence},
        {"next", metaPatternsInterTool.next_sentence},
        {"need_should", metaPatternsInterTool.need_should},
        {"lets", metaPatternsInterTool.lets},
        {"rang_wo", metaPatternsInterTool.rang_wo},
        {"wo_lai", metaPatternsInterTool.wo_lai},
        {"wo_jiang", metaPatternsInterTool.wo_jiang},
        {"jie_xia_lai", metaPatternsInterTool.jie_xia_lai},
        {"xianzai_rangwo", metaPatternsInterTool.xianzai_rangwo},
        {"jixu_duqu", metaPatternsInterTool.jixu_duqu},
        {"zuihou_zai", metaPatternsInterTool.zuihou_zai},
        {"yijing_shouji", metaPatternsInterTool.yijing_shouji},
        {"total_en", metaPatternsInterTool.totalEn()},
        {"total_zh", metaPatternsInterTool.totalZh()},
        {"total", metaPatternsInterTool.total()}
    };

    // M1 final-answer: computed = total - inter_tool
    // (blocks NOT immediately followed by a tool block)
    {
        auto faLetMe = metaPatterns.let_me - metaPatternsInterTool.let_me;
        auto faIll = metaPatterns.ill - metaPatternsInterTool.ill;
        auto faFirst = metaPatterns.first_sentence - metaPatternsInterTool.first_sentence;
        auto faNow = metaPatterns.now_sentence - metaPatternsInterTool.now_sentence;
        auto faNext = metaPatterns.next_sentence - metaPatternsInterTool.next_sentence;
        auto faNeed = metaPatterns.need_should - metaPatternsInterTool.need_should;
        auto faLets = metaPatterns.lets - metaPatternsInterTool.lets;
        auto faRangWo = metaPatterns.rang_wo - metaPatternsInterTool.rang_wo;
        auto faWoLai = metaPatterns.wo_lai - metaPatternsInterTool.wo_lai;
        auto faWoJiang = metaPatterns.wo_jiang - metaPatternsInterTool.wo_jiang;
        auto faJieXiaLai = metaPatterns.jie_xia_lai - metaPatternsInterTool.jie_xia_lai;
        auto faXianzai = metaPatterns.xianzai_rangwo - metaPatternsInterTool.xianzai_rangwo;
        auto faJixu = metaPatterns.jixu_duqu - metaPatternsInterTool.jixu_duqu;
        auto faZuihou = metaPatterns.zuihou_zai - metaPatternsInterTool.zuihou_zai;
        auto faYijing = metaPatterns.yijing_shouji - metaPatternsInterTool.yijing_shouji;
        int faTotalEn = faLetMe + faIll + faFirst + faNow + faNext + faNeed + faLets;
        int faTotalZh = faRangWo + faWoLai + faWoJiang + faJieXiaLai + faXianzai + faJixu + faZuihou + faYijing;

        j["meta_patterns_final_answer"] = {
            {"let_me", faLetMe},
            {"ill", faIll},
            {"first", faFirst},
            {"now", faNow},
            {"next", faNext},
            {"need_should", faNeed},
            {"lets", faLets},
            {"rang_wo", faRangWo},
            {"wo_lai", faWoLai},
            {"wo_jiang", faWoJiang},
            {"jie_xia_lai", faJieXiaLai},
            {"xianzai_rangwo", faXianzai},
            {"jixu_duqu", faJixu},
            {"zuihou_zai", faZuihou},
            {"yijing_shouji", faYijing},
            {"total_en", faTotalEn},
            {"total_zh", faTotalZh},
            {"total", faTotalEn + faTotalZh}
        };
    }

    // M2 statistics
    if (!interToolWordCounts.empty()) {
        json m2;
        m2["values"] = interToolWordCounts;
        // Compute median inline for quick scanning
        auto sorted = interToolWordCounts;
        std::sort(sorted.begin(), sorted.end());
        if (!sorted.empty()) m2["median"] = sorted[sorted.size() / 2];
        if (sorted.size() >= 10) {
            m2["p90"] = sorted[static_cast<size_t>(sorted.size() * 0.9)];
            m2["p25"] = sorted[static_cast<size_t>(sorted.size() * 0.25)];
            m2["p75"] = sorted[static_cast<size_t>(sorted.size() * 0.75)];
        }
        j["inter_tool_words"] = m2;
    } else {
        j["inter_tool_words"] = json::object();
    }

    // M2c: inter-tool char counts (parallel to M2, for CJK accuracy)
    if (!interToolCharCounts.empty()) {
        json m2c;
        m2c["values"] = interToolCharCounts;
        auto sorted = interToolCharCounts;
        std::sort(sorted.begin(), sorted.end());
        if (!sorted.empty()) m2c["median"] = sorted[sorted.size() / 2];
        if (sorted.size() >= 10) {
            m2c["p90"] = sorted[static_cast<size_t>(sorted.size() * 0.9)];
            m2c["p25"] = sorted[static_cast<size_t>(sorted.size() * 0.25)];
            m2c["p75"] = sorted[static_cast<size_t>(sorted.size() * 0.75)];
        }
        j["inter_tool_chars"] = m2c;
    } else {
        j["inter_tool_chars"] = json::object();
    }

    // M3
    j["block_counts"] = {
        {"answer_text", answerTextBlocks},
        {"tool_result", toolResultBlocks},
        {"tool_group", toolGroupBlocks},
        {"collapsed_group", collapsedGroupBlocks},
        {"agent_progress", agentProgressBlocks}
    };

    // M4
    j["total_user_words"] = totalUserWords;

    // M5
    j["is_silent_round"] = isSilentRound;

    // M6: tool call counts — real tools vs agent dispatch
    j["tool_call_count"] = toolCallCount;
    j["agent_progress_count"] = agentProgressCount;
    j["tool_like_block_count"] = toolLikeBlockCount;
    // Density = tool_calls / (totalUserWords + 50), +50 to avoid div-by-zero amplification
    j["m6_density"] = static_cast<double>(toolCallCount) / (totalUserWords + 50);

    // Text samples (first 200 chars of each block) — always present for uniform schema
    j["text_samples"] = answerTextSamples;

    // Full combined text (for human verification)
    j["combined_text"] = combinedAnswerText;

    // Inter-tool text samples (for debugging inter-tool narration)
    if (!interToolTextSamples.empty()) {
        nlohmann::json its = nlohmann::json::array();
        for (auto& s : interToolTextSamples) {
            its.push_back(s.toJson());
        }
        j["inter_tool_text_samples"] = its;
    } else {
        j["inter_tool_text_samples"] = nlohmann::json::array();
    }

    return j;
}

// ========== TurnMetricsCollector ==========

TurnMetricsCollector::TurnMetricsCollector(const std::string& outputPath)
    : outputPath_(outputPath) {
    fs::path p(outputPath_);
    fs::create_directories(p.parent_path());
    spdlog::info("TurnMetricsCollector: writing to {}", outputPath_);

    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
    if (debugMetrics) {
        fprintf(stderr, "[DEBUG_METRICS] TurnMetricsCollector::ctor: outputPath=%s, parent_dir=%s, dir_created=%s\n",
                outputPath_.c_str(),
                p.parent_path().string().c_str(),
                fs::exists(p.parent_path()) ? "true" : "false");
    }
}

TurnMetricsCollector::~TurnMetricsCollector() {
    if (file_.is_open()) {
        file_.close();
    }
}

TurnMetrics TurnMetricsCollector::analyze(
    const std::vector<ContentBlock>& blocks,
    size_t turnStartIndex,
    const std::string& modelId,
    int userTurnIndex) {

    TurnMetrics m;
    m.timestamp = timestampNow();
    m.snapshotIndex = snapshotCount_++;
    m.modelId = modelId;
    m.userTurnIndex = userTurnIndex;

    // Experiment metadata from environment
    if (const char* v = std::getenv("CLAUDE_CODE_METRICS_PROMPT_VARIANT")) {
        m.promptVersion = v;
    }
    if (const char* v = std::getenv("CLAUDE_CODE_METRICS_TASK_ID")) {
        m.taskId = v;
    }
    if (const char* v = std::getenv("CLAUDE_CODE_METRICS_RUN_ID")) {
        m.runId = v;
    }

    if (turnStartIndex >= blocks.size()) {
        m.isSilentRound = true;
        return m;
    }

    // Collect inter-tool text blocks
    for (size_t i = turnStartIndex; i < blocks.size(); ++i) {
        const auto& block = blocks[i];

        switch (block.type) {
            case ContentBlock::AnswerText: {
                if (!block.text.empty()) {
                    int wc = countWords(block.text);
                    m.answerTextBlocks++;
                    m.totalUserWords += wc;

                    // Accumulate meta pattern counts
                    auto patterns = countMetaPatterns(block.text);
                    m.metaPatterns.let_me += patterns.let_me;
                    m.metaPatterns.ill += patterns.ill;
                    m.metaPatterns.first_sentence += patterns.first_sentence;
                    m.metaPatterns.now_sentence += patterns.now_sentence;
                    m.metaPatterns.next_sentence += patterns.next_sentence;
                    m.metaPatterns.need_should += patterns.need_should;
                    m.metaPatterns.lets += patterns.lets;
                    m.metaPatterns.rang_wo += patterns.rang_wo;
                    m.metaPatterns.wo_lai += patterns.wo_lai;
                    m.metaPatterns.wo_jiang += patterns.wo_jiang;
                    m.metaPatterns.jie_xia_lai += patterns.jie_xia_lai;
                    m.metaPatterns.xianzai_rangwo += patterns.xianzai_rangwo;
                    m.metaPatterns.jixu_duqu += patterns.jixu_duqu;
                    m.metaPatterns.zuihou_zai += patterns.zuihou_zai;
                    m.metaPatterns.yijing_shouji += patterns.yijing_shouji;

                    // Inter-tool text: an AnswerText is inter-tool iff a tool-related
                    // block immediately follows it within the same round.
                    // "Preceded by tool" is NOT used — it would falsely count the
                    // final AnswerText after all tools as inter-tool narration.
                    bool isInterTool = false;
                    if (i + 1 < blocks.size() && isToolLikeBlock(blocks[i + 1])) {
                        isInterTool = true;
                    }
                    if (isInterTool) {
                        int cc = static_cast<int>(block.text.size());
                        m.interToolWordCounts.push_back(wc);
                        m.interToolCharCounts.push_back(cc);
                        // Also accumulate inter-tool meta patterns separately
                        m.metaPatternsInterTool.let_me += patterns.let_me;
                        m.metaPatternsInterTool.ill += patterns.ill;
                        m.metaPatternsInterTool.first_sentence += patterns.first_sentence;
                        m.metaPatternsInterTool.now_sentence += patterns.now_sentence;
                        m.metaPatternsInterTool.next_sentence += patterns.next_sentence;
                        m.metaPatternsInterTool.need_should += patterns.need_should;
                        m.metaPatternsInterTool.lets += patterns.lets;
                        m.metaPatternsInterTool.rang_wo += patterns.rang_wo;
                        m.metaPatternsInterTool.wo_lai += patterns.wo_lai;
                        m.metaPatternsInterTool.wo_jiang += patterns.wo_jiang;
                        m.metaPatternsInterTool.jie_xia_lai += patterns.jie_xia_lai;
                        m.metaPatternsInterTool.xianzai_rangwo += patterns.xianzai_rangwo;
                        m.metaPatternsInterTool.jixu_duqu += patterns.jixu_duqu;
                        m.metaPatternsInterTool.zuihou_zai += patterns.zuihou_zai;
                        m.metaPatternsInterTool.yijing_shouji += patterns.yijing_shouji;

                        // Capture text sample for human inspection
                        InterToolTextSample sample;
                        sample.word_count = wc;
                        sample.char_count = cc;
                        sample.text = block.text.size() > 120
                            ? block.text.substr(0, 117) + "..." : block.text;
                        sample.prev_block_type = (i > 0)
                            ? ContentBlock::typeName(blocks[i - 1].type) : "none";
                        sample.next_block_type = ContentBlock::typeName(blocks[i + 1].type);
                        sample.matched_meta_patterns = patterns.activePatterns();
                        m.interToolTextSamples.push_back(std::move(sample));
                    }

                    // Text sampling
                    m.answerTextSamples.push_back(truncateSample(block.text));
                }
                break;
            }

            case ContentBlock::ToolResult:
                m.toolResultBlocks++;
                break;

            case ContentBlock::ToolGroup:
                m.toolGroupBlocks++;
                break;

            case ContentBlock::CollapsedGroup:
                m.collapsedGroupBlocks++;
                break;

            case ContentBlock::AgentProgress:
                m.agentProgressBlocks++;
                break;

            default:
                break;
        }
    }

    // Count real tool calls (Read/Grep/Bash/Edit/WebSearch etc.)
    // AgentProgress is counted separately — it represents sub-agent dispatch,
    // not a direct tool call.
    for (size_t i = turnStartIndex; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        if (block.type == ContentBlock::ToolResult && !block.toolCallId.empty()) {
            m.toolCallCount++;
        } else if (block.type == ContentBlock::ToolGroup ||
                   block.type == ContentBlock::CollapsedGroup) {
            m.toolCallCount += static_cast<int>(block.toolUseIds.size());
        } else if (block.type == ContentBlock::AgentProgress) {
            m.agentProgressCount++;
        }
    }

    // Tool-like block count: real tools + sub-agent dispatch
    m.toolLikeBlockCount = m.toolCallCount + m.agentProgressCount;

    m.isSilentRound = (m.answerTextBlocks == 0);

    // Combined text for human verification
    if (!m.answerTextSamples.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < m.answerTextSamples.size(); ++i) {
            if (i > 0) oss << "\n---\n";
            oss << m.answerTextSamples[i];
        }
        m.combinedAnswerText = oss.str();
    }

    return m;
}

void TurnMetricsCollector::write(const TurnMetrics& m) {
    std::lock_guard lock(writeMutex_);

    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');

    if (!file_.is_open()) {
        if (debugMetrics) {
            fprintf(stderr, "[DEBUG_METRICS] TurnMetricsCollector::write: opening file %s (first write, user_turn=%d snapshot=%d)\n",
                    outputPath_.c_str(), m.userTurnIndex, m.snapshotIndex);
        }
        file_.open(outputPath_, std::ios::app);
        if (!file_.is_open()) {
            spdlog::error("TurnMetricsCollector: failed to open {}", outputPath_);
            if (debugMetrics) {
                fprintf(stderr, "[DEBUG_METRICS] TurnMetricsCollector::write: FAILED to open %s (errno=%d, %s)\n",
                        outputPath_.c_str(), errno, strerror(errno));
            }
            return;
        }
    }

    file_ << m.toJson().dump() << "\n";
    file_.flush();  // flush each line for crash resilience

    if (debugMetrics) {
        fprintf(stderr, "[DEBUG_METRICS] TurnMetricsCollector::write: written user_turn=%d snapshot=%d to %s, file_size=%lld\n",
                m.userTurnIndex, m.snapshotIndex, outputPath_.c_str(),
                (long long)fs::file_size(outputPath_));
    }
}

// ========== Private helpers ==========

TurnMetaPatterns TurnMetricsCollector::countMetaPatterns(const std::string& text) {
    TurnMetaPatterns p;

    // Case-insensitive matching throughout.

    // "Let me" — match "let me" as a phrase
    {
        std::regex re(R"(\blet me\b)", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.let_me = static_cast<int>(std::distance(it, it_end));
    }

    // "I'll" or "I will"
    {
        std::regex re(R"(\bi'll\b|\bi will\b)", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.ill = static_cast<int>(std::distance(it, it_end));
    }

    // "First," / "Firstly" — only at sentence start
    {
        std::regex re(R"((?:^|[.!?]\s+)[Ff]irst[,\s])", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.first_sentence = static_cast<int>(std::distance(it, it_end));
    }

    // "Now," / "Now let me" — only at sentence start
    {
        std::regex re(R"((?:^|[.!?]\s+)[Nn]ow[\s,])", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.now_sentence = static_cast<int>(std::distance(it, it_end));
    }

    // "Next," — only at sentence start
    {
        std::regex re(R"((?:^|[.!?]\s+)[Nn]ext[\s,])", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.next_sentence = static_cast<int>(std::distance(it, it_end));
    }

    // "I need to" or "I should"
    {
        std::regex re(R"(\bi need to\b|\bi should\b)", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.need_should = static_cast<int>(std::distance(it, it_end));
    }

    // "Let's"
    {
        std::regex re(R"(\blet's\b)", std::regex::icase);
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.lets = static_cast<int>(std::distance(it, it_end));
    }

    // ---- Chinese patterns ----

    // "让我" (Let me)
    {
        std::regex re(R"(让我)");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.rang_wo = static_cast<int>(std::distance(it, it_end));
    }

    // "我来" (I'll / Let me)
    {
        std::regex re(R"(我来)");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.wo_lai = static_cast<int>(std::distance(it, it_end));
    }

    // "我将" (I will)
    {
        std::regex re(R"(我将)");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.wo_jiang = static_cast<int>(std::distance(it, it_end));
    }

    // "接下来" (Next)
    {
        std::regex re(R"(接下来)");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.jie_xia_lai = static_cast<int>(std::distance(it, it_end));
    }

    // "现在让我" (Now let me)
    {
        std::regex re(R"(现在让我)");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.xianzai_rangwo = static_cast<int>(std::distance(it, it_end));
    }

    // "继续读取|继续查看|继续检查|继续分析|继续探索" (Continue reading/viewing/...)
    {
        std::regex re(R"(继续(读取|查看|检查|分析|探索|研究|深入))");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.jixu_duqu = static_cast<int>(std::distance(it, it_end));
    }

    // "最后再" (Finally)
    {
        std::regex re(R"(最后再)");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.zuihou_zai = static_cast<int>(std::distance(it, it_end));
    }

    // "我已经收集了|我已经收集到|我已经找到了|我已经阅读了" (I have collected/found)
    {
        std::regex re(R"(我已经(收集|找到|阅读|查看|获取|完成))");
        auto it = std::sregex_iterator(text.begin(), text.end(), re);
        auto it_end = std::sregex_iterator();
        p.yijing_shouji = static_cast<int>(std::distance(it, it_end));
    }

    return p;
}

int TurnMetricsCollector::countWords(const std::string& text) {
    int count = 0;
    bool inWord = false;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '\'' || c == '-') {
            if (!inWord) {
                inWord = true;
                ++count;
            }
        } else {
            inWord = false;
        }
    }
    return count;
}

std::string TurnMetricsCollector::timestampNow() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    struct tm tm_buf;
    localtime_r(&now_time_t, &tm_buf);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return std::string(buf);
}

bool TurnMetricsCollector::isToolLikeBlock(const ContentBlock& block) {
    return block.type == ContentBlock::ToolResult ||
           block.type == ContentBlock::ToolProgress ||
           block.type == ContentBlock::ToolGroup ||
           block.type == ContentBlock::CollapsedGroup ||
           block.type == ContentBlock::AgentProgress;
}

std::string TurnMetricsCollector::truncateSample(const std::string& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    // Try to break at a word boundary
    size_t cut = maxChars;
    while (cut > 0 && !std::isspace(static_cast<unsigned char>(text[cut]))) {
        --cut;
    }
    if (cut == 0) cut = maxChars;  // can't find a space, hard cut
    return text.substr(0, cut) + "...";
}

} // namespace claude
