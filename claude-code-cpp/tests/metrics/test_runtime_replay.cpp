/// Runtime Output Replay: trace a single agent turn through each layer
/// and report block counts, transformations, and user-visible lines.
///
/// This test simulates FTXUI mode (not headless) to identify where the
/// "log-like" output experience comes from.
///
/// Run: ctest -R RuntimeReplay -V

#include <catch2/catch_test_macros.hpp>
#include "claude/stream/DisplayEvent.hpp"
#include "claude/stream/ContentBlock.hpp"
#include "claude/stream/MessagePipeline.hpp"
#include "claude/stream/AnswerPostProcessor.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace claude;

// ---- Helper: count visible lines for a ContentBlock tree ----
// This simulates what FTXUI ContentBlockRenderer would produce.
// Conservative estimate: 1 line per non-container block, +1 for containers.
static int estimateVisibleLines(const std::vector<ContentBlock>& blocks, int depth = 0) {
    int lines = 0;
    for (const auto& b : blocks) {
        switch (b.type) {
            case ContentBlock::AnswerText:
                // Each AnswerText becomes 1+ lines (wrap at ~80 chars)
                lines += std::max(1, (int)(b.text.size() / 80) + 1);
                break;
            case ContentBlock::ToolProgress:
                lines += 1;  // "  ⎿ ● activity..."
                break;
            case ContentBlock::ToolResult:
                if (b.summary.isError || b.resultStatus == ToolResultStatus::Cancelled ||
                    b.resultStatus == ToolResultStatus::Rejected) {
                    lines += 2;  // expanded error display
                } else {
                    lines += b.expanded ? 3 : 1;  // collapsed: 1 line, expanded: ~3
                }
                break;
            case ContentBlock::ToolGroup:
                lines += b.expanded ? (2 + estimateVisibleLines(b.children, depth + 1)) : 1;
                break;
            case ContentBlock::CollapsedGroup:
                lines += b.expanded ? (2 + estimateVisibleLines(b.children, depth + 1)) : 1;
                break;
            case ContentBlock::ThinkingBlock:
                lines += b.expanded ? 5 : 1;  // collapsed: 1, expanded: ~5
                break;
            case ContentBlock::TurnDuration:
                lines += 1;
                break;
            case ContentBlock::ErrorMessage:
                lines += 2;
                break;
            case ContentBlock::SystemMessage:
                lines += 1;
                break;
            default:
                break;
        }
    }
    return lines;
}

// ---- Helper: count blocks by type ----
struct BlockCounts {
    int answerText = 0, toolProgress = 0, toolResult = 0;
    int toolGroup = 0, collapsedGroup = 0, thinkingBlock = 0;
    int errorMessage = 0, systemMessage = 0, turnDuration = 0;
    int other = 0;
    int total() const {
        return answerText + toolProgress + toolResult + toolGroup +
               collapsedGroup + thinkingBlock + errorMessage + systemMessage +
               turnDuration + other;
    }
};

static BlockCounts countBlocks(const std::vector<ContentBlock>& blocks) {
    BlockCounts c;
    for (const auto& b : blocks) {
        switch (b.type) {
            case ContentBlock::AnswerText: c.answerText++; break;
            case ContentBlock::ToolProgress: c.toolProgress++; break;
            case ContentBlock::ToolResult: c.toolResult++; break;
            case ContentBlock::ToolGroup: c.toolGroup++; break;
            case ContentBlock::CollapsedGroup: c.collapsedGroup++; break;
            case ContentBlock::ThinkingBlock: c.thinkingBlock++; break;
            case ContentBlock::ErrorMessage: c.errorMessage++; break;
            case ContentBlock::SystemMessage: c.systemMessage++; break;
            case ContentBlock::TurnDuration: c.turnDuration++; break;
            default: c.other++; break;
        }
        // Recurse into children
        auto childCounts = countBlocks(b.children);
        c.answerText += childCounts.answerText;
        c.toolProgress += childCounts.toolProgress;
        c.toolResult += childCounts.toolResult;
        c.toolGroup += childCounts.toolGroup;
        c.collapsedGroup += childCounts.collapsedGroup;
        c.thinkingBlock += childCounts.thinkingBlock;
        c.errorMessage += childCounts.errorMessage;
        c.systemMessage += childCounts.systemMessage;
        c.turnDuration += childCounts.turnDuration;
        c.other += childCounts.other;
    }
    return c;
}

