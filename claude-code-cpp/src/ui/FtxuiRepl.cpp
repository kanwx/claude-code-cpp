#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/ui/components/AppLayout.hpp"
#include "claude/console/CreativeVerbs.hpp"
#include "FtxuiColors.hpp"
#include <spdlog/spdlog.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <algorithm>
#include <ctime>

namespace claude {

using namespace ftxui_colors;

// Diagnostic helper — writes directly to /tmp/esc-debug.log bypassing spdlog.
// ESC debug: use raw write() syscall to fd 2 (stderr) and also to a tmp file.
// FILE*-based I/O (fprintf/fopen) was unreliable in FTXUI fullscreen on macOS.
static void escLog(const char* msg) {
    auto t = std::time(nullptr);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ESC-DBG %s\n",
                       tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, msg);
    if (len > 0) {
        write(STDERR_FILENO, buf, static_cast<size_t>(len));
        // Also write to file as fallback
        int fd = open("/tmp/esc-debug.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            write(fd, buf, static_cast<size_t>(len));
            close(fd);
        }
    }
}


// Running-status verb list — curated subset from TS spinnerVerbs.ts.
// One verb is randomly selected per user turn and displayed during the
// thinking/streaming phase.  Completely independent from the final
// turn-completion verbs (Baked/Brewed/Churned/… in kTurnVerbs).
namespace {
static const std::vector<String> kRunningVerbs = {
    "Calculating",   "Cerebrating",   "Choreographing",
    "Cogitating",    "Composing",     "Computing",
    "Concocting",    "Considering",   "Contemplating",
    "Crafting",      "Crunching",     "Deciphering",
    "Deliberating",  "Elucidating",   "Generating",
    "Ideating",      "Inferring",     "Marinating",
    "Orchestrating", "Percolating",   "Perusing",
    "Pondering",     "Processing",    "Ruminating",
    "Synthesizing",  "Tinkering",     "Wandering",
};
} // anonymous namespace

// ========== ContentBlock-based display event handler ==========

