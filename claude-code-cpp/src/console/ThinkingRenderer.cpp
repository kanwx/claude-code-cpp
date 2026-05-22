#include <claude/console/ThinkingRenderer.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/MessageResponse.hpp>
#include <claude/utils/I18n.hpp>
#include <algorithm>

namespace claude {

// ========== ThinkingVisibility logic ==========

ThinkingVisibility getThinkingVisibility(
    bool verboseMode,
    bool transcriptMode,
    bool isStreaming,
    bool isLastInTurn,
    std::chrono::steady_clock::time_point streamEndTime)
{
    if (isStreaming) return ThinkingVisibility::Streaming;
    if (transcriptMode && !isLastInTurn) return ThinkingVisibility::Hidden;
    auto elapsed = std::chrono::steady_clock::now() - streamEndTime;
    bool withinTimeout = elapsed < std::chrono::seconds(30);
    if (transcriptMode && !withinTimeout) return ThinkingVisibility::Hidden;
    if (verboseMode) return ThinkingVisibility::Expanded;
    return ThinkingVisibility::Collapsed;
}

ThinkingRenderer::ThinkingRenderer(std::ostream& out, int terminalWidth)
    : out_(out), terminalWidth_(terminalWidth) {}

String ThinkingRenderer::makePadding(int usedChars) const {
    int remaining = terminalWidth_ - usedChars - 1;
    if (remaining < 1) remaining = 1;
    String dash = "\xe2\x94\x80";  // ─
    String result;
    for (int i = 0; i < remaining; ++i) result += dash;
    return result;
}

const char* ThinkingRenderer::nextRainbowColor() {
    // Cycle through subtle colors for thinking phases
    static constexpr const char* RAINBOW_COLORS[] = {
        "\033[2;35m",  // dim magenta
        "\033[2;34m",  // dim blue
        "\033[2;36m",  // dim cyan
        "\033[2;32m",  // dim green
        "\033[2;33m",  // dim yellow
        "\033[2;31m",  // dim red
    };
    const char* color = RAINBOW_COLORS[rainbowIndex_ % RAINBOW_CYCLE_SIZE];
    rainbowIndex_++;
    return color;
}

const char* ThinkingRenderer::thinkingIcon(ThinkingDepth depth) {
    switch (depth) {
        case ThinkingDepth::Extended:
        case ThinkingDepth::Adaptive:
            return "\xf0\x9f\xa7\xa0";  // 🧠 (U+1F9E0)
        case ThinkingDepth::Normal:
        default:
            return Figures::THINKING_EMOJI;  // 💭
    }
}

String ThinkingRenderer::thinkingLabel(ThinkingDepth depth) const {
    auto& i18n = I18n::instance();
    String label = i18n.tr("status.thinking");
    switch (depth) {
        case ThinkingDepth::Extended:
            return label + " (extended)";
        case ThinkingDepth::Adaptive:
            return label + " (adaptive)";
        case ThinkingDepth::Normal:
        default:
            return label;
    }
}

String ThinkingRenderer::doneLabel(int durationMs, int tokenCount) const {
    auto& i18n = I18n::instance();
    String label = i18n.tr("status.done");

    // Add duration if available
    if (durationMs > 0) {
        if (durationMs >= 60000) {
            int mins = durationMs / 60000;
            int secs = (durationMs % 60000) / 1000;
            label += " (" + std::to_string(mins) + "m " + std::to_string(secs) + "s";
        } else if (durationMs >= 1000) {
            label += " (" + std::to_string(durationMs / 1000) + "s";
        } else {
            label += " (" + std::to_string(durationMs) + "ms";
        }

        // Add token count
        if (tokenCount > 0) {
            label += " · ";
            if (tokenCount >= 10000) {
                label += std::to_string(tokenCount / 1000) + "K";
            } else {
                label += std::to_string(tokenCount);
            }
            label += " tokens";
        }

        // Add budget usage for adaptive depth
        if (thinkingBudget_ > 0 && tokenCount > 0) {
            int pct = (tokenCount * 100) / thinkingBudget_;
            label += " · " + std::to_string(pct) + "% budget";
        }

        label += ")";
    }
    return label;
}

void ThinkingRenderer::renderTopBorder(const String& label, const char* icon) {
    // ┌─ 💭 Thinking ──────────────┐
    // Count visible characters for padding
    int visibleLen = 4 + static_cast<int>(label.size()) + 2; // "┌─ " + icon + " " + label + " "
    // Icon is multi-byte but takes 2 columns
    visibleLen += 2; // emoji width

    out_ << "\n"
         << AnsiStyle::Semantic::THINKING_BORDER
         << "\xe2\x94\x8c" << "\xe2\x94\x80" << " "  // ┌─
         << icon << " " << label << " "
         << makePadding(visibleLen)
         << "\xe2\x94\x90"   // ┐
         << AnsiStyle::RESET << "\n";
}

void ThinkingRenderer::renderBottomBorder(const String& label) {
    // └─ ✓ Done ───────────────────┘
    int visibleLen = 4 + 1 + static_cast<int>(label.size()) + 1; // "└─ " + "✓" + " " + label + " "
    // ✓ is 1 column

    out_ << AnsiStyle::Semantic::THINKING_BORDER
         << "\xe2\x94\x94" << "\xe2\x94\x80" << " "  // └─
         << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK
         << AnsiStyle::Semantic::THINKING_BORDER << " " << label << " "
         << makePadding(visibleLen)
         << "\xe2\x94\x98"   // ┘
         << AnsiStyle::RESET << "\n\n";
}

void ThinkingRenderer::renderContentLine(const String& line) {
    int contentWidth = terminalWidth_ - 4; // "│ " + " │"
    String displayLine = line;
    if (static_cast<int>(displayLine.length()) > contentWidth - 1) {
        displayLine = displayLine.substr(0, contentWidth - 4) + "...";
    }

    out_ << AnsiStyle::Semantic::THINKING_BORDER << "\xe2\x94\x82" << " " << AnsiStyle::RESET;

    // Use rainbow color for this line
    out_ << nextRainbowColor() << displayLine << AnsiStyle::RESET;

    // Pad to right border
    int padLen = contentWidth - static_cast<int>(displayLine.length()) - 1;
    if (padLen > 0) out_ << String(padLen, ' ');
    out_ << AnsiStyle::Semantic::THINKING_BORDER << " " << "\xe2\x94\x82" << AnsiStyle::RESET << "\n";
}

// ========== Public API ==========

void ThinkingRenderer::render(const String& thinking, bool collapsed,
                              const String& summary, ThinkingDepth depth) {
    collapsed_ = collapsed;
    currentDepth_ = depth;
    rainbowIndex_ = 0;
    contentLineCount_ = 0;

    String label = thinkingLabel(depth);
    const char* icon = thinkingIcon(depth);

    renderTopBorder(label, icon);

    if (collapsed) {
        // Collapsed: show single summary line
        String displaySummary = summary;
        if (displaySummary.empty() && !thinking.empty()) {
            size_t nl = thinking.find('\n');
            if (nl != String::npos) {
                displaySummary = thinking.substr(0, std::min(nl, static_cast<size_t>(60)));
            } else {
                displaySummary = thinking.substr(0, std::min(thinking.length(), static_cast<size_t>(60)));
            }
            if (thinking.length() > displaySummary.length()) {
                displaySummary += "...";
            }
        }
        if (!displaySummary.empty()) {
            renderContentLine(displaySummary);
        }
    } else {
        // Expanded: show all content lines with rainbow colors
        std::istringstream stream(thinking);
        String line;
        while (std::getline(stream, line)) {
            contentLineCount_++;
            renderContentLine(line);
        }
    }

    // Estimate token count from content length for done label
    int estimatedTokens = static_cast<int>(thinking.size()) / 4;
    String done = doneLabel(0, estimatedTokens);
    renderBottomBorder(done);
}

void ThinkingRenderer::renderStart(ThinkingDepth depth) {
    currentDepth_ = depth;
    rainbowIndex_ = 0;
    contentLineCount_ = 0;
    inThinking_ = true;
    isStreaming_ = true;
    streamEndTime_ = std::chrono::steady_clock::now();

    String label = thinkingLabel(depth);
    const char* icon = thinkingIcon(depth);

    renderTopBorder(label, icon);
}

void ThinkingRenderer::renderContent(const String& line) {
    if (!inThinking_) return;
    // If visibility is Hidden, skip output entirely
    auto vis = getThinkingVisibility(false, false, isStreaming_, true, streamEndTime_);
    if (vis == ThinkingVisibility::Hidden) return;
    contentLineCount_++;
    renderContentLine(line);
}

void ThinkingRenderer::renderEnd(int durationMs, int tokenCount) {
    if (!inThinking_) return;

    thinkingTokensUsed_ = tokenCount;
    isStreaming_ = false;
    streamEndTime_ = std::chrono::steady_clock::now();

    // Compute visibility — note: renderEnd doesn't have full context,
    // so default to non-verbose, non-transcript. The caller should
    // check visibility before calling renderEnd if those matter.
    auto vis = getThinkingVisibility(false, false, false, true, streamEndTime_);

    if (vis == ThinkingVisibility::Hidden) {
        // Produce no output at all — not even the closing border
        inThinking_ = false;
        return;
    }

    if (vis == ThinkingVisibility::Collapsed) {
        renderCollapsedHint();
        inThinking_ = false;
        return;
    }

    // Expanded / Streaming-fallback: full border rendering
    String done = doneLabel(durationMs, tokenCount);
    renderBottomBorder(done);

    inThinking_ = false;
}

bool ThinkingRenderer::toggleCollapsed() {
    collapsed_ = !collapsed_;
    return collapsed_;
}

void ThinkingRenderer::renderCollapsedHint() {
    // Single dim line: "∴ Thinking" + optional Ctrl+O hint
    out_ << "\033[2m\033[3m\xe2\x88\xb4 Thinking\033[0m\n";
}

} // namespace claude
