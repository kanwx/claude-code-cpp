#include <claude/console/ThinkingRenderer.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/utils/I18n.hpp>

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

String ThinkingRenderer::doneLabel(int durationMs, int tokenCount) const {
    auto& i18n = I18n::instance();
    String label = i18n.tr("status.thinking");

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

// ========== Private helpers ==========

void ThinkingRenderer::renderHeader(const String& suffix) {
    // "∴ Thinking…" or "∴ Thinking"
    out_ << AnsiStyle::DIM << AnsiStyle::ITALIC
         << "\xe2\x88\xb4 Thinking" << suffix
         << AnsiStyle::RESET << "\n";
}

void ThinkingRenderer::renderDimLine(const String& line) {
    out_ << AnsiStyle::DIM << "  " << line << AnsiStyle::RESET << "\n";
}

// ========== Public API ==========

void ThinkingRenderer::render(const String& thinking, bool collapsed,
                              const String& summary, ThinkingDepth depth) {
    collapsed_ = collapsed;
    currentDepth_ = depth;
    contentLineCount_ = 0;

    if (collapsed) {
        // Collapsed: single dim line "∴ Thinking"
        out_ << AnsiStyle::DIM << AnsiStyle::ITALIC
             << "\xe2\x88\xb4 Thinking"
             << AnsiStyle::RESET << "\n";
    } else {
        // Expanded: header + content lines + done footer
        renderHeader("\xe2\x80\xa6");  // "…" (U+2026 ellipsis)

        std::istringstream stream(thinking);
        String line;
        while (std::getline(stream, line)) {
            contentLineCount_++;
            renderDimLine(line);
        }

        // Done footer
        int estimatedTokens = static_cast<int>(thinking.size()) / 4;
        String done = doneLabel(0, estimatedTokens);
        out_ << AnsiStyle::DIM << AnsiStyle::ITALIC
             << "\xe2\x88\xb4 " << done
             << AnsiStyle::RESET << "\n";
    }
}

void ThinkingRenderer::renderStart(ThinkingDepth depth) {
    currentDepth_ = depth;
    contentLineCount_ = 0;
    inThinking_ = true;
    isStreaming_ = true;
    streamEndTime_ = std::chrono::steady_clock::now();

    // "∴ Thinking…" header (dim)
    renderHeader("\xe2\x80\xa6");  // "…" (U+2026 ellipsis)
}

void ThinkingRenderer::renderContent(const String& line) {
    if (!inThinking_) return;
    // If visibility is Hidden, skip output entirely
    auto vis = getThinkingVisibility(false, false, isStreaming_, true, streamEndTime_);
    if (vis == ThinkingVisibility::Hidden) return;
    contentLineCount_++;
    renderDimLine(line);
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
        // Produce no output at all
        inThinking_ = false;
        return;
    }

    if (vis == ThinkingVisibility::Collapsed) {
        renderCollapsedHint();
        inThinking_ = false;
        return;
    }

    // Expanded / Streaming-fallback: done footer
    String done = doneLabel(durationMs, tokenCount);
    out_ << AnsiStyle::DIM << AnsiStyle::ITALIC
         << "\xe2\x88\xb4 " << done
         << AnsiStyle::RESET << "\n";

    inThinking_ = false;
}

bool ThinkingRenderer::toggleCollapsed() {
    collapsed_ = !collapsed_;
    return collapsed_;
}

void ThinkingRenderer::renderCollapsedHint() {
    // Single dim+italic line: "∴ Thinking"
    out_ << AnsiStyle::DIM << AnsiStyle::ITALIC
         << "\xe2\x88\xb4 Thinking"
         << AnsiStyle::RESET << "\n";
}

} // namespace claude
