/// Mini Baseline: generates realistic ContentBlock trees for T1-T7 tasks,
/// feeds them through TurnMetricsCollector, and emits a JSONL file for analysis.
///
/// This is NOT a unit test — it's a data-generation harness to validate metric
/// discrimination power before collecting real model data.

#include <catch2/catch_test_macros.hpp>
#include "claude/metrics/TurnMetricsCollector.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>

using namespace claude;
namespace fs = std::filesystem;

// ---- Helper factories ----

static ContentBlock at(const std::string& text) {
    ContentBlock cb;
    cb.type = ContentBlock::AnswerText;
    cb.text = text;
    return cb;
}

static ContentBlock tr(const std::string& name, const std::string& id) {
    ContentBlock cb;
    cb.type = ContentBlock::ToolResult;
    cb.toolName = name;
    cb.toolCallId = id;
    cb.summary = ToolResultSummary::success("Done");
    return cb;
}

static ContentBlock cg(const std::string& name, const std::vector<std::string>& ids) {
    ContentBlock cb;
    cb.type = ContentBlock::CollapsedGroup;
    cb.toolName = name;
    cb.toolUseIds = ids;
    cb.summary = ToolResultSummary::success("Read files");
    return cb;
}

static ContentBlock tg(const std::string& name, const std::vector<std::string>& ids) {
    ContentBlock cb;
    cb.type = ContentBlock::ToolGroup;
    cb.toolName = name;
    cb.toolUseIds = ids;
    cb.summary = ToolResultSummary::success("Done");
    return cb;
}

/// Map task description to realistic ContentBlock patterns.
///
/// Each ""entry"" models a single API round (one AnswerEnd).
/// For multi-round TAOR tasks, the caller chains multiple entries
/// by appending to the block vector with appropriate turnStartIndex.
struct TaskSpec {
    const char* label;
    /// Expected pattern: each inner vector is one API round's new blocks
    std::vector<std::vector<ContentBlock>> rounds;
};

// Patterns based on observed model behavior in claude-code-cpp without prompt alignment.
// These are representative, not exhaustive.

