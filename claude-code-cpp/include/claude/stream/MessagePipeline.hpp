#pragma once

#include "claude/stream/ContentBlock.hpp"
#include "claude/stream/MessageTypes.hpp"
#include <vector>
#include <functional>

namespace claude {

/// Multi-pass normalization pipeline that transforms flat ContentBlock lists
/// into properly grouped, collapsed, and reordered display-ready blocks.
///
/// Pipeline passes (run in order):
///   1. reorderToolTrails         — push tool_uses before their matching results
///   2. groupToolResultPairs       — pair adjacent ToolProgress+ToolResult into ToolGroup
///   3. groupConsecutiveToolUses   — group consecutive same-type tool_use blocks
///   4. collapseReadSearchGroups   — collapse consecutive read/search operations
///   5. collapseBackgroundBash     — collapse background bash start/end pairs
///   6. collapseHookSummaries      — merge consecutive hook system messages
///   7. collapseTeammateShutdowns  — collapse agent/teammate shutdown sequences
class MessagePipeline {
public:
    /// Configuration flags for which passes to run
    struct Config {
        bool reorderToolTrails = true;
        bool groupToolResultPairs = true;
        bool groupConsecutiveToolUses = true;
        bool collapseReadSearch = true;
        bool collapseBackgroundBash = true;
        bool collapseHookSummaries = true;
        bool collapseTeammateShutdowns = true;
        bool buildLookups = true;
        bool verbose = false;  // skip grouping when true (show everything expanded)
    };

    MessagePipeline() = default;
    explicit MessagePipeline(Config config) : config_(std::move(config)) {}

    /// Run all enabled pipeline passes. Returns the processed blocks.
    /// The lookups are available via lookups() after processing.
    std::vector<ContentBlock> process(std::vector<ContentBlock> blocks);

    /// Individual passes (public for testing)
    std::vector<ContentBlock> reorderToolTrails(std::vector<ContentBlock> blocks);
    std::vector<ContentBlock> groupToolResultPairs(std::vector<ContentBlock> blocks);
    std::vector<ContentBlock> groupConsecutiveToolUses(std::vector<ContentBlock> blocks);
    std::vector<ContentBlock> collapseReadSearchGroups(std::vector<ContentBlock> blocks);
    std::vector<ContentBlock> collapseBackgroundBash(std::vector<ContentBlock> blocks);
    std::vector<ContentBlock> collapseHookSummaries(std::vector<ContentBlock> blocks);
    std::vector<ContentBlock> collapseTeammateShutdowns(std::vector<ContentBlock> blocks);

    /// Build lookup tables from the processed blocks.
    void buildLookups(const std::vector<ContentBlock>& blocks);

    /// Access the built lookups (valid after process() or buildLookups())
    const MessageLookups& lookups() const { return lookups_; }

    /// Set tool classification callback. Called with toolName, returns whether
    /// the tool is collapsible / search / read / list.
    using ToolClassifier = std::function<bool(const String& toolName, int category)>;
    // Categories: 0=collapsible, 1=search, 2=read, 3=list, 4=memory
    void setToolClassifier(ToolClassifier cb) { toolClassifier_ = std::move(cb); }

    /// Check if an AnswerText block is inter-tool narration (should not break groups).
    bool isToolNarration(const ContentBlock& block) const;

    /// P6-P2b: Dim all AnswerText blocks identified as inter-tool narration
    /// in [startIndex, blocks.size()).  Only affects non-dimmed blocks —
    /// already-dimmed blocks are left unchanged (idempotent, current-turn scoped).
    void dimToolNarration(std::vector<ContentBlock>& blocks,
                          size_t startIndex = 0) const;

private:
    Config config_;
    MessageLookups lookups_;
    ToolClassifier toolClassifier_;

    // ---- collapseReadSearchGroups helpers ----

    /// Determine if a block is collapsible for the read/search group pass.
    bool isCollapsibleBlock(const ContentBlock& block) const;

    /// Determine the category of a collapsible block.
    GroupAccumulator::Category categorizeBlock(const ContentBlock& block) const;

    /// Build the summary text for a collapsed group.
    static String buildGroupSummary(const GroupAccumulator& acc, bool hasContentAfter);

    /// Build the summary text from a finalized GroupAccumulator.
    static String buildFinalizedSummary(const GroupAccumulator& acc);

    /// Build the active (in-progress) summary text.
    static String buildActiveSummary(const GroupAccumulator& acc);

    /// Detect whether there is meaningful content after the given index.
    static bool hasContentAfterIndex(const std::vector<ContentBlock>& blocks, size_t index);

    /// Check if a block "breaks" the read/search group (non-collapsible, needs flush).
    bool isGroupBreaker(const ContentBlock& block) const;
};

} // namespace claude
