#pragma once

#include "claude/stream/DisplayEvent.hpp"
#include "claude/stream/ContentBlock.hpp"
#include "claude/stream/MessagePipeline.hpp"
#include "claude/metrics/TurnMetricsCollector.hpp"
#include <vector>
#include <map>
#include <memory>
#include <algorithm>

namespace claude {

/// Builds a ContentBlock tree from DisplayEvents, runs MessagePipeline,
/// and feeds TurnMetricsCollector — all without any FTXUI rendering.
///
/// This is a headless mirror of FtxuiRepl's handleDisplayEvent logic.
/// It exists solely so `-p` mode (runOnce) can collect the same metrics
/// as the FTXUI interactive mode, without pulling in FTXUI dependencies.
///
/// Only instantiated when CLAUDE_CODE_METRICS env var is set.
class HeadlessContentBlockAccumulator {
public:
    HeadlessContentBlockAccumulator() = default;
    ~HeadlessContentBlockAccumulator() { flushMetrics(); }

    void handleDisplayEvent(DisplayEvent&& ev);
    void enableMetricsCollection(const std::string& outputPath);
    void flushMetrics();

    // Exposed for testing
    const std::vector<ContentBlock>& contentBlocks() const { return contentBlocks_; }

private:
    // ===== Content block tree (mirrors FtxuiRepl) =====
    std::vector<ContentBlock> contentBlocks_;
    String streamingText_;
    String thinkingText_;
    String thinkingSummary_;
    String modelInfo_ = "headless";
    bool isStreaming_ = false;
    bool isFirstAnswerBlock_ = true;

    // ===== Pipeline state (mirrors FtxuiRepl) =====
    uint64_t nextStableId_ = 1;
    size_t lastStableIndex_ = 0;
    size_t currentTurnStartIndex_ = 0;
    int userTurnIndex_ = 0;  // logical user turn id, incremented per UserMessage
    std::vector<size_t> turnBoundaries_;
    std::map<String, size_t> toolProgressIndices_;
    TurnMetadata newPipelineStatusMetadata_;
    static constexpr size_t MAX_BLOCKS = 2000;

    // ===== Pipeline components (own instances) =====
    MessagePipeline messagePipeline_;
    bool useExternalPostProcessor_ = false;
    bool verboseTools_ = false;  // headless: always collapsed for consistent metrics

    // ===== Metrics (optional) =====
    std::unique_ptr<TurnMetricsCollector> metricsCollector_;