void FtxuiRepl::handleDisplayEvent(DisplayEvent&& event) {
    if (!screen_) return;

    // Build ContentBlock directly from DisplayEvent — no StreamEvent back-conversion,
    // no MessagePipeline, no ThinkingFilter. The ContentBlock tree is rendered by
    // renderFtxuiElement() in the AppLayout.
    screen_->Post([this, ev = std::move(event)]() mutable {
        // [DEBUG_METRICS] Log every display event
        {
            const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                                       std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                                       std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
            if (debugMetrics) {
                static const char* eventTypeNames[] = {
                    "TextParagraph",  // 0
                    "TextPartial",    // 1
                    "ThinkingBlock",  // 2
                    "ToolProgress",   // 3
                    "ToolResult",     // 4
                    "ToolGroup",      // 5
                    "AnswerStart",    // 6
                    "AnswerEnd",      // 7
                    "TurnMetadata",   // 8
                    "SystemNotice",   // 9
                    "Tombstone",      // 10
                    "Error"           // 11
                };
                static constexpr int eventTypeCount = 12;
                int rawType = static_cast<int>(ev.type);
                const char* etname = (rawType >= 0 && rawType < eventTypeCount)
                    ? eventTypeNames[rawType] : "?";
                size_t before = contentBlocks_.size();
                fprintf(stderr, "[DEBUG_METRICS] onDisplayEvent type=%s(%d) cb_size_before=%zu",
                        etname, rawType, before);
                if (ev.type == DisplayEventType::ToolProgress ||
                    ev.type == DisplayEventType::ToolResult) {
                    fprintf(stderr, " tool=%s callId=%s",
                            ev.toolName.c_str(), ev.toolCallId.c_str());
                }
                if (!ev.activity.empty()) {
                    fprintf(stderr, " activity=\"%s\"", ev.activity.c_str());
                }
                fprintf(stderr, "\n");
            }
        }

        // Only signal content change for events that materially change the
        // visible display. ThinkingBlock and TurnMetadata fire at high
        // frequency during thinking (for spinner and token updates) but
        // don't change any visible content — the spinner is tickCounter-
        // driven and refreshes at its own pace. Letting these through
        // prevents the idle throttle from kicking in, causing excessive
        // terminal redraws that manifest as "frantically refreshing" tool
        // status indicators during the thinking phase.
        switch (ev.type) {
            case DisplayEventType::TextPartial:
            case DisplayEventType::TextParagraph:
            case DisplayEventType::ToolProgress:
            case DisplayEventType::ToolResult:
            case DisplayEventType::ToolGroup:
            case DisplayEventType::Tombstone:
            case DisplayEventType::Error:
            case DisplayEventType::AnswerStart:
            case DisplayEventType::AnswerEnd:
            case DisplayEventType::SystemNotice:
                refreshContentChanged_.store(true, std::memory_order_release);
                break;
            default:
                break;
        }

        switch (ev.type) {
            case DisplayEventType::TextPartial: {
                // Defensive: strip any residual thinking tags that may have
                // leaked through StreamBuffer. This is a last-line-of-defense
                // filter before the text reaches the display.
                String filtered = ev.text;
                // Strip <think> <thinking> and their close tags
                static const std::vector<std::pair<String, String>> residualTags = {
                    {"<think>", "</think>"},
                    {"<thinking>", "</thinking>"},
                    {"\xef\xbc\x9c" "think" "\xef\xbc\x9e", "\xef\xbc\x9c" "/think" "\xef\xbc\x9e"},
                    {"\xef\xbc\x9c" "thinking" "\xef\xbc\x9e", "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e"},
                    {"&lt;think&gt;", "&lt;/think&gt;"},
                    {"&lt;thinking&gt;", "&lt;/thinking&gt;"},
                };
                for (const auto& [openTag, closeTag] : residualTags) {
                    size_t pos = 0;
                    while ((pos = filtered.find(openTag)) != String::npos) {
                        auto endPos = filtered.find(closeTag, pos + openTag.size());
                        if (endPos != String::npos) {
                            filtered.erase(pos, endPos + closeTag.size() - pos);
                        } else {
                            filtered.erase(pos);
                            break;
                        }
                    }
                    // Also strip orphan close tags
                    while ((pos = filtered.find(closeTag)) != String::npos) {
                        filtered.erase(pos, closeTag.size());
                    }
                }
                if (filtered.empty()) break;
                // Skip whitespace-only chunks — they produce blank boxes in the UI.
                // Thinking tag stripping can leave behind trailing newlines.
                if (filtered.find_first_not_of(" \t\n\r") == String::npos) break;
                streamingText_ += filtered;
                streamingRenderer_.append(filtered);
                if (isThinking_) isThinking_ = false;
                lastOutputTime_ = std::chrono::steady_clock::now();
                break;
            }

            case DisplayEventType::TextParagraph: {
                // ev.text carries the full paragraph from StreamBuffer::flushTextBuffer.
                // streamingText_ is only populated by TextPartial (emitted at 256-char
                // threshold), so short responses and paragraph-boundary flushes would
                // be silently discarded if we only read streamingText_.
                String committed = ev.text.empty()
                    ? std::move(streamingText_)
                    : std::move(ev.text);
                streamingText_.clear();
                streamingRenderer_.reset();

                // Trim leading/trailing blank lines (same logic as AnswerEnd).
                // Without this, a leading "\n" in the flushed paragraph produces
                // an empty first element in the markdown renderer, which causes
                // the "⏺" prefix to render standalone on its own line.
                if (!committed.empty()) {
                    size_t textStart = 0;
                    while (textStart < committed.size()) {
                        size_t nl = committed.find('\n', textStart);
                        size_t lineEnd = (nl == String::npos) ? committed.size() : nl;
                        if (committed.find_first_not_of(" \t\r", textStart) < lineEnd) break;
                        textStart = (nl == String::npos) ? committed.size() : nl + 1;
                    }
                    if (textStart > 0 && textStart < committed.size()) {
                        committed = committed.substr(textStart);
                    } else if (textStart >= committed.size()) {
                        committed.clear();
                    }
                    // Trim trailing blank lines
                    if (!committed.empty()) {
                        size_t textEnd = committed.size();
                        while (textEnd > 0) {
                            size_t prevNl = committed.rfind('\n', textEnd - 1);
                            size_t lineStart = (prevNl == String::npos) ? 0 : prevNl + 1;
                            if (committed.find_first_not_of(" \t\r", lineStart) < textEnd) break;
                            textEnd = (lineStart > 0) ? lineStart - 1 : 0;
                        }
                        if (textEnd < committed.size()) {
                            committed.resize(textEnd);
                        }
                    }
                }

                if (!committed.empty() &&
                    committed.find_first_not_of(" \t\n\r") != String::npos) {
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.isFirst = isFirstAnswerBlock_;
                    cb.text = std::move(committed);
                    isFirstAnswerBlock_ = false;
                    cb.stableId = nextStableId_++;
                    contentBlocks_.push_back(std::move(cb));
                }
                break;
            }

            case DisplayEventType::ThinkingBlock: {
                // Store thinking text for the final collapsed ThinkingBlock.
                // Do NOT show raw reasoning text in the inline indicator —
                // it causes flickering and leaks model internals to the display.
                thinkingText_ = std::move(ev.thinkingText);
                break;
            }

            case DisplayEventType::ToolProgress: {
                // Clear streaming text before tool event
                if (!streamingText_.empty()) {
                    ContentBlock textCb;
                    textCb.type = ContentBlock::AnswerText;
                    textCb.isFirst = isFirstAnswerBlock_;
                    textCb.text = std::move(streamingText_);
                    isFirstAnswerBlock_ = false;
                    streamingText_.clear();
                    streamingRenderer_.reset();
                    textCb.stableId = nextStableId_++;
                    contentBlocks_.push_back(std::move(textCb));
                }
                ContentBlock cb;
                cb.type = ContentBlock::ToolProgress;
                cb.toolName = std::move(ev.toolName);
                cb.activity = std::move(ev.activity);
                cb.toolCallId = ev.toolCallId;
                cb.stableId = nextStableId_++;
                toolProgressIndices_[ev.toolCallId] = contentBlocks_.size();  // B5: record index before push
                contentBlocks_.push_back(std::move(cb));
                break;
            }

            case DisplayEventType::ToolResult: {
                // Clear streaming text before tool result.
                // Track whether we inserted text so we can fix up ordering
                // after the in-place replacement below.
                bool textFlushed = false;
                if (!streamingText_.empty()) {
                    ContentBlock textCb;
                    textCb.type = ContentBlock::AnswerText;
                    textCb.isFirst = isFirstAnswerBlock_;
                    textCb.text = std::move(streamingText_);
                    isFirstAnswerBlock_ = false;
                    streamingText_.clear();
                    streamingRenderer_.reset();
                    textCb.stableId = nextStableId_++;
                    contentBlocks_.push_back(std::move(textCb));
                    textFlushed = true;
                }

                // Build the ToolResult block
                ContentBlock cb;
                cb.type = ContentBlock::ToolResult;
                cb.toolName = std::move(ev.toolName);
                cb.summary = std::move(ev.summary);
                cb.rawResultPath = std::move(ev.rawResultPath);
                cb.toolCallId = ev.toolCallId;
                cb.expanded = verboseTools_;
                // Set result status from summary
                if (cb.summary.isError) {
                    cb.resultStatus = ToolResultStatus::Error;
                } else if (cb.summary.isDim) {
                    // Detect cancelled vs rejected from summary text
                    if (cb.summary.primaryText.find("Interrupted") != String::npos) {
                        cb.resultStatus = ToolResultStatus::Cancelled;
                    } else if (cb.summary.primaryText.find("Rejected") != String::npos) {
                        cb.resultStatus = ToolResultStatus::Rejected;
                    }
                }

                // B5: O(1) in-place ToolProgress → ToolResult replacement.
                // No index shifting — other ToolProgress blocks stay at stable positions.
                String callId = cb.toolCallId;
                bool replaced = false;
                if (!callId.empty()) {
                    auto it = toolProgressIndices_.find(callId);
                    if (it != toolProgressIndices_.end()) {
                        size_t idx = it->second;
                        if (idx < contentBlocks_.size() &&
                            contentBlocks_[idx].type == ContentBlock::ToolProgress &&
                            contentBlocks_[idx].toolCallId == callId) {
                            // If we just flushed text (push_back), the AnswerText
                            // is at the end — after the ToolProgress. Swap them
                            // so AnswerText appears before the tool result.
                            if (textFlushed && idx < contentBlocks_.size() - 1) {
                                std::swap(contentBlocks_[idx], contentBlocks_.back());
                                idx = contentBlocks_.size() - 1;
                            }
                            auto oldStableId = contentBlocks_[idx].stableId;
                            contentBlocks_[idx] = std::move(cb);
                            contentBlocks_[idx].stableId = oldStableId;
                            contentBlocks_[idx].activity.clear();
                            replaced = true;
                        }
                        toolProgressIndices_.erase(it);
                    }
                }
                if (!replaced) {
                    cb.stableId = nextStableId_++;
                    contentBlocks_.push_back(std::move(cb));
                }
                runIncrementalPipeline();
                break;
            }

            case DisplayEventType::ToolGroup: {
                ContentBlock cb;
                cb.type = ContentBlock::ToolGroup;
                cb.toolName = std::move(ev.toolName);
                cb.summary = std::move(ev.summary);
                cb.expanded = false;
                cb.stableId = nextStableId_++;
                contentBlocks_.push_back(std::move(cb));
                runIncrementalPipeline();
                break;
            }

            case DisplayEventType::SystemNotice:
                layoutState_.status.systemNotice = std::move(ev.noticeText);
                break;

            case DisplayEventType::Tombstone: {
                // Remove the specific ToolResult block identified by toolCallId.
                // Previously used pop_back() which blindly removed the last block,
                // but after AnswerEnd commits streaming text and inserts TurnDuration,
                // the last block is no longer the ToolResult being tombstoned.
                if (!ev.toolCallId.empty()) {
                    auto it = std::find_if(contentBlocks_.begin(), contentBlocks_.end(),
                        [&](const ContentBlock& b) {
                            return (b.type == ContentBlock::ToolResult ||
                                    b.type == ContentBlock::ToolProgress) &&
                                   b.toolCallId == ev.toolCallId;
                        });
                    if (it != contentBlocks_.end()) {
                        contentBlocks_.erase(it);
                    }
                } else if (!contentBlocks_.empty()) {
                    // Fallback: remove last ToolResult block
                    auto it = contentBlocks_.end();
                    while (it != contentBlocks_.begin()) {
                        --it;
                        if (it->type == ContentBlock::ToolResult) {
                            contentBlocks_.erase(it);
                            break;
                        }
                    }
                }
                break;
            }

            case DisplayEventType::Error: {
                ContentBlock cb;
                cb.type = ContentBlock::ErrorMessage;
                cb.text = std::move(ev.text);
                cb.stableId = nextStableId_++;
                contentBlocks_.push_back(std::move(cb));
                break;
            }

            case DisplayEventType::AnswerStart:
                // Remove stale TurnDuration and ThinkingBlock within the
                // CURRENT user turn only (after the last UserMessage).
                // Must not touch blocks from earlier turns — those are
                // historical content that must persist across turns.
                {
                    size_t lastUserMsg = contentBlocks_.size();
                    for (size_t i = contentBlocks_.size(); i > 0; --i) {
                        if (contentBlocks_[i - 1].type == ContentBlock::UserMessage) {
                            lastUserMsg = i - 1;
                            break;
                        }
                    }
                    contentBlocks_.erase(
                        std::remove_if(contentBlocks_.begin() + static_cast<long>(lastUserMsg),
                                       contentBlocks_.end(),
                            [](const ContentBlock& b) {
                                return b.type == ContentBlock::TurnDuration ||
                                       b.type == ContentBlock::ThinkingBlock;
                            }),
                        contentBlocks_.end()
                    );
                }

                apiRoundIndex_++;  // each AnswerStart = new API round within the user turn

                // P1: Only set startTime_ on the first AnswerStart of a user turn.
                // Subsequent TAOR iterations must not reset the clock so
                // finishStream can compute the full turn duration.
                if (!turnStarted_) {
                    startTime_ = std::chrono::steady_clock::now();
                    turnStarted_ = true;
                    turnDurationEmitted_ = false;
                }

                isStreaming_ = true;
                isThinking_ = true;
                streamingText_.clear();
                streamingRenderer_.reset();
                currentTurnStartIndex_ = contentBlocks_.size();    // preserve scrollback
                toolProgressIndices_.clear();                        // fresh indices for new turn
                lastStableIndex_ = 0;                               // reset anchor for new turn
                startRefreshThread();
                break;

            case DisplayEventType::AnswerEnd: {
                // Commit any remaining streaming text.
                // Trim leading/trailing blank lines to avoid rendering a
                // standalone "●" prefix with no visible content beneath it.
                if (!streamingText_.empty()) {
                    // Trim leading blank lines (empty or whitespace-only)
                    size_t textStart = 0;
                    while (textStart < streamingText_.size()) {
                        size_t nl = streamingText_.find('\n', textStart);
                        size_t lineEnd = (nl == String::npos) ? streamingText_.size() : nl;
                        if (streamingText_.find_first_not_of(" \t\r", textStart) < lineEnd) break;
                        textStart = (nl == String::npos) ? streamingText_.size() : nl + 1;
                    }

                    if (textStart < streamingText_.size()) {
                        // Trim trailing blank lines
                        size_t textEnd = streamingText_.size();
                        while (textEnd > textStart) {
                            size_t prevNl = streamingText_.rfind('\n', textEnd - 1);
                            size_t lineStart = (prevNl == String::npos) ? 0 : prevNl + 1;
                            if (streamingText_.find_first_not_of(" \t\r", lineStart) < textEnd) break;
                            textEnd = (lineStart > 0) ? lineStart - 1 : 0;
                        }

                        String trimmed = (textStart > 0 || textEnd < streamingText_.size())
                            ? streamingText_.substr(textStart, textEnd - textStart)
                            : std::move(streamingText_);

                        if (trimmed.find_first_not_of(" \t\n\r") != String::npos) {
                            ContentBlock cb;
                            cb.type = ContentBlock::AnswerText;
                            cb.isFirst = isFirstAnswerBlock_;
                            cb.text = std::move(trimmed);
                            isFirstAnswerBlock_ = false;
                            cb.stableId = nextStableId_++;
                            contentBlocks_.push_back(std::move(cb));
                        }
                    }
                    streamingText_.clear();
                    streamingRenderer_.reset();
                }
                // B5: Clean up orphaned ToolProgress blocks (tools that never completed)
                for (auto& [callId, idx] : toolProgressIndices_) {
                    if (idx < contentBlocks_.size() &&
                        contentBlocks_[idx].type == ContentBlock::ToolProgress) {
                        contentBlocks_[idx].type = ContentBlock::ToolResult;
                        contentBlocks_[idx].summary = ToolResultSummary::dim("Interrupted");
                        contentBlocks_[idx].resultStatus = ToolResultStatus::Cancelled;
                    }
                }
                toolProgressIndices_.clear();

                // Always run full MessagePipeline at AnswerEnd.
                // This guarantees FTXUI final ContentBlock tree == Headless final tree
                // for the same DisplayEvent input.
                {
                    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
                    if (debugMetrics) {
                        fprintf(stderr, "[DEBUG_METRICS] AnswerEnd BEFORE pipeline: contentBlocks_.size()=%zu, metricsTurnStartIndex_=%zu (apiRound=%d)\n",
                                contentBlocks_.size(), metricsTurnStartIndex_, apiRoundIndex_);
                        static const char* typeNames[] = {
                            "UserMessage","AnswerText","ThinkingBlock","ToolProgress",
                            "ToolResult","ToolGroup","AgentProgress","ErrorMessage",
                            "SystemMessage","CompactBoundary","CollapsedGroup","TurnDuration"
                        };
                        for (size_t i = 0; i < contentBlocks_.size(); ++i) {
                            int t = static_cast<int>(contentBlocks_[i].type);
                            const char* tn = (t >= 0 && t < 12) ? typeNames[t] : "?";
                            fprintf(stderr, "  BEFORE[%zu] %s\n", i, tn);
                        }
                    }
                }
                contentBlocks_ = messagePipeline_.process(std::move(contentBlocks_));

                // P6-P1b: Phase-aware AnswerText prefix detection.
                // After pipeline, re-assign isFirst on AnswerText blocks:
                //   - First non-dimmed AnswerText → isFirst=true (phase header)
                //   - Non-dimmed AnswerText preceded by tool-like block → isFirst=true
                //   - All other non-dimmed AnswerText → isFirst=false (continuation)
                //   - Dimmed narration → isFirst=false always
                {
                    auto isToolLikeBlock = [](const ContentBlock& b) {
                        return b.type == ContentBlock::ToolResult ||
                               b.type == ContentBlock::ToolGroup ||
                               b.type == ContentBlock::CollapsedGroup ||
                               b.type == ContentBlock::AgentProgress;
                    };

                    // Find previous significant block, skipping dimmed/empty AnswerText
                    auto findPrevSignificant = [&](size_t i) -> const ContentBlock* {
                        for (size_t j = i; j > 0; --j) {
                            const auto& prev = contentBlocks_[j - 1];
                            if (prev.type == ContentBlock::AnswerText) {
                                if (prev.dimmed || prev.text.find_first_not_of(" \t\n\r") == String::npos) {
                                    continue;
                                }
                            }
                            return &prev;
                        }
                        return nullptr;
                    };

                    bool seenNonDimmedAnswer = false;

                    for (size_t i = 0; i < contentBlocks_.size(); ++i) {
                        auto& block = contentBlocks_[i];
                        if (block.type != ContentBlock::AnswerText) continue;

                        if (block.dimmed) {
                            block.isFirst = false;
                            continue;
                        }

                        const auto* prev = findPrevSignificant(i);

                        if (!seenNonDimmedAnswer) {
                            block.isFirst = true;
                        } else if (prev && isToolLikeBlock(*prev)) {
                            block.isFirst = true;
                        } else {
                            block.isFirst = false;
                        }

                        seenNonDimmedAnswer = true;
                    }
                }

                lastStableIndex_ = contentBlocks_.size();
                {
                    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
                    if (debugMetrics) {
                        fprintf(stderr, "[DEBUG_METRICS] AnswerEnd AFTER pipeline: contentBlocks_.size()=%zu\n",
                                contentBlocks_.size());
                        static const char* typeNames[] = {
                            "UserMessage","AnswerText","ThinkingBlock","ToolProgress",
                            "ToolResult","ToolGroup","AgentProgress","ErrorMessage",
                            "SystemMessage","CompactBoundary","CollapsedGroup","TurnDuration"
                        };
                        for (size_t i = 0; i < contentBlocks_.size(); ++i) {
                            int t = static_cast<int>(contentBlocks_[i].type);
                            const char* tn = (t >= 0 && t < 12) ? typeNames[t] : "?";
                            fprintf(stderr, "  AFTER[%zu] %s\n", i, tn);
                        }
                    }
                }

                // Thinking is rendered by the AppLayout streaming overlay
                // (s->content.thinking.active).  We do not persist a
                // ThinkingBlock ContentBlock — TS compact mode hides
                // thinking in the final frame.
                thinkingSummary_.clear();
                thinkingText_.clear();

                // TurnDuration is deferred to finishStream().
                // AnswerEnd = API stream end ≠ whole turn end.
                // The turn is not complete until tool execution finishes
                // and executeLoop returns.  Creating TurnDuration here
                // would show a premature duration (e.g. "Baked for 1s"
                // while sleep 30 is still running).

                // [DIAGNOSTIC] contentBlocks_ final dump — gated by CLAUDE_CODE_DEBUG_METRICS
                {
                    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
                    if (debugMetrics) {
                        fprintf(stderr, "\n=== CONTENT_BLOCKS_DUMP (turn=%d, apiRound=%d, total=%zu) ===\n",
                                userTurnIndex_, apiRoundIndex_, contentBlocks_.size());
                        static const char* kTypeNames[] = {
                            "UserMessage","AnswerText","ThinkingBlock","ToolProgress",
                            "ToolResult","ToolGroup","AgentProgress","ErrorMessage",
                            "SystemMessage","CompactBoundary","CollapsedGroup","TurnDuration"
                        };
                        for (size_t bi = 0; bi < contentBlocks_.size(); ++bi) {
                            const auto& b = contentBlocks_[bi];
                            int t = static_cast<int>(b.type);
                            const char* tn = (t >= 0 && t < 12) ? kTypeNames[t] : "?";
                            String preview;
                            for (size_t ci = 0; ci < b.text.size() && ci < 120; ++ci) {
                                unsigned char c = static_cast<unsigned char>(b.text[ci]);
                                if (c == '\n') preview += "\\n";
                                else if (c == '\r') preview += "\\r";
                                else if (c == '\t') preview += "\\t";
                                else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\x%02x", c); preview += buf; }
                                else preview += static_cast<char>(c);
                            }
                            fprintf(stderr, "  [%zu] %-15s text.size=%4zu text=\"%s\"%s%s isFirst=%d\n",
                                    bi, tn, b.text.size(), preview.c_str(),
                                    b.text.size() > 120 ? "..." : "",
                                    t == 11 ? " <<<TURN_DURATION" : "",  // type 11 = TurnDuration
                                    b.isFirst ? 1 : 0);
                        }
                        fprintf(stderr, "=== END CONTENT_BLOCKS_DUMP ===\n\n");
                    }
                }

                // [METRICS] Turn-level metrics collection (read-only, no side effects)
                {
                    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
                    if (debugMetrics) {
                        fprintf(stderr, "[DEBUG_METRICS] AnswerEnd: metricsCollector_=%s, modelInfo_=%s, metricsTurnStartIndex_=%zu, contentBlocks_.size()=%zu, apiRound=%d\n",
                                metricsCollector_ ? "non-null" : "null",
                                modelInfo_.empty() ? "(empty)" : modelInfo_.c_str(),
                                metricsTurnStartIndex_,
                                contentBlocks_.size(),
                                apiRoundIndex_);

                        // Block type dump for turn window
                        fprintf(stderr, "[DEBUG_METRICS] AnswerEnd block dump (metrics window [%zu, %zu), apiRound=%d):\n",
                                metricsTurnStartIndex_, contentBlocks_.size(), apiRoundIndex_);
                        static const char* blockTypeNames[] = {
                            "UserMessage",     // 0
                            "AnswerText",      // 1
                            "ThinkingBlock",   // 2
                            "ToolProgress",    // 3
                            "ToolResult",      // 4
                            "ToolGroup",       // 5
                            "AgentProgress",   // 6
                            "ErrorMessage",    // 7
                            "SystemMessage",   // 8
                            "CompactBoundary", // 9
                            "CollapsedGroup",  // 10
                            "TurnDuration"     // 11
                        };
                        static constexpr int blockTypeCount = 12;
                        for (size_t bi = metricsTurnStartIndex_; bi < contentBlocks_.size(); ++bi) {
                            const auto& b = contentBlocks_[bi];
                            int rawType = static_cast<int>(b.type);
                            const char* tname = (rawType >= 0 && rawType < blockTypeCount)
                                ? blockTypeNames[rawType] : nullptr;

                            const char* summary = "";
                            if (b.type == ContentBlock::CollapsedGroup) {
                                summary = b.text.c_str();
                            } else if (b.type == ContentBlock::ToolGroup) {
                                summary = b.text.c_str();
                            } else if (b.type == ContentBlock::ToolResult) {
                                summary = b.summary.primaryText.c_str();
                            } else if (b.type == ContentBlock::AgentProgress) {
                                summary = b.text.c_str();
                            }

                            if (tname) {
                                fprintf(stderr, "  [%zu] %s%s%s\n",
                                        bi, tname,
                                        summary[0] ? " \"" : "",
                                        summary[0] ? summary : "");
                            } else {
                                // Unknown block — dump all available info
                                fprintf(stderr, "  [%zu] Unknown(raw=%d) text=\"%s\" toolName=\"%s\""
                                        " children=%zu expanded=%d stableId=%llu\n",
                                        bi, rawType,
                                        b.text.c_str(),
                                        b.toolName.c_str(),
                                        b.children.size(),
                                        b.expanded ? 1 : 0,
                                        (unsigned long long)b.stableId);
                            }
                        }
                    }
                    if (metricsCollector_) {
                        if (debugMetrics) {
                            fprintf(stderr, "[DEBUG_METRICS] AnswerEnd: calling analyze+write, answer_end_seen=true\n");
                        }
                        auto m = metricsCollector_->analyze(
                            contentBlocks_, metricsTurnStartIndex_, modelInfo_, userTurnIndex_);
                        m.apiRoundIndex = apiRoundIndex_;  // cumulative snapshot round
                        metricsCollector_->write(std::move(m));
                        if (debugMetrics) {
                            fprintf(stderr, "[DEBUG_METRICS] AnswerEnd: write completed, metrics_written=true, write_error=none\n");
                        }
                    } else if (debugMetrics) {
                        fprintf(stderr, "[DEBUG_METRICS] AnswerEnd: metricsCollector_ is null, skipping write\n");
                    }
                }

                // Keep isStreaming_ true until finishStream() — the turn is not
                // complete until runStreaming() returns and main.cpp calls
                // finishStream().  Setting isStreaming_=false here allowed the
                // Return handler to accept new user input while tools were still
                // executing in the background, producing orphan tool_use blocks
                // and 400 errors from the API (E8 bug).
                isThinking_ = false;

                // B6: Record turn boundary
                turnBoundaries_.push_back(contentBlocks_.size());

                // B6: Hard cap — trim oldest blocks if exceeding MAX_BLOCKS
                if (contentBlocks_.size() > MAX_BLOCKS) {
                    size_t toRemove = contentBlocks_.size() - MAX_BLOCKS / 2;
                    contentBlocks_.erase(
                        contentBlocks_.begin(),
                        contentBlocks_.begin() + static_cast<long>(toRemove));
                    // Adjust turn boundaries
                    for (auto& b : turnBoundaries_) {
                        b = (b > toRemove) ? (b - toRemove) : 0;
                    }
                    currentTurnStartIndex_ = (currentTurnStartIndex_ > toRemove)
                        ? (currentTurnStartIndex_ - toRemove) : 0;
                    metricsTurnStartIndex_ = (metricsTurnStartIndex_ > toRemove)
                        ? (metricsTurnStartIndex_ - toRemove) : 0;
                }
                break;
            }

            case DisplayEventType::TurnMetadata:
                newPipelineStatusMetadata_ = std::move(ev.metadata);
                break;

            default:
                break;
        }

        // [DEBUG_METRICS] Log cb_size after event processing
        {
            const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                                       std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                                       std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
            if (debugMetrics) {
                fprintf(stderr, "[DEBUG_METRICS] onDisplayEvent done, cb_size_after=%zu\n",
                        contentBlocks_.size());
            }
        }
    });
}