static std::vector<TaskSpec> specs = {
    // ===== T1: Read single file =====
    // Typical: 1 "Let me..." AnswerText → 1 Read → 1 AnswerText summary
    {"T1-1: Read single file (verbose)", {{
        at("Let me read the main.cpp file to understand its structure."),
        tr("Read", "r1"),
        at("The file contains the main entry point that initializes the CLI app, "
           "parses arguments, and dispatches to either FTXUI or ANSI mode based on flags.")
    }}},
    {"T1-2: Read single file (concise)", {{
        at("Let me check the file."),
        tr("Read", "r2"),
        at("main.cpp: CLI entry point, arg parsing, dispatches to FTXUI or ANSI mode.")
    }}},

    // ===== T2: Code search =====
    // Typical: 1 "Let me..." → 1-2 Grep → 1-2 AnswerText with results
    {"T2-1: Grep search (verbose)", {{
        at("Let me search for handleDisplayEvent to find where it's called."),
        tr("Grep", "g1"),
        at("Now let me check the specific call sites."),
        tr("Read", "r3"),
        at("Found 12 call sites of handleDisplayEvent. The main ones are in "
           "FtxuiRepl.cpp:72, StreamBuffer.cpp:45, and AnswerPostProcessor.cpp:89.")
    }}},
    {"T2-2: Grep search (concise)", {{
        at("Searching for handleDisplayEvent call sites."),
        tr("Grep", "g2"),
        at("handleDisplayEvent called at: FtxuiRepl.cpp:72, StreamBuffer.cpp:45, "
           "AnswerPostProcessor.cpp:89.")
    }}},

    // ===== T3: Explain =====
    // Typical: 0 tools, long AnswerText only
    {"T3-1: Explain (verbose)", {{
        at("Let me explain the ContentBlock type enum and its role in the rendering pipeline. "
           "The ContentBlock tree is a recursive structure where each node represents "
           "a distinct UI element. UserMessage blocks represent input from the user, "
           "AnswerText blocks are the assistant's text response, ThinkingBlock holds "
           "extended reasoning content, ToolProgress shows in-progress tool execution, "
           "ToolResult displays completed results, ToolGroup wraps paired tool-use/result, "
           "AgentProgress shows sub-agent status, ErrorMessage displays errors, "
           "SystemMessage conveys system notices, CompactBoundary marks compaction "
           "boundaries, CollapsedGroup collapses repeated read/search results, and "
           "TurnDuration shows the elapsed time and token count per turn. "
           "Each type maps to a specific FTXUI rendering strategy via ContentBlockRenderer.")
    }}},
    {"T3-2: Explain (concise)", {{
        at("ContentBlock has 12 types: UserMessage, AnswerText, ThinkingBlock, "
           "ToolProgress, ToolResult, ToolGroup, AgentProgress, ErrorMessage, "
           "SystemMessage, CompactBoundary, CollapsedGroup, TurnDuration. "
           "Each maps to a rendering strategy in ContentBlockRenderer.")
    }}},

    // ===== T4: Multi-file edit =====
    // Typical: 2-3 Read/Grep → AnswerText analysis → 1-2 Edit
    {"T4-1: Multi-file edit (medium chain)", {{
        // Round 1: read and search
        at("Let me first read the header to understand the interface."),
        tr("Read", "r4"),
        at("Now let me search for existing callers to understand the pattern."),
        tr("Grep", "g3"),
        at("I can see the pattern now. Let me add the getter."),
        tr("Edit", "e1"),
        at("Added the getter to FtxuiRepl.hpp. Let me verify it compiles.")
    }}},
    {"T4-2: Multi-file edit (efficient)", {{
        at("Looking up FtxuiRepl interface."),
        tr("Read", "r5"),
        at("Adding getter."),
        tr("Edit", "e2"),
        at("Done. Added getCwd() to FtxuiRepl.hpp line 105.")
    }}},

    // ===== T5: Bug investigation =====
    // Typical: Long chain of tools with minimal text between them
    {"T5-1: Bug hunt (long chain)", {{
        // Round 1: initial search
        at("Let me investigate the refreshThread_ crash. First, let me find all "
           "places where refreshThread_ is accessed."),
        tr("Grep", "g4"),
        at("Found 8 references. Let me check the stop sequence."),
        tr("Read", "r6"),
        at("I need to check if the atomic flag is correctly ordered."),
        tr("Read", "r7"),
        at("Let me also check for potential race conditions in the refresh loop."),
        tr("Read", "r8"),
        at("Now let me look at how the thread is joined during shutdown."),
        tr("Grep", "g5"),
        at("I found the issue: stopRefreshThread sets refreshActive_ before join, "
           "but refreshLoop checks refreshActive_ without a mutex. The fix is to "
           "add a mutex guard in the loop.")
    }}},
    {"T5-2: Bug hunt (efficient)", {{
        at("Investigating refreshThread_ crash."),
        tr("Grep", "g6"),
        at("Race in stopRefreshThread: refreshActive_ lacks mutex guard."),
        tr("Read", "r9"),
        at("Confirmed. Fix: add lock_guard to refreshLoop.")
    }}},

    // ===== T6: Concept explanation =====
    // Typical: 0-1 tool (maybe reading docs), then long text
    {"T6-1: Concept (verbose)", {{
        at("Let me find the design document for reference first."),
        tr("Read", "r10"),
        at("The MessagePipeline processes ContentBlocks through 7 numbered passes. "
           "Pass 1 (reorderToolTrails) moves trailing ToolProgress blocks next to "
           "their preceding ToolResult. Pass 2 (groupToolResultPairs) pairs adjacent "
           "ToolProgress+ToolResult into ToolGroup nodes. Pass 3 (groupConsecutiveToolUses) "
           "groups consecutive ToolResult blocks of the same type. Pass 4 "
           "(collapseReadSearchGroups) collapses groups of Read/Grep/Glob into "
           "CollapsedGroup blocks with summary text. Passes 5-7 are stubs for "
           "future use. The incremental variant (runIncrementalPipeline) only "
           "processes blocks from lastStableIndex_ onwards, avoiding full re-scan. "
           "This design mirrors the TS version's useMemo approach where grouping "
           "and collapsing are applied as pure transformations on the block tree.")
    }}},
    {"T6-2: Concept (concise)", {{
        at("MessagePipeline: 7 passes. Pass1 reorder, Pass2 pair, Pass3 group "
           "consecutive, Pass4 collapse read/search groups, Pass5-7 stubs. "
           "Incremental mode processes only unstable tail for performance.")
    }}},

    // ===== T7: Long chain investigation =====
    // Typical: Many tool calls with minimal inter-tool text
    {"T7-1: Full trace (long chain, verbose)", {{
        at("Let me trace the full pipeline from user input to FTXUI rendering. "
           "First, let me find the entry point."),
        tr("Read", "r11"),
        at("I found the handleDisplayEvent dispatch. Let me trace into it."),
        tr("Read", "r12"),
        at("Now let me follow the ContentBlock creation path."),
        tr("Read", "r13"),
        at("I see the blocks being created. Let me trace the pipeline passes."),
        tr("Read", "r14"),
        at("Now let me look at how ContentBlockRenderer maps types to "
           "FTXUI elements."),
        tr("Read", "r15"),
        at("Let me also check the incremental pipeline trigger."),
        tr("Grep", "g7"),
        at("And the stable index advancement logic."),
        tr("Read", "r16"),
        at("Finally, let me verify the rendering output."),
        tr("Read", "r17"),
        at("The full chain is: User input → AgentLoop::runStreaming → "
           "AnthropicClient SSE → TypedStreamEvent → StreamBuffer → "
           "DisplayEvent → FtxuiRepl::handleDisplayEvent → ContentBlock → "
           "incremental MessagePipeline → ContentBlockRenderer → FTXUI element. "
           "Each stage has specific threading and synchronization requirements.")
    }}},
    {"T7-2: Full trace (long chain, efficient)", {{
        at("Tracing user-input to FTXUI."),
        tr("Read", "r18"),
        at("AgentLoop → AnthropicClient SSE."),
        tr("Read", "r19"),
        at("StreamBuffer → DisplayEvent."),
        tr("Read", "r20"),
        at("FtxuiRepl → MessagePipeline → ContentBlockRenderer → FTXUI."),
        tr("Grep", "g8"),
        at("Full chain: 9 layers, each with specific threading requirements.")
    }}},
};

