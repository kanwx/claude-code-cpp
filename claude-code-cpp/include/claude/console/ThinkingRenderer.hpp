#pragma once

#include "../core/Types.hpp"

#include <chrono>
#include <ostream>

namespace claude {

/// Thinking depth level — matches original TS design
enum class ThinkingDepth {
    Normal,      // Standard thinking
    Extended,    // Extended/ultrathink
    Adaptive     // Budget-aware adaptive depth
};

/// Thinking visibility levels — matches original TS four-layer design
enum class ThinkingVisibility {
    Hidden,     // completely invisible (transcript mode, post-timeout)
    Collapsed,  // single line: "∴ Thinking" + expand hint
    Expanded,   // full content, Markdown dim rendering
    Streaming   // live thinking content, always visible
};

/// Determine thinking visibility based on context
ThinkingVisibility getThinkingVisibility(
    bool verboseMode,
    bool transcriptMode,
    bool isStreaming,
    bool isLastInTurn,
    std::chrono::steady_clock::time_point streamEndTime);

/// Thinking content renderer — matches TS Claude Code style
///
/// All thinking text uses ∴ (U+2234 "Therefore") prefix, dim styling,
/// no box-drawing borders or rainbow colors.
///
/// Collapsed mode:
///   ∴ Thinking
///
/// Expanded mode:
///   ∴ Thinking…
///     <dim content line 1>
///     <dim content line 2>
///   ∴ Thinking (5s · 1K tokens)
class ThinkingRenderer {
public:
    explicit ThinkingRenderer(std::ostream& out, int terminalWidth = 80);

    /// Render a complete thinking block
    /// @param thinking Full thinking text
    /// @param collapsed If true, show only summary
    /// @param summary Summary text for collapsed mode
    /// @param depth Thinking depth level
    void render(const String& thinking, bool collapsed = true,
                const String& summary = "", ThinkingDepth depth = ThinkingDepth::Normal);

    /// Render the start of a thinking block
    /// @param depth Thinking depth level
    void renderStart(ThinkingDepth depth = ThinkingDepth::Normal);

    /// Render a streaming content line (between renderStart/renderEnd)
    void renderContent(const String& line);

    /// Render the end of a thinking block
    /// @param durationMs Duration of thinking in milliseconds
    /// @param tokenCount Number of thinking tokens used
    void renderEnd(int durationMs = 0, int tokenCount = 0);

    /// Toggle collapsed/expanded state and return new state
    bool toggleCollapsed();

    /// Set terminal width for proper formatting
    void setTerminalWidth(int width) { terminalWidth_ = width; }

    /// Set thinking budget for adaptive depth display
    void setThinkingBudget(int maxTokens) { thinkingBudget_ = maxTokens; }

    /// Get whether currently in thinking mode
    bool isInThinking() const { return inThinking_; }

    /// Get whether currently collapsed
    bool isCollapsed() const { return collapsed_; }

private:
    std::ostream& out_;
    int terminalWidth_;
    bool inThinking_ = false;
    bool collapsed_ = true;
    ThinkingDepth currentDepth_ = ThinkingDepth::Normal;
    int thinkingBudget_ = 0;        // Max thinking tokens (0 = unknown)
    int thinkingTokensUsed_ = 0;    // Tokens used so far
    int contentLineCount_ = 0;      // Lines rendered in current block
    bool isStreaming_ = false;
    std::chrono::steady_clock::time_point streamEndTime_;

    /// Get done label with optional duration/tokens
    String doneLabel(int durationMs, int tokenCount) const;

    /// Render header line: "∴ Thinking" + optional suffix (dim+italic)
    void renderHeader(const String& suffix);

    /// Render a dim content line with "  " prefix
    void renderDimLine(const String& line);

    /// Render a collapsed single-line hint ("∴ Thinking")
    void renderCollapsedHint();
};

} // namespace claude