void FtxuiRepl::groupConsecutiveToolResults() {
    std::vector<ContentBlock> result;
    size_t i = 0;
    while (i < contentBlocks_.size()) {
        if (contentBlocks_[i].type == ContentBlock::ToolResult &&
            AnswerPostProcessor::isCollapsibleTool(contentBlocks_[i].toolName)) {
            size_t start = i;
            while (i < contentBlocks_.size() &&
                   contentBlocks_[i].type == ContentBlock::ToolResult &&
                   AnswerPostProcessor::isCollapsibleTool(contentBlocks_[i].toolName)) {
                ++i;
            }
            size_t count = i - start;
            if (count >= 2) {
                ContentBlock group;
                group.type = ContentBlock::ToolGroup;
                group.expanded = false;
                std::map<String, int> counts;
                for (size_t j = start; j < i; ++j) {
                    counts[contentBlocks_[j].toolName]++;
                }
                // Build summary with verb form matching TS:
                // "Read 3 files", "Grep 1 result", "Bash 2 runs"
                static const std::map<String, std::pair<String, String>> verbForms = {
                    {"Read",   {"Read",   "files"}},
                    {"Grep",   {"Grep",   "results"}},
                    {"Glob",   {"Glob",   "files"}},
                    {"LS",     {"LS",     "listings"}},
                    {"Bash",   {"Ran",    "commands"}},
                    {"WebFetch",  {"Fetched", "pages"}},
                    {"WebSearch", {"Searched", "queries"}},
                };
                String summaryText;
                for (auto& [name, cnt] : counts) {
                    if (!summaryText.empty()) summaryText += ", ";
                    auto it = verbForms.find(name);
                    if (it != verbForms.end()) {
                        summaryText += it->second.first + " " +
                            std::to_string(cnt) + " " + it->second.second;
                    } else {
                        summaryText += name + " " + std::to_string(cnt) +
                            (cnt > 1 ? " times" : " time");
                    }
                }
                group.summary = ToolResultSummary::success(summaryText);
                group.toolName = "Group";
                for (size_t j = start; j < i; ++j) {
                    group.children.push_back(std::move(contentBlocks_[j]));
                }
                result.push_back(std::move(group));
            } else {
                result.push_back(std::move(contentBlocks_[start]));
                i = start + 1;
            }
        } else {
            result.push_back(std::move(contentBlocks_[i]));
            ++i;
        }
    }
    contentBlocks_ = std::move(result);
}