    void runIncrementalPipeline();
};

// ========== Implementation ==========

inline void HeadlessContentBlockAccumulator::handleDisplayEvent(DisplayEvent&& ev) {
    switch (ev.type) {
        case DisplayEventType::TextParagraph:
        case DisplayEventType::TextPartial: {
            if (!isStreaming_) break;
            if (!ev.text.empty()) {
                streamingText_ += ev.text;
            }
            break;
        }

        case DisplayEventType::ToolProgress: {
            ContentBlock cb;
            cb.type = ContentBlock::ToolProgress;
            cb.toolName = ev.toolName;
            cb.toolCallId = ev.toolCallId;
            cb.activity = std::move(ev.activity);
            cb.stableId = nextStableId_++;
            // Track index for O(1) replacement by ToolResult
            toolProgressIndices_[ev.toolCallId] = contentBlocks_.size();
            contentBlocks_.push_back(std::move(cb));
            // No incremental pipeline — headless doesn't need progressive UI
            break;
        }

        case DisplayEventType::ToolResult: {
            // Commit any pending streaming text before the tool result.
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
            if (cb.summary.isError) {
                cb.resultStatus = ToolResultStatus::Error;
            } else if (cb.summary.isDim) {
                if (cb.summary.primaryText.find("Interrupted") != String::npos) {
                    cb.resultStatus = ToolResultStatus::Cancelled;
                } else if (cb.summary.primaryText.find("Rejected") != String::npos) {
                    cb.resultStatus = ToolResultStatus::Rejected;
                }
            }

            // O(1) in-place ToolProgress → ToolResult replacement.
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
            // Headless: no incremental pipeline — full pipeline runs at AnswerEnd
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
            // Headless: no incremental pipeline — full pipeline runs at AnswerEnd
            break;
        }

        case DisplayEventType::SystemNotice:
            // Headless: no-op (no status bar to update)
            break;

        case DisplayEventType::Tombstone: {
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
            // [METRICS] Collect metrics from the PREVIOUS turn.
            // ToolResult events often arrive AFTER AnswerEnd because tool
            // execution happens after model streaming completes. We defer
            // metrics collection to here so late-arriving tools are captured.
            if (metricsCollector_ && currentTurnStartIndex_ < contentBlocks_.size()) {
                // Run full pipeline to process late-arriving tool blocks
                contentBlocks_ = messagePipeline_.process(std::move(contentBlocks_));
                auto m = metricsCollector_->analyze(
                    contentBlocks_, currentTurnStartIndex_, modelInfo_, userTurnIndex_);
                metricsCollector_->write(std::move(m));
            }

            // Remove stale TurnDuration blocks from previous API calls
            contentBlocks_.erase(
                std::remove_if(contentBlocks_.begin(), contentBlocks_.end(),
                    [](const ContentBlock& b) { return b.type == ContentBlock::TurnDuration; }),
                contentBlocks_.end()
            );
            isStreaming_ = true;
            isFirstAnswerBlock_ = true;
            streamingText_.clear();
            currentTurnStartIndex_ = contentBlocks_.size();
            toolProgressIndices_.clear();
            lastStableIndex_ = 0;
            break;

        case DisplayEventType::AnswerEnd: {
            // Commit remaining streaming text
            if (!streamingText_.empty()) {
                ContentBlock cb;
                cb.type = ContentBlock::AnswerText;
                cb.isFirst = isFirstAnswerBlock_;
                cb.text = std::move(streamingText_);
                isFirstAnswerBlock_ = false;
                streamingText_.clear();
                cb.stableId = nextStableId_++;
                contentBlocks_.push_back(std::move(cb));
            }
            for (auto& [callId, idx] : toolProgressIndices_) {
                if (idx < contentBlocks_.size() &&
                    contentBlocks_[idx].type == ContentBlock::ToolProgress) {
                    contentBlocks_[idx].type = ContentBlock::ToolResult;
                    contentBlocks_[idx].summary = ToolResultSummary::dim("Interrupted");
                    contentBlocks_[idx].resultStatus = ToolResultStatus::Cancelled;
                }
            }
            toolProgressIndices_.clear();

            // Run full pipeline (headless: no AnswerPostProcessor, run directly)
            if (!useExternalPostProcessor_) {
                contentBlocks_ = messagePipeline_.process(std::move(contentBlocks_));
            }
            lastStableIndex_ = contentBlocks_.size();

            // Insert collapsed ThinkingBlock
            if (!thinkingText_.empty()) {
                auto existingThink = std::find_if(contentBlocks_.begin(), contentBlocks_.end(),
                    [](const ContentBlock& b) { return b.type == ContentBlock::ThinkingBlock; });
                if (existingThink != contentBlocks_.end()) {
                    existingThink->detailText += "\n\n" + thinkingText_;
                } else {
                    ContentBlock thinkBlock;
                    thinkBlock.type = ContentBlock::ThinkingBlock;
                    thinkBlock.detailText = std::move(thinkingText_);
                    thinkBlock.expanded = false;
                    thinkBlock.text = thinkingSummary_.empty()
                        ? "Thinking..." : thinkingSummary_;
                    thinkBlock.stableId = nextStableId_++;
                    auto it = contentBlocks_.begin();
                    while (it != contentBlocks_.end() &&
                           (it->type == ContentBlock::UserMessage ||
                            it->type == ContentBlock::ThinkingBlock)) {
                        ++it;
                    }
                    contentBlocks_.insert(it, std::move(thinkBlock));
                }
                thinkingSummary_.clear();
                thinkingText_.clear();
            }

            // Insert turn duration block
            {
                auto& meta = newPipelineStatusMetadata_;
                if (!meta.durationStr.empty() || meta.outputTokens > 0) {
                    ContentBlock td;
                    td.type = ContentBlock::TurnDuration;
                    td.text = meta.durationStr;
                    td.stableId = nextStableId_++;
                    if (meta.outputTokens > 0) {
                        auto fmtK = [](int64_t n) -> String {
                            if (n >= 1'000) return std::to_string(n / 100) + "." +
                                std::to_string((n % 100) / 10) + "K";
                            return std::to_string(n);
                        };
                        if (!td.text.empty()) td.text += " · ";
                        td.text += fmtK(meta.outputTokens) + " tokens";
                    }
                    if (!meta.costStr.empty()) {
                        td.text += " · " + meta.costStr;
                    }
                    contentBlocks_.push_back(std::move(td));
                }
            }

            // ===== Metrics hook moved to next AnswerStart =====
            // ToolResult events arrive after AnswerEnd (tool execution
            // follows model streaming). Metrics are now collected at
            // the FOLLOWING AnswerStart via flushMetrics() to capture
            // all late-arriving tools for the turn.

            isStreaming_ = false;
            turnBoundaries_.push_back(contentBlocks_.size());

            // Trim old blocks
            if (contentBlocks_.size() > MAX_BLOCKS) {
                size_t toRemove = contentBlocks_.size() - MAX_BLOCKS / 2;
                contentBlocks_.erase(
                    contentBlocks_.begin(),
                    contentBlocks_.begin() + static_cast<long>(toRemove));
                for (auto& b : turnBoundaries_) {
                    b = (b > toRemove) ? (b - toRemove) : 0;
                }
                currentTurnStartIndex_ = (currentTurnStartIndex_ > toRemove)
                    ? (currentTurnStartIndex_ - toRemove) : 0;
            }
            break;
        }

        case DisplayEventType::ThinkingBlock:
            if (!ev.thinkingText.empty()) {
                thinkingText_ += ev.thinkingText;
            }
            break;

        case DisplayEventType::TurnMetadata:
            if (!ev.metadata.modelName.empty()) {
                modelInfo_ = ev.metadata.modelName;
            }
            newPipelineStatusMetadata_ = std::move(ev.metadata);
            break;
    }
}

inline void HeadlessContentBlockAccumulator::runIncrementalPipeline() {
    if (!useExternalPostProcessor_ && lastStableIndex_ < contentBlocks_.size()) {
        // Run incremental pipeline on the unstable tail only
        // (Mirrors FtxuiRepl::runIncrementalPipeline behavior)
        std::vector<ContentBlock> tail(
            contentBlocks_.begin() + static_cast<long>(lastStableIndex_),
            contentBlocks_.end());
        tail = messagePipeline_.process(std::move(tail));
        contentBlocks_.erase(
            contentBlocks_.begin() + static_cast<long>(lastStableIndex_),
            contentBlocks_.end());
        for (auto& b : tail) {
            contentBlocks_.push_back(std::move(b));
        }
    }
}

inline void HeadlessContentBlockAccumulator::enableMetricsCollection(const std::string& outputPath) {
    metricsCollector_ = std::make_unique<TurnMetricsCollector>(outputPath);
}

inline void HeadlessContentBlockAccumulator::flushMetrics() {
    if (!metricsCollector_) return;
    if (currentTurnStartIndex_ >= contentBlocks_.size()) return;

    // Run full pipeline to process any unprocessed blocks
    contentBlocks_ = messagePipeline_.process(std::move(contentBlocks_));
    lastStableIndex_ = contentBlocks_.size();

    auto m = metricsCollector_->analyze(
        contentBlocks_, currentTurnStartIndex_, modelInfo_, userTurnIndex_);
    metricsCollector_->write(std::move(m));
}

} // namespace claude
