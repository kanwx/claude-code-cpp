#include "claude/ui/ContentBlockRenderer.hpp"
#include "claude/ui/ToolResultFormatter.hpp"

#ifdef HAS_FTXUI
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include "claude/ui/FtxuiMarkdown.hpp"
#include "claude/console/CreativeVerbs.hpp"
#include "../FtxuiColors.hpp"
#include <sstream>

namespace claude {

using namespace ftxui;
using namespace ftxui_colors;

// ========== Margin helper ==========
// Unified prefix system matching TS "  ⎿  " / "  ⏺  " pattern.

namespace {

/// Return the prefix for a content block type.
/// - First assistant text → "⏺ "
/// - Response content (tool results, system msgs, subsequent text) → "  ⎿ " or "  "
/// - User messages → no prefix (frame handles it)
String contentMargin(const ContentBlock& block, bool isResponse = true) {
    if (block.type == ContentBlock::UserMessage) return "";
    if (block.type == ContentBlock::AnswerText && block.isFirst) return "● ";
    if (isResponse) return "  ⎿ ";
    return "  ";
}

/// Render content with margin prefix.
Element withMargin(const String& prefix, Element content) {
    if (prefix.empty()) return content;
    return hbox({
        text(prefix) | dim,
        std::move(content),
    });
}

/// Render a badge for a tool name (colored background).
Element renderToolBadge(const String& toolName, bool dimmed = false) {
    auto fg = toolFgColor(toolName);
    auto bg = toolBgColor(toolName);
    auto badge = text(" " + toolName + " ") | bold | color(fg) | bgcolor(bg);
    if (dimmed) badge = badge | dim;
    return badge;
}

/// Render the Ctrl+O hint
Element ctrlOHint() {
    return text(" [Ctrl+O]") | dim | color(MacShadow);
}

} // anonymous namespace

ftxui::Element renderFtxuiElement(const ContentBlock& block) {
    switch (block.type) {
        // ===== User Message =====
        case ContentBlock::UserMessage: {
            // Detect user input type from content
            auto textEl = FtxuiMarkdown::render(block.text);

            // Different border style based on userInputType
            Color borderColor = MacPeach;
            String prefix;
            switch (block.userInputType) {
                case UserInputType::Command:
                    borderColor = MacSky;
                    prefix = "/";
                    break;
                case UserInputType::Bash:
                    borderColor = MacSage;
                    prefix = "! ";
                    break;
                case UserInputType::Plan:
                    borderColor = MacLavender;
                    break;
                case UserInputType::Memory:
                    borderColor = MacGold;
                    break;
                default:
                    break;
            }

            Elements els;
            els.push_back(hbox({
                text(" ╭─") | color(borderColor),
                filler(),
                text("─╮") | color(borderColor),
            }));
            for (auto& el : textEl) {
                els.push_back(hbox({
                    text(" │ ") | color(borderColor),
                    std::move(el) | flex,
                    text(" │") | color(borderColor),
                }));
            }
            els.push_back(hbox({
                text(" ╰─") | color(borderColor),
                filler(),
                text("─╯") | color(borderColor),
            }));
            return vbox(std::move(els));
        }

        // ===== Assistant Text =====
        case ContentBlock::AnswerText: {
            if (block.dimmed) {
                return text(block.text) | dim | color(MacCream);
            }
            auto elements = FtxuiMarkdown::render(block.text);
            if (elements.empty()) return text("");
            Elements result;
            String prefix = block.isFirst ? "● " : "  ";
            bool first = true;
            for (auto& el : elements) {
                if (first) {
                    result.push_back(hbox({
                        text(prefix) | color(MacSky),
                        std::move(el) | flex,
                    }));
                    first = false;
                } else {
                    result.push_back(hbox({
                        text("  "),
                        std::move(el) | flex,
                    }));
                }
            }
            return vbox(std::move(result));
        }

        // ===== Thinking Block =====
        case ContentBlock::ThinkingBlock:
            // Guard: when collapsed, never render raw thinking text.
            // detailText is only shown when user explicitly expands via Ctrl+O.
            if (!block.expanded) {
                return hbox({
                    text("  ∴ ") | color(MacLavender),
                    text("Thinking") | dim | color(MacLavender),
                });
            }
            // Expanded: user explicitly toggled, show detail content
            {
                Elements els;
                els.push_back(hbox({
                    text("  ∴ ") | color(MacLavender),
                    text("Thinking...") | dim | color(MacLavender),
                }));
                auto detail = FtxuiMarkdown::render(block.detailText);
                for (auto& el : detail) {
                    els.push_back(hbox({
                        text("  │ ") | color(MacLavender) | dim,
                        std::move(el) | flex | dim,
                    }));
                }
                els.push_back(hbox({
                    text("  ∴ ") | color(MacLavender),
                    text("(collapsed)") | dim | color(MacLavender),
                }));
                return vbox(std::move(els));
            }

        // ===== Tool Progress =====
        case ContentBlock::ToolProgress: {
            return hbox({
                text("  ⎿ "),
                renderToolBadge(block.toolName),
                text(" "),
                text(block.activity) | dim,
            });
        }

        // ===== Tool Result =====
        case ContentBlock::ToolResult: {
            auto dm = formatToolResult(block);

            if (dm.isError) {
                String errDisplay = dm.errorText.empty() ? "Error" : dm.errorText;
                return hbox({
                    text("  ⎿ "),
                    renderToolBadge(dm.toolName),
                    text(" "),
                    text(errDisplay) | color(MacRose),
                });
            }

            if (dm.isCancelled || dm.isRejected) {
                String label = dm.isRejected ? "Rejected" : "Interrupted";
                return hbox({
                    text("  ⊘ ") | dim,
                    renderToolBadge(dm.toolName, /*dimmed=*/true),
                    text(" "),
                    text(label) | dim,
                });
            }

            // Build per-tool display text
            String displayText = dm.toDisplayText();
            String detailText;
            // FTXUI-specific: show file path on second line for Edit/Write
            if ((dm.toolName == "Edit" || dm.toolName == "Write") && !dm.filePath.empty()) {
                detailText = dm.filePath;
            }

            auto summaryEl = text(displayText) | dim;
            if (!detailText.empty()) {
                summaryEl = vbox({
                    text(displayText) | dim,
                    text("  " + detailText) | dim | color(MacShadow),
                });
            }

            if (!block.expanded) {
                return hbox({
                    text("  ⎿ "),
                    renderToolBadge(dm.toolName),
                    text(" "),
                    summaryEl,
                    ctrlOHint(),
                });
            }
            return hbox({
                text("  ⎿ "),
                renderToolBadge(dm.toolName),
                text(" "),
                summaryEl,
            });
        }

        // ===== Tool Group =====
        case ContentBlock::ToolGroup: {
            if (!block.expanded) {
                return hbox({
                    text("  ⎿ "),
                    text(block.summary.primaryText) | dim,
                    ctrlOHint(),
                });
            }
            Elements childrenEls;
            childrenEls.push_back(hbox({
                text("  ⎿ "),
                text(block.summary.primaryText) | dim,
            }));
            for (auto& child : block.children) {
                childrenEls.push_back(hbox({
                    text("    "),
                    renderFtxuiElement(child),
                }));
            }
            return vbox(std::move(childrenEls));
        }

        // ===== Agent Progress =====
        case ContentBlock::AgentProgress:
            return hbox({
                text("  ⎿ "),
                text("● ") | color(MacLilac),
                text(block.toolName + ": " + block.text) | dim | color(MacLilac),
            });

        // ===== Error Message =====
        case ContentBlock::ErrorMessage:
            return hbox({
                text("  ✕ ") | color(MacRose),
                text(block.text) | color(MacRose),
            });

        // ===== Collapsed Read/Search Group =====
        case ContentBlock::CollapsedGroup: {
            if (!block.expanded) {
                // Determine dot color: green=success, red=error, grey=active
                Color dotColor = MacMint;
                if (block.hasContentAfter) {
                    dotColor = MacShadow;  // Still loading → grey
                } else if (block.summary.isError) {
                    dotColor = MacRose;
                }
                return hbox({
                    text("  "),
                    text("⏺ ") | color(dotColor),
                    text(block.summary.primaryText) | dim,
                    ctrlOHint(),
                });
            }
            // Expanded: header + indented children with tree connectors
            Elements cel;
            cel.push_back(hbox({
                text("  "),
                text("⏺ ") | color(MacMint),
                text(block.summary.primaryText) | dim,
            }));
            for (size_t i = 0; i < block.children.size(); i++) {
                bool last = (i == block.children.size() - 1);
                String connector = last ? "  └─ " : "  ├─ ";
                cel.push_back(hbox({
                    text(connector) | dim | color(MacShadow),
                    renderFtxuiElement(block.children[i]),
                }));
            }
            return vbox(std::move(cel));
        }

        // ===== Compact Boundary =====
        case ContentBlock::CompactBoundary: {
            if (block.systemSubtype == SystemMessageSubtype::MicroCompactBoundary &&
                block.snippedCount > 0) {
                // "─── N messages snipped ───"
                String label = " " + std::to_string(block.snippedCount) +
                               " messages snipped ";
                return hbox({
                    text("  ───") | dim | color(MacShadow),
                    text(label) | dim | color(MacShadow),
                    text("───") | dim | color(MacShadow),
                }) | center;
            }
            // Standard compact boundary: "─── Earlier conversation compacted ───"
            String label = block.text.empty() ? " Earlier conversation compacted " : block.text;
            return hbox({
                text("  ───") | dim | color(MacShadow),
                text(label) | dim | color(MacShadow),
                text("───") | dim | color(MacShadow),
            }) | center;
        }

        // ===== System Message =====
        case ContentBlock::SystemMessage: {
            switch (block.systemSubtype) {
                case SystemMessageSubtype::ApiError:
                    return hbox({
                        text("  ✕ ") | color(MacRose),
                        text(block.text) | color(MacRose),
                    });
                case SystemMessageSubtype::MemorySaved:
                    return hbox({
                        text("  ⎿ "),
                        text("Memory saved") | dim | color(MacCream),
                    });
                case SystemMessageSubtype::StopHookSummary:
                    return hbox({
                        text("  ⎿ "),
                        text(block.text) | dim | color(MacCream),
                    });
                case SystemMessageSubtype::AgentsKilled:
                    return hbox({
                        text("  ⎿ "),
                        text("Agent " + block.text + " completed") | dim | color(MacLilac),
                    });
                case SystemMessageSubtype::BridgeStatus:
                    return hbox({
                        text("  ⎿ "),
                        text(block.text) | dim | color(MacCream),
                    });
                default:
                    return hbox({
                        text("  ⎿ "),
                        text(block.text) | dim | color(MacCream),
                    });
            }
        }

        // ===== Turn Duration =====
        case ContentBlock::TurnDuration: {
            String verb = console::CreativeVerbs::randomCreativeVerb();
            return hbox({
                text("  ● ") | color(MacSky),
                text(verb + "  ") | dim,
                text(block.text) | dim,
            });
        }

        default:
            return text(block.text);
    }
}

} // namespace claude
#endif