void FtxuiRepl::runMessagePipeline() {
    contentBlocks_ = messagePipeline_.process(std::move(contentBlocks_));
}

void FtxuiRepl::runIncrementalPipeline() {
    // P0: Defer all pipeline processing to AnswerEnd.
    //
    // Running grouping passes (3/4) during streaming causes premature wrapping:
    // pass 4 wraps ToolResult → CollapsedGroup before pass 3 sees subsequent
    // tools, preventing consecutive same-type ToolResults from being merged.
    // This causes FTXUI and Headless to produce different final ContentBlock
    // trees for consecutive tool calls (e.g., Read×3 becomes 3×CollapsedGroup
    // instead of 1×CollapsedGroup "Read 3 files").
    //
    // During streaming, bare ToolResults are display-safe: the FTXUI renderer
    // shows only 1 line (badge + truncated summary + [Ctrl+O]) when expanded=false,
    // which is the default. Full output lives in rawResultPath and is never
    // rendered inline. At AnswerEnd, the full MessagePipeline processes all
    // accumulated blocks at once, producing the same tree as Headless.
}

namespace {

void crashHandler(int sig) {
    // Restore terminal before anything else — mouse tracking must be
    // disabled so the shell works after the crash dump.
    // write() and tcsetattr() are async-signal-safe.
    write(STDOUT_FILENO, "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l\x1b[?25h\x1b[0m", 38);
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag |= (ICANON | ECHO | ISIG);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }

    void* array[20];
    int size = backtrace(array, 20);
    fprintf(stderr, "\n=== FTXUI CRASH (signal %d) ===\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    _Exit(1);
}

} // anonymous namespace

// ========== Sync layout state ==========
// Pushes current FtxuiRepl state into layoutState_ for the AppLayout renderer.
// Called from UI thread before each render.

void FtxuiRepl::syncLayoutState() {
    auto& ls = layoutState_;

    // Header
    ls.header.modelName = modelInfo_;
    ls.header.contextPercent = (contextMaxTokens_ > 0)
        ? 100.0f * contextUsedTokens_ / contextMaxTokens_ : 0.0f;
    ls.header.inputTokens = inputTokens_;
    ls.header.outputTokens = outputTokens_;
    ls.header.cost = costUsd_;
    ls.header.cwd = cwd_;
    ls.header.gitBranch = gitBranch_;
    ls.header.isStreaming = isStreaming_;

    // Content — point contentBlocks directly (no copy)
    ls.content.contentBlocks = &contentBlocks_;
    ls.content.streaming.text = streamingText_;
    ls.tickCounter++;
    ls.content.streaming.tickCounter = ls.tickCounter;
    // Use incremental StreamingRenderer instead of full reparse
    if (!streamingText_.empty()) {
        ls.content.streaming.cachedElements = streamingRenderer_.render();
    } else {
        ls.content.streaming.cachedElements.clear();
    }
    ls.content.thinking.active = isThinking_;
    ls.content.thinking.summary = thinkingSummary_;
    ls.content.thinking.stalled = false; // will be computed from lastOutputTime_
    ls.content.thinking.tickCounter = ls.tickCounter;

    // Running-status fields: elapsed time, token estimate, running verb.
    if (isThinking_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_).count();
        ls.content.thinking.elapsedSeconds = static_cast<int>(elapsedMs / 1000);
        ls.content.thinking.tokenEstimate = static_cast<int>(streamingText_.size() / 4);
        if (thinkingVerb_.empty()) {
            // Pick a random verb per turn — stable for the duration.
            static size_t runningVerbIdx = 0;
            thinkingVerb_ = kRunningVerbs[runningVerbIdx % kRunningVerbs.size()];
            runningVerbIdx++;
        }
        ls.content.thinking.runningVerb = thinkingVerb_;
    } else {
        ls.content.thinking.elapsedSeconds = 0;
        ls.content.thinking.tokenEstimate = 0;
        ls.content.thinking.runningVerb.clear();
    }
    ls.content.messagesAbove = static_cast<int>(virtualScroll_.firstVisibleIndex());
    ls.content.autoScroll = ls.autoScroll;
    ls.content.scrollRatio = ls.scrollRatio;
    if (ls.autoScroll) {
        ls.scrollRatio = 1.0f;
        ls.content.scrollRatio = 1.0f;
    }

    // Input
    ls.input.streaming = isStreaming_;

    // Status bar (from new pipeline TurnMetadata)
    {
        auto& st = ls.status;
        auto& meta = newPipelineStatusMetadata_;
        st.modelName = meta.modelName;
        st.turnDuration = meta.durationStr;
        st.isStreaming = meta.isStreaming;
        if (meta.contextUsed > 0 || meta.contextTotal > 0) {
            auto fmtK = [](int64_t n) -> String {
                if (n >= 1'000'000) return std::to_string(n / 100'000) + "." + std::to_string((n % 100'000) / 10) + "M";
                if (n >= 1'000) return std::to_string(n / 100) + "." + std::to_string((n % 100) / 10) + "K";
                return std::to_string(n);
            };
            st.contextStr = fmtK(meta.contextUsed) + "/" + fmtK(meta.contextTotal) + " ctx";
        }
        if (meta.outputTokens > 0) {
            auto fmtK = [](int64_t n) -> String {
                if (n >= 1'000) return std::to_string(n / 100) + "." + std::to_string((n % 100) / 10) + "K";
                return std::to_string(n);
            };
            st.tokenStr = fmtK(meta.outputTokens) + " out";
        }
        st.costStr = meta.costStr;
        st.visible = !st.turnDuration.empty() || !st.modelName.empty() || st.isStreaming
                     || !st.contextStr.empty() || !st.costStr.empty();
    }

    // Footer
    ls.footer.modeIndicator = currentMode_;
    ls.footer.modeHintDismissed = modeHintDismissed_;
    ls.footer.authenticated = isAuthenticated_;
    ls.footer.isStreaming = isStreaming_;

    // Permission
    ls.permissionActive = permissionPromptActive_;
    ls.permissionToolName = permissionToolName_;
    ls.permissionActivity = permissionActivity_;
    ls.permissionDescription = permissionDescription_;
    ls.permissionFocusedIndex = permissionFocusedIndex_;
    ls.permissionFeedbackActive = permissionFeedbackActive_;
    ls.permissionFeedbackText = permissionFeedbackText_;
    ls.permissionFeedbackCursorPos = permissionFeedbackCursorPos_;

    // Completions
    const auto& completions = completer_.currentCompletions();
    ls.completions.assign(completions.begin(), completions.end());

    // Verbose tools
    ls.verboseTools = verboseTools_;

    // Collapsible tool result focus tracking
    ls.collapsibleCount = 0;
    int focusSeq = 0;
    for (const auto& block : contentBlocks_) {
        if (block.type == ContentBlock::ToolResult &&
            AnswerPostProcessor::isCollapsibleTool(block.toolName)) {
            focusSeq++;
            ls.collapsibleCount++;
        } else if (block.type == ContentBlock::ToolGroup ||
                   block.type == ContentBlock::CollapsedGroup) {
            focusSeq++;
            ls.collapsibleCount++;
        }
    }
    // Clamp focus index
    if (ls.collapsibleCount > 0) {
        if (ls.collapsibleFocusIndex < 0) ls.collapsibleFocusIndex = 0;
        if (ls.collapsibleFocusIndex >= ls.collapsibleCount) ls.collapsibleFocusIndex = ls.collapsibleCount - 1;
    } else {
        ls.collapsibleFocusIndex = -1;
    }

    // Footer nav hint
    ls.footer.collapsibleNavActive = (ls.collapsibleCount > 0 && !isStreaming_);

    // Text selection
    ls.selectionActive = selectionActive_;
    ls.selectionStartY = selectionHighlightStartY_;
    ls.selectionEndY = selectionHighlightEndY_;
}