TEST_CASE("Mini Baseline: T1-T7 × 2, produce JSONL for analysis", "[mini-baseline]") {
    auto tmp = fs::temp_directory_path() / "mini-baseline.jsonl";
    TurnMetricsCollector collector(tmp.string());

    std::cout << "\n========== MINI BASELINE ==========\n";
    std::cout << "Task                       M4_w   M2_v  M2_med  M6_den  silent\n";
    std::cout << "----                       ----   ----  ------  ------  ------\n";

    for (auto& spec : specs) {
        // Build cumulative blocks across rounds, simulating real turn boundaries
        std::vector<ContentBlock> blocks;
        size_t roundStart = 0;

        for (size_t ri = 0; ri < spec.rounds.size(); ++ri) {
            for (auto& b : spec.rounds[ri]) {
                blocks.push_back(b);
            }

            auto m = collector.analyze(blocks, roundStart, "test-model", 0);

            // Compute derived stats
            double m2_median = 0;
            if (!m.interToolWordCounts.empty()) {
                auto sorted = m.interToolWordCounts;
                std::sort(sorted.begin(), sorted.end());
                m2_median = sorted[sorted.size() / 2];
            }
            double m6 = static_cast<double>(m.toolCallCount) / (m.totalUserWords + 50);

            std::cout << spec.label
                      << "  " << m.totalUserWords
                      << "     " << m.interToolWordCounts.size()
                      << "     " << m2_median
                      << "     " << std::fixed << std::setprecision(4) << m6
                      << "  " << (m.isSilentRound ? "Y" : "N") << "\n";

            collector.write(m);
            roundStart = blocks.size();
        }
    }

    std::cout << "\nJSONL written to: " << tmp << "\n";

    // Dump the JSONL for inspection
    std::cout << "\n========== SAMPLE RECORDS (first 5) ==========\n";
    std::ifstream f(tmp);
    std::string line;
    int n = 0;
    while (std::getline(f, line) && n < 5) {
        auto j = nlohmann::json::parse(line);
        std::cout << "\n--- Record " << n << " ---\n";
        std::cout << "  user_turn_index: " << j["user_turn_index"] << "\n";
        std::cout << "  snapshot_index:  " << j["snapshot_index"] << "\n";
        std::cout << "  prompt_version: " << j["prompt_version"] << "\n";
        std::cout << "  model:          " << j["model"] << "\n";
        std::cout << "  M4 words:       " << j["total_user_words"] << "\n";
        std::cout << "  M2 values:      " << j["inter_tool_words"]["values"] << "\n";
        std::cout << "  M2 median:      " << j["inter_tool_words"]["median"] << "\n";
        std::cout << "  M6 density:     " << j["m6_density"] << "\n";
        std::cout << "  silent:         " << j["is_silent_round"] << "\n";
        std::cout << "  tool_calls:     " << j["tool_call_count"] << "\n";
        std::cout << "  combined_text:  \""
                  << std::string(j["combined_text"]).substr(0, 90) << "...\"\n";
        n++;
    }
    f.close();

    // Keep the file for manual inspection
    std::cout << "\nFile preserved at: " << tmp << "\n";
    CHECK(fs::exists(tmp));
}