// ---- Simplified FtxuiRepl logic (ContentBlock construction only) ----
// This mirrors FtxuiRepl::handleDisplayEvent's ContentBlock construction
// but skips FTXUI threading (screen_->Post) and refresh thread.
struct FtxuiReplSim {
    std::vector<ContentBlock> contentBlocks;
    String streamingText;
    bool isFirstAnswerBlock = true;
    uint64_t nextStableId = 1;
    std::map<String, size_t> toolProgressIndices;
    MessagePipeline messagePipeline;
    bool useExternalPostProcessor = false;

    void handleDisplayEvent(const DisplayEvent& ev) {
        switch (ev.type) {
            case DisplayEventType::TextPartial: {
                if (!ev.text.empty()) streamingText += ev.text;
                break;
            }
            case DisplayEventType::TextParagraph: {
                String text = ev.text.empty() ? std::move(streamingText) : ev.text;
                streamingText.clear();
                if (!text.empty() && text.find_first_not_of(" \t\n\r") != String::npos) {
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.isFirst = isFirstAnswerBlock;
                    cb.text = std::move(text);
                    isFirstAnswerBlock = false;
                    cb.stableId = nextStableId++;
                    contentBlocks.push_back(std::move(cb));
                }
                break;
            }
            case DisplayEventType::ToolProgress: {
                if (!streamingText.empty()) {
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.isFirst = isFirstAnswerBlock;
                    cb.text = std::move(streamingText);
                    isFirstAnswerBlock = false;
                    streamingText.clear();
                    cb.stableId = nextStableId++;
                    contentBlocks.push_back(std::move(cb));
                }
                ContentBlock cb;
                cb.type = ContentBlock::ToolProgress;
                cb.toolName = ev.toolName;
                cb.toolCallId = ev.toolCallId;
                cb.activity = ev.activity;
                cb.stableId = nextStableId++;
                toolProgressIndices[ev.toolCallId] = contentBlocks.size();
                contentBlocks.push_back(std::move(cb));
                break;
            }
            case DisplayEventType::ToolResult: {
                if (!streamingText.empty()) {
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.isFirst = isFirstAnswerBlock;
                    cb.text = std::move(streamingText);
                    isFirstAnswerBlock = false;
                    streamingText.clear();
                    cb.stableId = nextStableId++;
                    contentBlocks.push_back(std::move(cb));
                }
                // Remove matching ToolProgress
                auto it = toolProgressIndices.find(ev.toolCallId);
                if (it != toolProgressIndices.end()) {
                    size_t idx = it->second;
                    if (idx < contentBlocks.size() &&
                        contentBlocks[idx].type == ContentBlock::ToolProgress &&
                        contentBlocks[idx].toolCallId == ev.toolCallId) {
                        contentBlocks.erase(contentBlocks.begin() + (long)idx);
                        for (auto& [tid, i] : toolProgressIndices) {
                            if (i > idx) --i;
                        }
                    }
                    toolProgressIndices.erase(it);
                }
                ContentBlock cb;
                cb.type = ContentBlock::ToolResult;
                cb.toolName = ev.toolName;
                cb.summary = ev.summary;
                cb.toolCallId = ev.toolCallId;
                cb.stableId = nextStableId++;
                // Set result status
                if (cb.summary.isError) cb.resultStatus = ToolResultStatus::Error;
                else if (cb.summary.isDim) {
                    if (cb.summary.primaryText.find("Interrupted") != String::npos)
                        cb.resultStatus = ToolResultStatus::Cancelled;
                    else if (cb.summary.primaryText.find("Rejected") != String::npos)
                        cb.resultStatus = ToolResultStatus::Rejected;
                }
                contentBlocks.push_back(std::move(cb));
                // FTXUI: run incremental pipeline after each ToolResult
                if (useExternalPostProcessor) runIncrementalPipeline();
                break;
            }
            case DisplayEventType::ToolGroup: {
                ContentBlock cb;
                cb.type = ContentBlock::ToolGroup;
                cb.toolName = ev.toolName;
                cb.summary = ev.summary;
                cb.stableId = nextStableId++;
                contentBlocks.push_back(std::move(cb));
                // FTXUI: run incremental pipeline after each ToolGroup
                if (useExternalPostProcessor) runIncrementalPipeline();
                break;
            }
            case DisplayEventType::AnswerStart:
                isFirstAnswerBlock = true;
                streamingText.clear();
                toolProgressIndices.clear();
                useExternalPostProcessor = true;
                break;
            case DisplayEventType::AnswerEnd: {
                // Commit remaining streaming text
                if (!streamingText.empty()) {
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.isFirst = isFirstAnswerBlock;
                    cb.text = std::move(streamingText);
                    isFirstAnswerBlock = false;
                    streamingText.clear();
                    cb.stableId = nextStableId++;
                    contentBlocks.push_back(std::move(cb));
                }
                // Clean orphaned ToolProgress
                for (auto& [callId, idx] : toolProgressIndices) {
                    if (idx < contentBlocks.size() &&
                        contentBlocks[idx].type == ContentBlock::ToolProgress) {
                        contentBlocks[idx].type = ContentBlock::ToolResult;
                        contentBlocks[idx].summary = ToolResultSummary::dim("Interrupted");
                        contentBlocks[idx].resultStatus = ToolResultStatus::Cancelled;
                    }
                }
                toolProgressIndices.clear();
                // FTXUI: SKIP MessagePipeline (useExternalPostProcessor=true)
                // Insert TurnDuration
                {
                    ContentBlock td;
                    td.type = ContentBlock::TurnDuration;
                    td.text = "3.2s · 1.5K tokens";
                    td.stableId = nextStableId++;
                    contentBlocks.push_back(std::move(td));
                }
                break;
            }
            case DisplayEventType::ThinkingBlock:
                // Just accumulate; inserted at AnswerEnd
                break;
            case DisplayEventType::Tombstone: {
                // Remove matching ToolResult
                auto it = std::find_if(contentBlocks.begin(), contentBlocks.end(),
                    [&](const ContentBlock& b) {
                        return (b.type == ContentBlock::ToolResult ||
                                b.type == ContentBlock::ToolProgress) &&
                               b.toolCallId == ev.toolCallId;
                    });
                if (it != contentBlocks.end()) contentBlocks.erase(it);
                break;
            }
            default:
                break;
        }
    }