// ========== Constructor / Destructor ==========

FtxuiRepl::FtxuiRepl() = default;

FtxuiRepl::~FtxuiRepl() {
    escLog("[LIFETIME] ~FtxuiRepl destructor");
    running_ = false;
    stopRefreshThread();
    escLog("[LIFETIME] ~FtxuiRepl destructor complete");
}

// ========== Thread-safe message operations ==========

void FtxuiRepl::addUserMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        ContentBlock cb;
        cb.type = ContentBlock::UserMessage;
        cb.text = std::move(c);
        if (!cb.text.empty()) {
            if (cb.text[0] == '/') cb.userInputType = UserInputType::Command;
            else if (cb.text[0] == '!') cb.userInputType = UserInputType::Bash;
        }
        metricsTurnStartIndex_ = contentBlocks_.size();  // user turn starts at this UserMessage
        apiRoundIndex_ = 0;
        isFirstAnswerBlock_ = true;  // fresh per user turn; survives tool-only API rounds
        userTurnIndex_++;
        contentBlocks_.push_back(std::move(cb));
    });
}

void FtxuiRepl::addAssistantMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        ContentBlock cb;
        cb.type = ContentBlock::AnswerText;
        cb.text = std::move(c);
        contentBlocks_.push_back(std::move(cb));
    });
}

void FtxuiRepl::addToolUseStart(const String& toolName, const String& toolId, const String& input) {
    if (!screen_) return;
    screen_->Post([this, tn = String(toolName), tid = String(toolId), inp = String(input)]() {
        if (!streamingText_.empty()) {
            ContentBlock textCb;
            textCb.type = ContentBlock::AnswerText;
            textCb.text = std::move(streamingText_);
            streamingText_.clear();
            streamingRenderer_.reset();
            contentBlocks_.push_back(std::move(textCb));
        }
        ContentBlock cb;
        cb.type = ContentBlock::ToolProgress;
        cb.toolName = tn;
        cb.toolCallId = tid;
        cb.activity = "Running";
        toolProgressIndices_[tid] = contentBlocks_.size();  // B5: record index before push
        contentBlocks_.push_back(std::move(cb));
    });
}

void FtxuiRepl::addToolUseComplete(const String& toolId, const String& toolInput) {
    // No-op: ToolProgress remains until ToolResult replaces it
}

void FtxuiRepl::addToolResult(const String& toolName, const String& toolId, const String& result,
                               bool isError, bool isRejected, bool isCancelled) {
    if (!screen_) return;
    screen_->Post([this, tn = String(toolName), tid = String(toolId), res = String(result),
                   err = isError, rej = isRejected, can = isCancelled]() {
        if (!streamingText_.empty()) {
            ContentBlock textCb;
            textCb.type = ContentBlock::AnswerText;
            textCb.text = std::move(streamingText_);
            streamingText_.clear();
            streamingRenderer_.reset();
            contentBlocks_.push_back(std::move(textCb));
        }
        // B5: O(1) ToolProgress removal using index map
        if (!tid.empty()) {
            auto it = toolProgressIndices_.find(tid);
            if (it != toolProgressIndices_.end()) {
                size_t idx = it->second;
                if (idx < contentBlocks_.size() &&
                    contentBlocks_[idx].type == ContentBlock::ToolProgress &&
                    contentBlocks_[idx].toolCallId == tid) {
                    contentBlocks_.erase(contentBlocks_.begin() + static_cast<long>(idx));
                    // Shift all indices after the removed position
                    for (auto& [tcid, i] : toolProgressIndices_) {
                        if (i > idx) --i;
                    }
                }
                toolProgressIndices_.erase(it);
            }
        }
        ContentBlock cb;
        cb.type = ContentBlock::ToolResult;
        cb.toolName = tn;
        cb.toolCallId = tid;
        cb.expanded = verboseTools_;
        if (err) {
            cb.summary = ToolResultSummary::error(res);
            cb.resultStatus = ToolResultStatus::Error;
        } else if (can) {
            cb.summary = ToolResultSummary::dim("Interrupted");
            cb.resultStatus = ToolResultStatus::Cancelled;
        } else if (rej) {
            cb.summary = ToolResultSummary::dim("Rejected");
            cb.resultStatus = ToolResultStatus::Rejected;
        } else {
            // Truncate raw result for summary display.
            // The full result is available in the tool result content block
            // and can be expanded via Ctrl+O.
            String summary;
            if (res.empty()) {
                summary = "completed";
            } else {
                // Take first line, max 80 chars
                auto nlPos = res.find('\n');
                summary = (nlPos != String::npos) ? res.substr(0, nlPos) : res;
                if (summary.size() > 80) summary = summary.substr(0, 77) + "...";
            }
            cb.summary = ToolResultSummary::dim(summary);
        }
        contentBlocks_.push_back(std::move(cb));
    });
}

void FtxuiRepl::addToolMessage(const String& toolName, const String& input, const String& result) {
    if (input.empty() && !result.empty()) {
        addToolResult(toolName, "", result, false);
    } else {
        addToolUseStart(toolName, "", input);
    }
}

void FtxuiRepl::addSystemMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        ContentBlock cb;
        cb.type = ContentBlock::AnswerText;
        cb.text = std::move(c);
        cb.dimmed = true;
        contentBlocks_.push_back(std::move(cb));
    });
}

void FtxuiRepl::addErrorMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        ContentBlock cb;
        cb.type = ContentBlock::ErrorMessage;
        cb.text = std::move(c);
        contentBlocks_.push_back(std::move(cb));
    });
}

void FtxuiRepl::addTurnDurationMessage(int durationMs) {
    // Turn duration is now shown in the status bar via TurnMetadata,
    // not in the content area. This method is kept for API compatibility.
}

void FtxuiRepl::clearMessages() {
    if (!screen_) return;
    screen_->Post([this]() {
        contentBlocks_.clear();
    });
}

void FtxuiRepl::setModelInfo(const String& info) {
    if (!screen_) { modelInfo_ = info; return; }
    screen_->Post([this, s = String(info)]() { modelInfo_ = std::move(s); });
}

void FtxuiRepl::setContextInfo(long usedTokens, long maxTokens, double costUsd) {
    if (!screen_) { contextUsedTokens_ = usedTokens; contextMaxTokens_ = maxTokens; costUsd_ = costUsd; return; }
    screen_->Post([this, usedTokens, maxTokens, costUsd]() {
        contextUsedTokens_ = usedTokens;
        contextMaxTokens_ = maxTokens;
        costUsd_ = costUsd;
    });
}

void FtxuiRepl::enableMetricsCollection(const std::string& outputPath) {
    const bool debugMetrics = (std::getenv("CLAUDE_CODE_DEBUG_METRICS") != nullptr &&
                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[0] == '1' &&
                               std::getenv("CLAUDE_CODE_DEBUG_METRICS")[1] == '\0');
    if (debugMetrics) {
        fprintf(stderr, "[DEBUG_METRICS] FtxuiRepl::enableMetricsCollection: path=%s\n",
                outputPath.c_str());
    }
    metricsCollector_ = std::make_unique<TurnMetricsCollector>(outputPath);
    if (debugMetrics) {
        fprintf(stderr, "[DEBUG_METRICS] FtxuiRepl::enableMetricsCollection: collector_initialized=%s\n",
                metricsCollector_ ? "true" : "false");
    }
}

void FtxuiRepl::setTokenCounts(int inputTokens, int outputTokens) {
    if (!screen_) { inputTokens_ = inputTokens; outputTokens_ = outputTokens; return; }
    screen_->Post([this, inputTokens, outputTokens]() {
        inputTokens_ = inputTokens;
        outputTokens_ = outputTokens;
    });
}

// ========== Control ==========

void FtxuiRepl::run() {
    using namespace ftxui;

    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);

    escLog("=== FTXUI starting, ESC debug log active ===");
    spdlog::debug("FTXUI: Building component...");
    auto component = BuildMainComponent();
    spdlog::debug("FTXUI: Creating screen...");
    auto screen = ScreenInteractive::Fullscreen();
    screen_ = &screen;
    // Let Ctrl+C pass through to our CatchEvent handler instead of
    // FTXUI intercepting it and exiting the loop immediately.
    screen.ForceHandleCtrlC(false);
    spdlog::debug("FTXUI: Enabling mouse tracking...");
    screen.TrackMouse();

    spdlog::debug("FTXUI: Starting loop...");

    try {
        screen.Loop(component);
        escLog("[LOOP] screen.Loop() returned normally");
    } catch (const std::exception& e) {
        escLog("[LOOP] screen.Loop() EXCEPTION — exiting");
        spdlog::error("FTXUI loop exception: {}", e.what());
    } catch (...) {
        escLog("[LOOP] screen.Loop() UNKNOWN EXCEPTION — exiting");
    }

    screen_ = nullptr;
    stopRefreshThread();
    escLog("[LOOP] run() exiting, screen teardown complete");
    spdlog::debug("FTXUI: Loop ended");
}