/// Cross-task comparison: verify metric discrimination
TEST_CASE("Mini Baseline: cross-task metric discrimination", "[mini-baseline][analysis]") {
    auto tmp = fs::temp_directory_path() / "mini-baseline-analysis.jsonl";
    TurnMetricsCollector collector(tmp.string());

    struct Result { std::string label; TurnMetrics m; };
    std::vector<Result> results;

    for (auto& spec : specs) {
        std::vector<ContentBlock> blocks;
        size_t roundStart = 0;
        for (size_t ri = 0; ri < spec.rounds.size(); ++ri) {
            for (auto& b : spec.rounds[ri]) blocks.push_back(b);
            auto m = collector.analyze(blocks, roundStart, "test-model", 0);
            results.push_back({spec.label, m});
            roundStart = blocks.size();
        }
    }

    // ---- Discrimination analysis ----

    // M2: inter-tool word count should be higher for verbose patterns
    auto findResult = [&](const std::string& label) -> const TurnMetrics* {
        for (auto& r : results)
            if (r.label == label) return &r.m;
        return nullptr;
    };

    auto m2EntryCount = [](const TurnMetrics& m) -> size_t {
        return m.interToolWordCounts.size();
    };

    // T1 verbose vs concise: 1 inter-tool chunk (only pre-tool, not post-tool summary)
    auto* t1v = findResult("T1-1: Read single file (verbose)");
    auto* t1c = findResult("T1-2: Read single file (concise)");
    REQUIRE(t1v); REQUIRE(t1c);
    CHECK(m2EntryCount(*t1v) == 1);
    CHECK(m2EntryCount(*t1c) == 1);
    // Both have 1 chunk, but verbose has MORE words in that chunk
    CHECK(t1v->interToolWordCounts[0] > t1c->interToolWordCounts[0]);

    // T5 (long chain): verbose has more inter-tool chunks than concise
    auto* t5v = findResult("T5-1: Bug hunt (long chain)");
    auto* t5c = findResult("T5-2: Bug hunt (efficient)");
    REQUIRE(t5v); REQUIRE(t5c);
    CHECK(m2EntryCount(*t5v) >= 4);   // verbose: many inter-tool texts
    CHECK(m2EntryCount(*t5c) <= 2);   // efficient: fewer inter-tool texts
    CHECK(m2EntryCount(*t5v) > m2EntryCount(*t5c));

    // T3 (explain) has 0 inter-tool entries (no tools at all)
    auto* t3v = findResult("T3-1: Explain (verbose)");
    auto* t3c = findResult("T3-2: Explain (concise)");
    REQUIRE(t3v); REQUIRE(t3c);
    CHECK(m2EntryCount(*t3v) == 0);
    CHECK(m2EntryCount(*t3c) == 0);

    // M6: explain tasks → density ≈ 0; tool-heavy tasks → higher density
    double t3v_m6 = static_cast<double>(t3v->toolCallCount) / (t3v->totalUserWords + 50);
    double t5v_m6 = static_cast<double>(t5v->toolCallCount) / (t5v->totalUserWords + 50);
    double t7v_m6 = static_cast<double>(
        findResult("T7-1: Full trace (long chain, verbose)")->toolCallCount) /
        (findResult("T7-1: Full trace (long chain, verbose)")->totalUserWords + 50);

    CHECK(t3v_m6 < 0.01);   // explain: near zero
    CHECK(t5v_m6 > t3v_m6); // bug hunt has tools
    CHECK(t7v_m6 > t3v_m6); // trace has tools

    // M6 discrimination: efficient versions should have SIMILAR density
    // (tool count and word count both decrease)
    double t5c_m6 = static_cast<double>(t5c->toolCallCount) / (t5c->totalUserWords + 50);
    // The efficient version has fewer tools AND fewer words — density may go either way
    // This is the normalization issue: M6 punishes wordy explanations, rewards wordy tool chains

    // M4: explain tasks have highest word count
    CHECK(t3v->totalUserWords > t1v->totalUserWords);
    CHECK(t3v->totalUserWords > t5v->totalUserWords);

    // Silent rounds: none expected (all have text)
    for (auto& r : results) {
        CHECK(r.m.isSilentRound == false);
    }

    std::cout << "\n========== METRIC DISCRIMINATION SUMMARY ==========\n";
    std::cout << "T3 (explain verbose): M4=" << t3v->totalUserWords
              << " M2_v=" << m2EntryCount(*t3v)
              << " M6=" << std::fixed << std::setprecision(4) << t3v_m6 << "\n";
    std::cout << "T5 (bug verbose):    M4=" << t5v->totalUserWords
              << " M2_v=" << m2EntryCount(*t5v)
              << " M6=" << std::fixed << std::setprecision(4) << t5v_m6 << "\n";
    auto* t7v_ptr = findResult("T7-1: Full trace (long chain, verbose)");
    auto* t7c_ptr = findResult("T7-2: Full trace (long chain, efficient)");
    REQUIRE(t7v_ptr); REQUIRE(t7c_ptr);

    std::cout << "T7 (trace verbose):  M4=" << t7v_ptr->totalUserWords
              << " M2_v=" << m2EntryCount(*t7v_ptr)
              << " M6=" << std::fixed << std::setprecision(4) << t7v_m6 << "\n";
    std::cout << "T5 (bug efficient):  M4=" << t5c->totalUserWords
              << " M2_v=" << m2EntryCount(*t5c)
              << " M6=" << std::fixed << std::setprecision(4) << t5c_m6 << "\n";
    double t7c_m6 = static_cast<double>(t7c_ptr->toolCallCount) /
                    (t7c_ptr->totalUserWords + 50);
    std::cout << "T7 (trace efficient): M4=" << t7c_ptr->totalUserWords
              << " M2_v=" << m2EntryCount(*t7c_ptr)
              << " M6=" << std::fixed << std::setprecision(4) << t7c_m6 << "\n";

    fs::remove(tmp);
}