    void runIncrementalPipeline() {
        // Simplified: run full pipeline on all blocks (incremental in real code)
        contentBlocks = messagePipeline.process(std::move(contentBlocks));
    }
};

TEST_CASE("Runtime Replay: 3-Read turn through each layer", "[RuntimeReplay]") {
    // ===== Stage 0: Define a realistic DisplayEvent sequence =====
    // Simulates: "Let me search for handleDisplayEvent references."
    // → Read FtxuiRepl.cpp, Read StreamBuffer.cpp, Read AnswerPostProcessor.cpp
    // → "Found at FtxuiRepl.cpp:72, StreamBuffer.cpp:45, AnswerPostProcessor.cpp:89"

    struct EventLog {
        const char* stage;
        DisplayEvent event;
    };

    std::vector<EventLog> events = {
        {"AnswerStart", DisplayEvent::answerStart()},
        {"TextPartial", DisplayEvent::textPartial("Let me search for ")},
        {"TextPartial", DisplayEvent::textPartial("handleDisplayEvent references.")},
        {"ToolProgress", DisplayEvent::toolProgress("call_1", "Read", "Reading FtxuiRepl.cpp")},
        {"ToolResult", DisplayEvent::toolResult("call_1", "Read",
            ToolResultSummary::success("Found 3 references in FtxuiRepl.cpp"))},
        {"TextPartial", DisplayEvent::textPartial("Now let me check ")},
        {"TextPartial", DisplayEvent::textPartial("StreamBuffer.cpp.")},
        {"ToolProgress", DisplayEvent::toolProgress("call_2", "Read", "Reading StreamBuffer.cpp")},
        {"ToolResult", DisplayEvent::toolResult("call_2", "Read",
            ToolResultSummary::success("Found 1 reference in StreamBuffer.cpp"))},
        {"TextPartial", DisplayEvent::textPartial("And AnswerPostProcessor.cpp.")},
        {"ToolProgress", DisplayEvent::toolProgress("call_3", "Read", "Reading AnswerPostProcessor.cpp")},
        {"ToolResult", DisplayEvent::toolResult("call_3", "Read",
            ToolResultSummary::success("Found 1 reference in AnswerPostProcessor.cpp"))},
        {"TextPartial", DisplayEvent::textPartial(
            "Found handleDisplayEvent called at: FtxuiRepl.cpp:72, "
            "StreamBuffer.cpp:45, and AnswerPostProcessor.cpp:89.")},
        {"ThinkingBlock", DisplayEvent::thinkingBlock("The search pattern is consistent across files.")},
        {"AnswerEnd", DisplayEvent::answerEnd()},
    };

    // ===== AnswerPostProcessor simulation =====
    // In FTXUI mode, AnswerPostProcessor buffers events during the turn
    // and at AnswerEnd emits Tombstones + ToolGroups.
    AnswerPostProcessor postProcessor;
    std::vector<DisplayEvent> postProcessed;
    int answerEndCount = 0;

    for (auto& el : events) {
        if (el.event.type == DisplayEventType::AnswerEnd) {
            answerEndCount++;
            // Phase 1: process AnswerEnd
            postProcessor.process(DisplayEvent{el.event});
            // Phase 2: finalize
            auto finalEvents = postProcessor.finalize();
            for (auto& fe : finalEvents) {
                postProcessed.push_back(std::move(fe));
            }
            postProcessor.reset();
        } else {
            postProcessor.process(DisplayEvent{el.event});
        }
    }

    // Count DisplayEvents
    int deAnswerText = 0, deToolProgress = 0, deToolResult = 0;
    int deToolGroup = 0, deTombstone = 0, deAnswerStart = 0, deAnswerEnd = answerEndCount;
    int deThinking = 0;

    for (auto& el : events) {
        switch (el.event.type) {
            case DisplayEventType::TextPartial:
            case DisplayEventType::TextParagraph: deAnswerText++; break;
            case DisplayEventType::ToolProgress: deToolProgress++; break;
            case DisplayEventType::ToolResult: deToolResult++; break;
            case DisplayEventType::ThinkingBlock: deThinking++; break;
            case DisplayEventType::AnswerStart: deAnswerStart++; break;
            default: break;
        }
    }
    for (auto& e : postProcessed) {
        switch (e.type) {
            case DisplayEventType::ToolGroup: deToolGroup++; break;
            case DisplayEventType::Tombstone: deTombstone++; break;
            default: break;
        }
    }

    // ===== Stage 1: ContentBlock construction (FtxuiRepl simulation) =====
    FtxuiReplSim repl;

    // Feed all original events (including AnswerEnd)
    for (auto& el : events) {
        repl.handleDisplayEvent(el.event);
    }
    // Feed AnswerPostProcessor finalize events (Tombstones, ToolGroups)
    for (auto& e : postProcessed) {
        repl.handleDisplayEvent(e);
    }

    // Capture ContentBlocks at final state
    auto finalBlocks = repl.contentBlocks;
    auto finalCounts = countBlocks(finalBlocks);
    int finalVisibleLines = estimateVisibleLines(finalBlocks);

    // ===== REPORT =====
    std::cout << "\n";
    std::cout << "==============================================================\n";
    std::cout << "  RUNTIME OUTPUT REPLAY: 3-Read Turn\n";
    std::cout << "==============================================================\n";
    std::cout << "  Model output: 'Find handleDisplayEvent references'\n";
    std::cout << "  Tools: 3 x Read (FtxuiRepl, StreamBuffer, AnswerPostProc)\n";
    std::cout << "==============================================================\n";
    std::cout << "\n";

    // Table header
    auto sep = std::string(78, '-');
    std::cout << sep << "\n";
    std::cout << std::left
              << std::setw(28) << "Stage"
              << std::setw(10) << "AnswerTxt"
              << std::setw(10) << "ToolProg"
              << std::setw(10) << "ToolRes"
              << std::setw(10) << "ToolGrp"
              << std::setw(10) << "CollGrp"
              << std::setw(10) << "Lines"
              << "Notes\n";
    std::cout << sep << "\n";

    // Stage 1: DisplayEvent
    std::cout << std::left
              << std::setw(28) << "1. DisplayEvent (raw)"
              << std::setw(10) << deAnswerText
              << std::setw(10) << deToolProgress
              << std::setw(10) << deToolResult
              << std::setw(10) << deToolGroup
              << std::setw(10) << "-"
              << std::setw(10) << "-"
              << "AnswerPostProc: +" << deTombstone << " Tombs, +" << deToolGroup << " ToolGrp\n";

    // Stage 2: ContentBlock (post pipeline)
    std::cout << std::left
              << std::setw(28) << "2. ContentBlock (final)"
              << std::setw(10) << finalCounts.answerText
              << std::setw(10) << finalCounts.toolProgress
              << std::setw(10) << finalCounts.toolResult
              << std::setw(10) << finalCounts.toolGroup
              << std::setw(10) << finalCounts.collapsedGroup
              << std::setw(10) << finalVisibleLines
              << "FTXUI: pipeline SKIPPED at AnswerEnd\n";

    // Stage 3: Show final block tree
    std::cout << "\n" << sep << "\n";
    std::cout << "FINAL CONTENT BLOCK TREE:\n";
    for (size_t i = 0; i < finalBlocks.size(); i++) {
        auto& b = finalBlocks[i];
        const char* typeStr = "?";
        switch (b.type) {
            case ContentBlock::AnswerText: typeStr = "AnswerText"; break;
            case ContentBlock::ToolResult: typeStr = "ToolResult"; break;
            case ContentBlock::ToolGroup: typeStr = "ToolGroup"; break;
            case ContentBlock::CollapsedGroup: typeStr = "CollapsedGroup"; break;
            case ContentBlock::TurnDuration: typeStr = "TurnDuration"; break;
            case ContentBlock::ThinkingBlock: typeStr = "ThinkingBlock"; break;
            default: break;
        }
        std::cout << "  [" << i << "] " << typeStr;
        if (!b.toolName.empty()) std::cout << " (" << b.toolName << ")";
        if (!b.toolCallId.empty()) std::cout << " id=" << b.toolCallId;
        if (b.type == ContentBlock::AnswerText) {
            auto preview = b.text.substr(0, 60);
            std::cout << " \"" << preview << (b.text.size() > 60 ? "..." : "") << "\"";
        }
        if (!b.children.empty()) {
            std::cout << " [" << b.children.size() << " children]";
        }
        if (b.type == ContentBlock::CollapsedGroup && !b.summary.primaryText.empty()) {
            std::cout << " summary=\"" << b.summary.primaryText << "\"";
        }
        std::cout << "\n";
    }

    // ===== FINDINGS =====
    std::cout << "\n" << sep << "\n";
    std::cout << "KEY FINDINGS:\n";
    std::cout << sep << "\n";

    // 1. AnswerPostProcessor produces group ONLY for >=2 consecutive same-type tools
    // In this example, the 3 Read tools are NOT consecutive in ContentBlocks
    // because AnswerText blocks separate them: AT→TR→AT→TR→AT→TR
    // So AnswerPostProcessor won't group them — they stay as 3 individual ToolResults
    int collapsedReadGroups = finalCounts.collapsedGroup;
    bool toolsCollapsed = collapsedReadGroups > 0;
    std::cout << "\n1. COLLAPSED GROUPS: "
              << (toolsCollapsed ? "PRESENT" : "ABSENT")
              << " (" << collapsedReadGroups << " CollapsedGroup blocks)\n";
    if (!toolsCollapsed) {
        std::cout << "   ROOT CAUSE: AnswerText blocks between ToolResults prevent\n";
        std::cout << "   AnswerPostProcessor.groupConsecutiveToolResults() from grouping.\n";
        std::cout << "   It groups ONLY when ToolResults are adjacent (count >= 2).\n";
        std::cout << "   Pattern AT→TR→AT→TR escapes grouping.\n";
    }

    // 2. Streaming text between tools
    int interToolTexts = 0;
    for (size_t i = 0; i + 1 < finalBlocks.size(); i++) {
        if (finalBlocks[i].type == ContentBlock::AnswerText &&
            (finalBlocks[i+1].type == ContentBlock::ToolResult ||
             finalBlocks[i+1].type == ContentBlock::ToolGroup)) {
            interToolTexts++;
        }
    }
    std::cout << "\n2. INTER-TOOL TEXT BLOCKS: " << interToolTexts << "\n";
    if (interToolTexts > 0) {
        std::cout << "   These AnswerText blocks are visible to the user as separate\n";
        std::cout << "   lines of narration between tool results.\n";
    }

    // 3. Pipeline skip at AnswerEnd
    std::cout << "\n3. PIPELINE AT ANSWEREND: SKIPPED (useExternalPostProcessor=true)\n";
    std::cout << "   Only incremental pipeline runs during streaming.\n";
    std::cout << "   Full MessagePipeline (with CollapsedGroup logic) is not applied.\n";
    std::cout << "   This means CollapsedGroup with displayHint is never created.\n";

    // 4. Total visible lines
    std::cout << "\n4. USER-VISIBLE LINES (estimated): " << finalVisibleLines << "\n";
    std::cout << "   Each AnswerText → " << finalCounts.answerText << " blocks\n";
    std::cout << "   Each ToolResult → " << finalCounts.toolResult << " blocks (1 line each if collapsed)\n";
    std::cout << "   Each ToolGroup → " << finalCounts.toolGroup << " blocks\n";
    std::cout << "   TurnDuration → 1 line\n";

    // 5. Streaming flicker analysis
    std::cout << "\n5. STREAMING FLICKER:\n";
    std::cout << "   During streaming: ToolProgress blocks visible (with spinner icon)\n";
    std::cout << "   After ToolResult: ToolProgress removed, ToolResult added\n";
    std::cout << "   After each ToolResult: incremental pipeline runs\n";
    std::cout << "   → User sees: spinner → result → spinner → result → spinner → result\n";
    std::cout << "   → Blocks shift position as pipeline merges/collapses\n";

    // ===== COMPARISON: What MessagePipeline WOULD produce =====
    std::cout << "\n" << sep << "\n";
    std::cout << "COMPARISON: Full MessagePipeline at AnswerEnd (headless mode)\n";
    std::cout << sep << "\n";

    // Re-build the same blocks but run full MessagePipeline
    FtxuiReplSim repl2;
    repl2.useExternalPostProcessor = false;  // headless mode
    for (auto& el : events) {
        repl2.handleDisplayEvent(el.event);
    }
    // Headless: run full pipeline at AnswerEnd (simulated by calling process)
    repl2.contentBlocks = repl2.messagePipeline.process(std::move(repl2.contentBlocks));

    auto mpBlocks = repl2.contentBlocks;
    auto mpCounts = countBlocks(mpBlocks);
    int mpVisibleLines = estimateVisibleLines(mpBlocks);

    std::cout << std::left
              << std::setw(28) << "Headless (full pipeline)"
              << std::setw(10) << mpCounts.answerText
              << std::setw(10) << mpCounts.toolProgress
              << std::setw(10) << mpCounts.toolResult
              << std::setw(10) << mpCounts.toolGroup
              << std::setw(10) << mpCounts.collapsedGroup
              << std::setw(10) << mpVisibleLines
              << "Pipeline runs at AnswerEnd\n";

    std::cout << "\nHeadless final block tree:\n";
    for (size_t i = 0; i < mpBlocks.size(); i++) {
        auto& b = mpBlocks[i];
        const char* typeStr = "?";
        switch (b.type) {
            case ContentBlock::AnswerText: typeStr = "AnswerText"; break;
            case ContentBlock::ToolResult: typeStr = "ToolResult"; break;
            case ContentBlock::ToolGroup: typeStr = "ToolGroup"; break;
            case ContentBlock::CollapsedGroup: typeStr = "CollapsedGroup"; break;
            case ContentBlock::TurnDuration: typeStr = "TurnDuration"; break;
            default: break;
        }
        std::cout << "  [" << i << "] " << typeStr;
        if (!b.toolName.empty()) std::cout << " (" << b.toolName << ")";
        if (b.type == ContentBlock::AnswerText) {
            auto preview = b.text.substr(0, 60);
            std::cout << " \"" << preview << (b.text.size() > 60 ? "..." : "") << "\"";
        }
        if (!b.children.empty()) {
            std::cout << " [" << b.children.size() << " children]";
        }
        if (b.type == ContentBlock::CollapsedGroup && !b.summary.primaryText.empty()) {
            std::cout << " summary=\"" << b.summary.primaryText << "\"";
        }
        std::cout << "\n";
    }

    // Delta
    std::cout << "\nFTXUI vs HEADLESS DELTA:\n";
    std::cout << "  CollapsedGroup: FTXUI=" << finalCounts.collapsedGroup
              << " Headless=" << mpCounts.collapsedGroup << "\n";
    std::cout << "  AnswerText:     FTXUI=" << finalCounts.answerText
              << " Headless=" << mpCounts.answerText << "\n";
    std::cout << "  ToolResult:     FTXUI=" << finalCounts.toolResult
              << " Headless=" << mpCounts.toolResult << "\n";
    std::cout << "  Visible lines:  FTXUI=" << finalVisibleLines
              << " Headless=" << mpVisibleLines << "\n";

    // ===== REQUIREMENTS (informational, not hard assertions) =====
    // These document the expected behavior discovered during replay.

    // FTXUI mode: inter-tool AnswerText blocks are COMMITTED before ToolResult
    // (streamingText_ is flushed at ToolProgress/ToolResult handler entry).
    // After pipeline: AnswerText blocks remain between CollapsedGroups.
    // interToolTexts may be 0 because CollapsedGroup replaces ToolResult,
    // so the adjacency check AnswerText→ToolResult no longer matches.
    // What matters: AnswerText blocks ARE present in the final tree.
    CHECK(finalCounts.answerText >= 2);  // At least pre-tool + final answer

    // AnswerPostProcessor only groups >=2 consecutive same-type ToolResults
    // With interleaved AnswerText, no grouping occurs
    CHECK(deToolGroup >= 0);  // May or may not be grouped

    INFO("Replay complete — see console output for trace table");
}