void FtxuiRepl::exit() {
    escLog("[EXIT] FtxuiRepl::exit() called");

    // Stack trace to identify who called exit()
    {
        void* bt[32];
        int n = backtrace(bt, 32);
        int fd = open("/tmp/esc-debug.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            backtrace_symbols_fd(bt, n, fd);
            close(fd);
        }
    }

    running_ = false;
    escLog("[EXIT] running_=false set");
    stopRefreshThread();
    escLog("[EXIT] stopRefreshThread done");
    if (screen_) {
        escLog("[EXIT] calling screen_->Exit()");
        screen_->Exit();
        escLog("[EXIT] screen_->Exit() returned");
    }
    escLog("[EXIT] exit() complete");
}

bool FtxuiRepl::handleCtrlC(std::chrono::steady_clock::time_point now, int timeoutMs) {
    if (ctrlCPending_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastCtrlC_).count();
        if (elapsed < timeoutMs) {
            escLog("[EXIT] handleCtrlC double-press -> exit()");
            exit();
            return true;
        }
        // Timeout expired — fall through to treat as first press
    }
    ctrlCPending_ = true;
    lastCtrlC_ = now;

    // Dedup: don't push another hint if the last block is already one
    static constexpr const char* kCtrlCHint = "Press Ctrl+C again to exit.";
    if (contentBlocks_.empty() ||
        contentBlocks_.back().text != kCtrlCHint) {
        ContentBlock cb;
        cb.type = ContentBlock::AnswerText;
        cb.text = kCtrlCHint;
        cb.dimmed = true;
        contentBlocks_.push_back(std::move(cb));
    }
    return true;
}

// ========== BuildMainComponent — creates AppLayout + event handler ==========

ftxui::Component FtxuiRepl::BuildMainComponent() {
    using namespace ftxui;

    // Initialize the completer with default commands if empty
    if (completer_.currentCompletions().empty()) {
        completer_ = createDefaultCompleter({});
    }

    // Construct the RenderContext with Macaron theme colors
    // Must be done here where FtxuiColors.hpp is visible (not in header)
    static ui::ThemeColors defaultTheme;
    renderContext_ = std::make_unique<ui::RenderContext>(defaultTheme);

    // Link contentBlocks to layoutState (rendered by AppLayout)
    layoutState_.content.contentBlocks = &contentBlocks_;

    // Build the AppLayout renderer component
    auto layoutComp = ui::AppLayoutComponent(layoutState_, *renderContext_);

    // Wrap with CatchEvent for all keyboard/mouse handling
    // This preserves the same event handling as the old MainComponent
    auto* r = this;
    auto* ls = &layoutState_;

    auto eventHandler = CatchEvent([r, ls](Event event) -> bool {
        // Sync state from FtxuiRepl to layoutState_ before processing
        r->syncLayoutState();

        if (event == Event::Escape) {
            escLog("[ESC] CatchEvent ENTRY — event reached FtxuiRepl handler");
        }

        if (event.is_mouse()) {
            auto& mouse = event.mouse();

            // Shift+Left click/drag = custom text selection
            // FTXUI's GetSelection() doesn't work with yframe (coordinate mismatch),
            // so we track selection coordinates ourselves and read from PixelAt().
            if (mouse.shift && mouse.button == Mouse::Left) {
                if (mouse.motion == Mouse::Pressed) {
                    r->selectionActive_ = true;
                    r->selectionStartX_ = mouse.x;
                    r->selectionStartY_ = mouse.y;
                    r->selectionEndX_ = mouse.x;
                    r->selectionEndY_ = mouse.y;
                    r->selectionText_.clear();
                } else if (mouse.motion == Mouse::Moved && r->selectionActive_) {
                    r->selectionEndX_ = mouse.x;
                    r->selectionEndY_ = mouse.y;
                } else if (mouse.motion == Mouse::Released && r->selectionActive_) {
                    r->selectionEndX_ = mouse.x;
                    r->selectionEndY_ = mouse.y;
                    // Extract text from screen pixel buffer
                    if (r->screen_) {
                        int minY = std::min(r->selectionStartY_, r->selectionEndY_);
                        int maxY = std::max(r->selectionStartY_, r->selectionEndY_);
                        int minX = std::min(r->selectionStartX_, r->selectionEndX_);
                        int maxX = std::max(r->selectionStartX_, r->selectionEndX_);
                        String extracted;
                        for (int y = minY; y <= maxY && y < r->screen_->dimy(); ++y) {
                            if (y < 0) continue;
                            int lineStart = (y == minY) ? minX : 0;
                            int lineEnd = (y == maxY) ? maxX : r->screen_->dimx() - 1;
                            lineStart = std::max(0, lineStart);
                            lineEnd = std::min(r->screen_->dimx() - 1, lineEnd);
                            String line;
                            for (int x = lineStart; x <= lineEnd; ++x) {
                                auto& pixel = r->screen_->PixelAt(x, y);
                                if (!pixel.character.empty() && pixel.character != " ") {
                                    line += pixel.character;
                                } else {
                                    line += " ";
                                }
                            }
                            // Trim trailing spaces
                            while (!line.empty() && line.back() == ' ') line.pop_back();
                            if (!line.empty()) {
                                if (!extracted.empty()) extracted += "\n";
                                extracted += line;
                            }
                        }
                        r->selectionText_ = std::move(extracted);
                    }
                    // Keep selectionActive_ true so highlight stays visible
                    // until next click without Shift clears it
                }
                // Store normalized range for rendering highlights
                r->selectionHighlightStartY_ = std::min(r->selectionStartY_, r->selectionEndY_);
                r->selectionHighlightEndY_ = std::max(r->selectionStartY_, r->selectionEndY_);
                return true;
            }
            // Non-Shift click clears selection
            if (mouse.button == Mouse::Left && !mouse.shift && r->selectionActive_) {
                r->selectionActive_ = false;
                r->selectionText_.clear();
            }

            if (mouse.button == Mouse::Left && !r->isStreaming_ && !mouse.shift) {
                if (!mouse.motion) {
                    int clickX = mouse.x - 2;
                    if (clickX >= 0 && static_cast<size_t>(clickX) <= static_cast<int>(ls->input.text.size())) {
                        ls->input.cursorPos = static_cast<size_t>(clickX);
                    } else if (clickX < 0) {
                        ls->input.cursorPos = 0;
                    } else {
                        ls->input.cursorPos = ls->input.text.size();
                    }
                }
                return false;
            }
            if (mouse.button == Mouse::WheelUp) {
                ls->scrollRatio = std::max(0.0f, ls->scrollRatio - 0.02f);
                ls->autoScroll = false;
                r->virtualScroll_.setPinToBottom(false);
                r->virtualScroll_.scrollUp();
                return true;
            }
            if (mouse.button == Mouse::WheelDown) {
                ls->scrollRatio = std::min(1.0f, ls->scrollRatio + 0.02f);
                if (ls->scrollRatio >= 0.95f) {
                    ls->autoScroll = true;
                    r->virtualScroll_.setPinToBottom(true);
                } else {
                    ls->autoScroll = false;
                    r->virtualScroll_.setPinToBottom(false);
                }
                return true;
            }
            return false;
        }

        if (event == Event::CtrlP || event == Event::F5) {
            ls->scrollRatio = std::max(0.0f, ls->scrollRatio - 0.02f);
            ls->autoScroll = false;
            r->virtualScroll_.setPinToBottom(false);
            if (r->virtualScroll_.firstVisibleIndex() > 0) r->virtualScroll_.scrollUp();
            return true;
        }
        if (event == Event::CtrlN || event == Event::F6) {
            ls->scrollRatio = std::min(1.0f, ls->scrollRatio + 0.02f);
            if (ls->scrollRatio >= 0.95f) {
                ls->autoScroll = true;
                r->virtualScroll_.setPinToBottom(true);
            } else {
                ls->autoScroll = false;
                r->virtualScroll_.setPinToBottom(false);
            }
            return true;
        }
        if (event == Event::F7 || event == Event::PageUp) {
            ls->scrollRatio = std::max(0.0f, ls->scrollRatio - 0.2f);
            ls->autoScroll = false;
            r->virtualScroll_.setPinToBottom(false);
            r->virtualScroll_.pageUp(24);
            return true;
        }
        if (event == Event::F8 || event == Event::PageDown) {
            ls->scrollRatio = std::min(1.0f, ls->scrollRatio + 0.2f);
            if (ls->scrollRatio >= 0.95f) {
                ls->autoScroll = true;
                r->virtualScroll_.setPinToBottom(true);
            } else {
                ls->autoScroll = false;
                r->virtualScroll_.setPinToBottom(false);
            }
            return true;
        }
        if (event == Event::End) {
            ls->scrollRatio = 1.0f;
            ls->autoScroll = true;
            r->virtualScroll_.setPinToBottom(true);
            return true;
        }
        if (event == Event::Home) {
            ls->scrollRatio = 0.0f;
            ls->autoScroll = false;
            r->virtualScroll_.setPinToBottom(false);
            r->virtualScroll_.scrollToTop();
            return true;
        }

        // Ctrl+O: toggle expand/collapse for focused collapsible tool result
        // or toggle all thinking/collapsed-groups if no tool result focused.
        // Only active in non-streaming state (matching TS behavior).
        if (event == Event::CtrlO) {
            if (r->isStreaming_) {
                return true;  // consume but ignore during streaming
            }
            if (ls->collapsibleCount > 0 && ls->collapsibleFocusIndex >= 0) {
                // Find the focused collapsible tool result and toggle it
                int focusSeq = 0;
                for (auto& block : r->contentBlocks_) {
                    if ((block.type == ContentBlock::ToolResult &&
                         AnswerPostProcessor::isCollapsibleTool(block.toolName)) ||
                        block.type == ContentBlock::ToolGroup ||
                        block.type == ContentBlock::CollapsedGroup) {
                        if (focusSeq == ls->collapsibleFocusIndex) {
                            block.expanded = !block.expanded;
                            break;
                        }
                        focusSeq++;
                    }
                }
            } else {
                // Fallback: toggle all thinking/collapsed-groups
                r->verboseTools_ = !r->verboseTools_;
                ls->verboseTools = r->verboseTools_;
                for (auto& block : r->contentBlocks_) {
                    if (block.type == ContentBlock::ThinkingBlock ||
                        block.type == ContentBlock::ToolGroup ||
                        block.type == ContentBlock::CollapsedGroup) {
                        block.expanded = r->verboseTools_;
                    }
                }
            }
            return true;
        }

        // [ and ] keys: cycle focus between collapsible tool results
        if (!r->isStreaming_ && ls->collapsibleCount > 0) {
            if (event == Event::Character('[') && r->contentBlocks_.empty() == false) {
                if (ls->input.text.empty()) {
                    ls->collapsibleFocusIndex = (ls->collapsibleFocusIndex <= 0)
                        ? ls->collapsibleCount - 1
                        : ls->collapsibleFocusIndex - 1;
                    return true;
                }
            }
            if (event == Event::Character(']') && r->contentBlocks_.empty() == false) {
                if (ls->input.text.empty()) {
                    ls->collapsibleFocusIndex = (ls->collapsibleFocusIndex + 1) % ls->collapsibleCount;
                    return true;
                }
            }
        }

        // Permission prompt — MUST be checked BEFORE isStreaming_
        if (r->permissionPromptActive_) {
            // Helper: clear permission progress on the pending ToolProgress block
            auto clearPermissionProgress = [r]() {
                for (auto it = r->contentBlocks_.rbegin(); it != r->contentBlocks_.rend(); ++it) {
                    if (it->type == ContentBlock::ToolProgress &&
                        it->activity == "Waiting for permission") {
                        it->activity = "Running";
                        break;
                    }
                }
            };
            // When feedback input is active, route text editing there
            if (r->permissionFeedbackActive_) {
                if (event == Event::Escape) {
                    // Escape in feedback mode: close feedback, stay in prompt
                    escLog("[ESC] permission feedback: closing feedback, staying in prompt");
                    r->permissionFeedbackActive_ = false;
                    ls->permissionFeedbackActive = false;
                    return true;
                }
                if (event == Event::Tab) {
                    // Tab again: close feedback
                    r->permissionFeedbackActive_ = false;
                    ls->permissionFeedbackActive = false;
                    return true;
                }
                if (event == Event::Return) {
                    // Return in feedback mode: confirm the choice with feedback
                    PermissionChoice choices[] = {
                        PermissionChoice::AllowOnce,
                        PermissionChoice::AllowSession,
                        PermissionChoice::AlwaysAllow,
                        PermissionChoice::DenyOnce,
                        PermissionChoice::AlwaysDeny
                    };
                    int idx = std::clamp(r->permissionFocusedIndex_, 0, 4);
                    PermissionChoice choice = choices[idx];
                    r->permissionPromptActive_ = false;
                    ls->permissionActive = false;
                    r->permissionFeedbackActive_ = false;
                    clearPermissionProgress();
                    ls->permissionFeedbackActive = false;

                    {
                        std::lock_guard lock(r->permissionMutex_);
                        r->permissionResult_ = choice;
                        r->permissionFeedbackResult_ = r->permissionFeedbackText_;
                        r->permissionAnswered_ = true;
                    }
                    r->permissionCv_.notify_one();
                    return true;
                }
                if (event == Event::Backspace && r->permissionFeedbackCursorPos_ > 0) {
                    size_t& pos = r->permissionFeedbackCursorPos_;
                    String& txt = r->permissionFeedbackText_;
                    size_t deleteStart = pos;
                    while (deleteStart > 0) {
                        deleteStart--;
                        auto c = static_cast<unsigned char>(txt[deleteStart]);
                        if ((c & 0xC0) != 0x80) break;
                    }
                    txt.erase(deleteStart, pos - deleteStart);
                    pos = deleteStart;
                    ls->permissionFeedbackText = txt;
                    ls->permissionFeedbackCursorPos = pos;
                    return true;
                }
                if (event == Event::ArrowLeft && r->permissionFeedbackCursorPos_ > 0) {
                    size_t& pos = r->permissionFeedbackCursorPos_;
                    while (pos > 0) {
                        pos--;
                        auto c = static_cast<unsigned char>(r->permissionFeedbackText_[pos]);
                        if ((c & 0xC0) != 0x80) break;
                    }
                    ls->permissionFeedbackCursorPos = pos;
                    return true;
                }
                if (event == Event::ArrowRight && r->permissionFeedbackCursorPos_ < r->permissionFeedbackText_.size()) {
                    size_t& pos = r->permissionFeedbackCursorPos_;
                    while (pos < r->permissionFeedbackText_.size()) {
                        pos++;
                        if (pos >= r->permissionFeedbackText_.size()) break;
                        auto c = static_cast<unsigned char>(r->permissionFeedbackText_[pos]);
                        if ((c & 0xC0) != 0x80) break;
                    }
                    ls->permissionFeedbackCursorPos = pos;
                    return true;
                }
                if (event.is_character()) {
                    size_t& pos = r->permissionFeedbackCursorPos_;
                    String& txt = r->permissionFeedbackText_;
                    txt.insert(pos, event.character());
                    pos += event.character().size();
                    ls->permissionFeedbackText = txt;
                    ls->permissionFeedbackCursorPos = pos;
                    return true;
                }
                // Consume all other events while in feedback mode
                return true;
            }

            // Normal permission prompt navigation (no feedback active)
            if (event == Event::ArrowUp || event == Event::CtrlP) {
                r->permissionFocusedIndex_ = (r->permissionFocusedIndex_ > 0)
                    ? r->permissionFocusedIndex_ - 1 : 4;
                ls->permissionFocusedIndex = r->permissionFocusedIndex_;
                return true;
            }
            if (event == Event::ArrowDown || event == Event::CtrlN) {
                r->permissionFocusedIndex_ = (r->permissionFocusedIndex_ < 4)
                    ? r->permissionFocusedIndex_ + 1 : 0;
                ls->permissionFocusedIndex = r->permissionFocusedIndex_;
                return true;
            }
            if (event == Event::Tab) {
                // Tab to amend: activate feedback text input
                r->permissionFeedbackActive_ = true;
                ls->permissionFeedbackActive = true;
                // Reset feedback text when entering for first time
                if (r->permissionFeedbackText_.empty()) {
                    r->permissionFeedbackCursorPos_ = 0;
                    ls->permissionFeedbackCursorPos = 0;
                }
                return true;
            }
            if (event == Event::Return) {
                PermissionChoice choices[] = {
                    PermissionChoice::AllowOnce,
                    PermissionChoice::AllowSession,
                    PermissionChoice::AlwaysAllow,
                    PermissionChoice::DenyOnce,
                    PermissionChoice::AlwaysDeny
                };
                int idx = std::clamp(r->permissionFocusedIndex_, 0, 4);
                PermissionChoice choice = choices[idx];
                r->permissionPromptActive_ = false;
                ls->permissionActive = false;
                clearPermissionProgress();

                {
                    std::lock_guard lock(r->permissionMutex_);
                    r->permissionResult_ = choice;
                    r->permissionFeedbackResult_ = r->permissionFeedbackText_;
                    r->permissionAnswered_ = true;
                }
                r->permissionCv_.notify_one();
                return true;
            }
            if (event == Event::Escape) {
                escLog("[ESC] permission prompt: deny once, closing prompt");
                r->permissionPromptActive_ = false;
                ls->permissionActive = false;
                clearPermissionProgress();
                {
                    std::lock_guard lock(r->permissionMutex_);
                    r->permissionResult_ = PermissionChoice::DenyOnce;
                    r->permissionFeedbackResult_.clear();
                    r->permissionAnswered_ = true;
                }
                r->permissionCv_.notify_one();
                return true;
            }
            return true;
        }

        // --- Ctrl+C: unified double-press exit (idle and streaming) ---
        // ESC is for cancel; Ctrl+C is ONLY for exit confirmation.
        if (event == Event::CtrlC) {
            return r->handleCtrlC(std::chrono::steady_clock::now());
        }

        if (r->isStreaming_) {
            if (event == Event::Escape) {
                try {
                escLog("[ESC] streaming: cancelling turn");
                if (r->onCancel_) {
                    escLog("[ESC] streaming: calling onCancel_");
                    r->onCancel_();
                    escLog("[ESC] streaming: onCancel_ returned");
                }

                escLog("[ESC] cleanup: before isStreaming=false");
                r->isStreaming_ = false;
                escLog("[ESC] cleanup: after isStreaming=false");

                // Clear pipeline status metadata so the footer/status bar
                // stops showing "● Running..." and stale token counts.
                r->newPipelineStatusMetadata_ = TurnMetadata{};
                r->outputTokens_ = 0;
                r->inputTokens_ = 0;

                escLog("[ESC] cleanup: before isThinking=false");
                r->isThinking_ = false;
                escLog("[ESC] cleanup: after isThinking=false");

                escLog("[ESC] cleanup: before turnStarted=false");
                r->turnStarted_ = false;
                escLog("[ESC] cleanup: after turnStarted=false");

                escLog("[ESC] cleanup: before move streamingText");
                String partial = std::move(r->streamingText_);
                escLog("[ESC] cleanup: after move streamingText");

                escLog("[ESC] cleanup: before clear streamingText_");
                r->streamingText_.clear();
                escLog("[ESC] cleanup: after clear streamingText_");

                escLog("[ESC] cleanup: before reset streamingRenderer_");
                r->streamingRenderer_.reset();
                escLog("[ESC] cleanup: after reset streamingRenderer_");

                if (!partial.empty()) {
                    escLog("[ESC] cleanup: pushing partial AnswerText block");
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.text = std::move(partial);
                    r->contentBlocks_.push_back(std::move(cb));
                    escLog("[ESC] cleanup: partial AnswerText block pushed");
                }
                {
                    escLog("[ESC] cleanup: before push Cancelled block");
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.text = "Cancelled";
                    cb.dimmed = true;
                    r->contentBlocks_.push_back(std::move(cb));
                    escLog("[ESC] cleanup: Cancelled block pushed");
                }

                escLog("[ESC] cleanup: before stopRefreshThread");
                r->stopRefreshThread();
                escLog("[ESC] cleanup: after stopRefreshThread");
                escLog("[ESC] cleanup: DONE, returning true");
                } catch (const std::exception& e) {
                    escLog("[ESC] streaming: std::exception caught");
                } catch (...) {
                    escLog("[ESC] streaming: unknown exception caught");
                }
                return true;
            }
            // Explicitly block Enter/Return during streaming to prevent
            // new user prompt submission while a turn is active.
            // Text input (character events) still passes through to the
            // Input component so the user can type, but submitting is
            // gated here until finishStream() runs.
            if (event == Event::Return) {
                return true;
            }
            return false;
        }

        if (event == Event::Return) {
            if (!ls->input.text.empty()) {
                r->ctrlCPending_ = false;
                String current = ls->input.text;
                {
                    // Log the submitted text (truncated for safety)
                    char buf[256];
                    String preview = current.size() > 40 ? current.substr(0, 40) + "..." : current;
                    snprintf(buf, sizeof(buf), "[RETURN] submitted input: '%s' (first char: 0x%02x)",
                             preview.c_str(), static_cast<unsigned char>(current[0]));
                    escLog(buf);
                }
                ls->input.text.clear();
                ls->input.cursorPos = 0;
                ContentBlock userCb;
                userCb.type = ContentBlock::UserMessage;
                userCb.text = current;
                // Detect user input type
                if (!current.empty()) {
                    if (current[0] == '/') {
                        userCb.userInputType = UserInputType::Command;
                    } else if (current[0] == '!') {
                        userCb.userInputType = UserInputType::Bash;
                    }
                }
                r->metricsTurnStartIndex_ = r->contentBlocks_.size();  // user turn starts here
                r->apiRoundIndex_ = 0;
                r->isFirstAnswerBlock_ = true;  // fresh per user turn
                r->userTurnIndex_++;
                r->contentBlocks_.push_back(std::move(userCb));
                r->isStreaming_ = true;
                r->isThinking_ = true;
                r->streamingText_.clear();
                r->streamingRenderer_.reset();
                r->thinkingSummary_.clear();
                r->thinkingVerb_.clear();     // fresh verb per user turn
                r->startTime_ = std::chrono::steady_clock::now();
                ls->autoScroll = true;
                ls->scrollRatio = 1.0f;
                ls->tickCounter = 0;

                r->completer_.addHistory(current);
                r->completer_.clearCompletions();
                ls->completions.clear();
                ls->completionIndex = 0;
                ls->lastCompletionInput.clear();

                r->startRefreshThread();

                if (!current.empty() && current[0] == '/' && r->onCommand_) {
                    escLog("[RETURN] routing to onCommand_");
                    r->onCommand_(current);
                } else if (r->onSubmit_) {
                    escLog("[RETURN] routing to onSubmit_");
                    r->onSubmit_(current);
                }
            }
            return true;
        }
        // Shift+Tab: cycle permission mode
        if (event == Event::TabReverse) {
            const char* modes[] = {"default", "acceptEdits", "auto", "bypassPermissions", "dontAsk", "plan"};
            String current = r->currentMode_;
            int idx = 0;
            for (int i = 0; i < 6; ++i) {
                if (modes[i] == current) { idx = i; break; }
            }
            idx = (idx + 1) % 6;
            r->currentMode_ = modes[idx];
            r->modeHintDismissed_ = false;
            AppState::instance().setPermissionMode(modes[idx]);
            return true;
        }
        // Tab completion
        if (event == Event::Tab) {
            const auto& completions = r->completer_.currentCompletions();
            if (!completions.empty()) {
                if (completions.size() == 1) {
                    ls->input.text = completions[0];
                    ls->input.cursorPos = ls->input.text.size();
                    r->completer_.clearCompletions();
                    ls->completions.clear();
                    ls->completionIndex = 0;
                    ls->lastCompletionInput.clear();
                } else if (ls->completionIndex < completions.size()) {
                    if (ls->lastCompletionInput == ls->input.text) {
                        ls->completionIndex = (ls->completionIndex + 1) % completions.size();
                        ls->input.text = completions[ls->completionIndex];
                        ls->input.cursorPos = ls->input.text.size();
                    } else {
                        String prefix = r->completer_.commonPrefix(ls->input.text);
                        if (prefix != ls->input.text) {
                            ls->input.text = prefix;
                            ls->input.cursorPos = ls->input.text.size();
                            ls->lastCompletionInput = ls->input.text;
                            r->completer_.updateCompletions(ls->input.text, ls->input.cursorPos);
                            ls->completionIndex = 0;
                        } else {
                            ls->completionIndex = 0;
                            ls->input.text = completions[ls->completionIndex];
                            ls->input.cursorPos = ls->input.text.size();
                            ls->lastCompletionInput = ls->input.text;
                        }
                    }
                }
                return true;
            }
            return true;
        }
        // Arrow up/down in completion list
        if (event == Event::ArrowUp && !r->completer_.currentCompletions().empty()) {
            if (ls->completionIndex > 0) {
                ls->completionIndex--;
            } else {
                ls->completionIndex = r->completer_.currentCompletions().size() - 1;
            }
            return true;
        }
        if (event == Event::ArrowDown && !r->completer_.currentCompletions().empty()) {
            ls->completionIndex = (ls->completionIndex + 1) % r->completer_.currentCompletions().size();
            return true;
        }
        if (event.is_character()) {
            ls->input.text.insert(ls->input.cursorPos, event.character());
            ls->input.cursorPos += event.character().size();
            r->completer_.updateCompletions(ls->input.text, ls->input.cursorPos);
            ls->completionIndex = 0;
            ls->lastCompletionInput = ls->input.text;
            return true;
        }
        if (event == Event::ArrowLeft && ls->input.cursorPos > 0) {
            while (ls->input.cursorPos > 0) {
                ls->input.cursorPos--;
                auto c = static_cast<unsigned char>(ls->input.text[ls->input.cursorPos]);
                if ((c & 0xC0) != 0x80) break;
            }
            return true;
        }
        if (event == Event::ArrowRight && ls->input.cursorPos < ls->input.text.size()) {
            while (ls->input.cursorPos < ls->input.text.size()) {
                ls->input.cursorPos++;
                if (ls->input.cursorPos >= ls->input.text.size()) break;
                auto c = static_cast<unsigned char>(ls->input.text[ls->input.cursorPos]);
                if ((c & 0xC0) != 0x80) break;
            }
            return true;
        }
        if (event == Event::Home) {
            ls->input.cursorPos = 0;
            return true;
        }
        if (event == Event::End) {
            ls->input.cursorPos = ls->input.text.size();
            return true;
        }
        if (event == Event::Backspace && ls->input.cursorPos > 0) {
            size_t deleteStart = ls->input.cursorPos;
            while (deleteStart > 0) {
                deleteStart--;
                auto c = static_cast<unsigned char>(ls->input.text[deleteStart]);
                if ((c & 0xC0) != 0x80) break;
            }
            ls->input.text.erase(deleteStart, ls->input.cursorPos - deleteStart);
            ls->input.cursorPos = deleteStart;
            r->completer_.updateCompletions(ls->input.text, ls->input.cursorPos);
            ls->completionIndex = 0;
            ls->lastCompletionInput = ls->input.text;
            return true;
        }
        if (event == Event::Delete && ls->input.cursorPos < ls->input.text.size()) {
            size_t deleteEnd = ls->input.cursorPos;
            while (deleteEnd < ls->input.text.size()) {
                deleteEnd++;
                if (deleteEnd >= ls->input.text.size()) break;
                auto c = static_cast<unsigned char>(ls->input.text[deleteEnd]);
                if ((c & 0xC0) != 0x80) break;
            }
            ls->input.text.erase(ls->input.cursorPos, deleteEnd - ls->input.cursorPos);
            r->completer_.updateCompletions(ls->input.text, ls->input.cursorPos);
            ls->completionIndex = 0;
            ls->lastCompletionInput = ls->input.text;
            return true;
        }
        // Ctrl+Y: copy selected text to clipboard
        // Uses custom selectionText_ (from Shift+drag) instead of FTXUI's
        // GetSelection() which doesn't work correctly with yframe.
        if (event == Event::CtrlY && r->screen_) {
            String selected = r->selectionText_;
            // Fallback to FTXUI's GetSelection for non-yframe content
            if (selected.empty()) {
                selected = r->screen_->GetSelection();
            }
            if (!selected.empty()) {
                auto task = r->screen_->WithRestoredIO([&selected]() {
                    FILE* pb = popen("pbcopy", "w");
                    if (!pb) pb = popen("xclip -selection clipboard", "w");
                    if (pb) {
                        fwrite(selected.data(), 1, selected.size(), pb);
                        pclose(pb);
                    }
                });
                r->screen_->Post(std::move(task));
                r->screen_->Post([r, len = selected.size()]() {
                    ContentBlock cb;
                    cb.type = ContentBlock::AnswerText;
                    cb.text = "Copied " + std::to_string(len) + " chars to clipboard";
                    cb.dimmed = true;
                    r->contentBlocks_.push_back(std::move(cb));
                });
                // Clear selection after copy
                r->selectionActive_ = false;
                r->selectionText_.clear();
            }
            return true;
        }
        if (event == Event::Escape) {
            escLog("[ESC] idle: consuming escape");
            if (!r->completer_.currentCompletions().empty()) {
                r->completer_.clearCompletions();
                ls->completions.clear();
                ls->completionIndex = 0;
                ls->lastCompletionInput.clear();
                return true;
            }
            return true;
        }
        // Log any unhandled event that falls through to help debug ESC issue
        if (event == Event::Escape) {
            escLog("[ESC] WARNING: ESC fell through all handlers!");
        }
        return false;
    });

    // Compose: layout wrapped with event handler + a pre-render sync
    // We use a wrapper Renderer that syncs state before delegating to layoutComp
    auto syncRenderer = Renderer(layoutComp, [r, innerComp = layoutComp] {
        r->syncLayoutState();
        return innerComp->Render();
    });

    return syncRenderer | eventHandler;
}

} // namespace claude

#endif // HAS_FTXUI
