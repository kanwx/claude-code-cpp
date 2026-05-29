#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/ui/FtxuiMarkdown.hpp"
#include "claude/ui/PromptInputFooter.hpp"
#include "claude/repl/Completer.hpp"
#include "claude/core/ReactiveState.hpp"
#include <ftxui/component/mouse.hpp>
#include "FtxuiColors.hpp"
#include "claude/console/ActivityDescription.hpp"
#include "claude/bootstrap/AppState.hpp"
#include <algorithm>

namespace claude {

using namespace ftxui_colors;

// ========== detail namespace — MainComponent extracted from BuildMainComponent ==========

namespace detail {

class MainComponent : public ftxui::ComponentBase {
public:
    explicit MainComponent(FtxuiRepl* owner) : owner_(owner) {
        // Initialize the completer with default commands if empty
        if (owner_->completer_.currentCompletions().empty()) {
            owner_->completer_ = createDefaultCompleter({});
        }
    }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        auto* r = owner_;
        auto orange = MacPeach;

        // --- Header ---
        // Context usage progress bar
        int ctxPct = (r->contextMaxTokens_ > 0)
            ? static_cast<int>(100 * r->contextUsedTokens_ / r->contextMaxTokens_)
            : 0;
        int barWidth = 10;
        int filled = std::clamp((ctxPct * barWidth + 50) / 100, 0, barWidth);

        auto barColor = (ctxPct >= 85) ? MacContextCrit
                      : (ctxPct >= 70) ? MacContextWarn
                      :                  MacContextOk;

        String barStr;
        for (int i = 0; i < filled; ++i) barStr += "█";
        for (int i = filled; i < barWidth; ++i) barStr += "░";
        String pctStr = std::to_string(ctxPct) + "% ctx";

        // Token count / cost formatters
        auto fmtTokens = [](int n) -> String {
            if (n >= 1'000'000) return std::to_string(n / 100'000) + "." + std::to_string((n % 100'000) / 10) + "M";
            if (n >= 1'000) return std::to_string(n / 100) + "." + std::to_string((n % 100) / 10) + "K";
            return std::to_string(n);
        };

        auto fmtCost = [](double cost) -> String {
            if (cost < 0.0001) return "$0.0000";
            char buf[16];
            snprintf(buf, sizeof(buf), "$%.4f", cost);
            return String(buf);
        };

        auto header = hbox({
            text(" ╭─") | color(MacPeach),
            text(" Claude Code C++ ") | bold | color(MacPeach),
            text("│ ") | color(MacShadow),
            text(r->modelInfo_) | dim | color(MacCream),
            text(" │ ") | color(MacShadow),
            text(barStr) | color(barColor),
            text(" " + pctStr) | color(barColor) | dim,
            text(" │ ") | color(MacShadow),
            text(fmtTokens(r->inputTokens_) + " in/" + fmtTokens(r->outputTokens_) + " out") | color(MacCream) | dim,
            text(" · ") | color(MacShadow),
            text(fmtCost(r->costUsd_)) | color(MacCream) | dim,
            filler(),
            text(r->isStreaming_ ? "● Running" : "○ Idle")
                | color(r->isStreaming_ ? MacMint : MacShadow),
            text(" ─╮") | color(MacPeach),
        });

        // --- Messages (virtual scrolling: only render visible window) ---
        std::vector<Element> elems;

        // Calculate visible window using VirtualScroll
        size_t totalMessages = r->messages_.size();
        constexpr size_t maxRender = 50;  // Max messages per frame

        // Auto-scroll: always show latest messages
        size_t startIdx = 0;
        if (totalMessages > maxRender) {
            if (!r->virtualScroll_.isPinnedToBottom() && r->virtualScroll_.firstVisibleIndex() + maxRender < totalMessages) {
                // User scrolled up: respect their position
                startIdx = r->virtualScroll_.firstVisibleIndex();
            } else {
                // Auto-scroll: show last maxRender messages
                startIdx = totalMessages - maxRender;
                r->virtualScroll_.setPinToBottom(true);
                r->virtualScroll_.setPinToBottom(true);
            }
        }

        // Show "N messages above" indicator
        if (startIdx > 0) {
            elems.push_back(hbox({
                text("  ⬆ " + std::to_string(startIdx) + " messages above (scroll up to see)")
                    | dim | color(MacShadow),
            }));
            elems.push_back(text(""));
        }

        // Group consecutive non-User messages into visual blocks
        // to match TS's "assistant turn" style where tool calls appear inline
        for (size_t i = startIdx; i < totalMessages; ) {
            const auto& msg = r->messages_[i];

            if (msg.type == DisplayMessage::Type::UserPrompt) {
                elems.push_back(text(""));
                elems.push_back(hbox({
                    text("❯ ") | color(MacSage) | bold,
                    paragraph(msg.text) | bold | flex,
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::SystemInfo) {
                elems.push_back(hbox({
                    text("※ ") | color(MacSky),
                    text(msg.text) | dim | color(MacCream),
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::SystemError) {
                elems.push_back(hbox({
                    text("✗ ") | color(MacRose) | bold,
                    text(msg.text) | color(MacRose),
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::TurnDuration) {
                elems.push_back(hbox({
                    text("  ") | dim,
                    text(msg.text) | dim,
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::CompactBoundary) {
                elems.push_back(hbox({
                    text("  ── context compacted ──") | dim | color(MacShadow),
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::HookSummary) {
                elems.push_back(hbox({
                    text("⚙ ") | color(MacShadow),
                    text(msg.text) | dim | color(MacCream),
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::AgentProgress) {
                auto agentType = msg.toolUse.toolName.empty() ? String("Agent") : msg.toolUse.toolName;
                auto desc = msg.text.empty() ? String("Working...") : msg.text;
                auto running = msg.expanded;  // makeAgentProgress stores running state in expanded
                auto tokens = msg.toolResult.result;  // "N tokens"

                auto badgeBg = toolBgColor("Agent");
                auto badgeFg = toolFgColor("Agent");

                elems.push_back(vbox({
                    hbox({
                        text("├── ") | color(MacShadow),
                        text(" " + agentType + " ") | bold | bgcolor(badgeBg) | color(badgeFg),
                        text(" " + desc) | color(MacCream),
                        text(" · ") | color(MacShadow),
                        text(tokens) | dim | color(MacCream),
                    }),
                    hbox({
                        text("│  ") | color(MacShadow),
                        text(running ? "⏻  Working..." : "Done") | color(running ? MacGold : MacMint),
                    }),
                }));
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::CollapsedReadSearch) {
                if (msg.expanded) {
                    // Show individual tools
                    for (size_t idx : msg.collapsedGroup.toolIndices) {
                        if (idx >= totalMessages) continue;
                        const auto& tmsg = r->messages_[idx];
                        if (tmsg.type != DisplayMessage::Type::AssistantToolUse) continue;
                        String activity = getActivityDescription(tmsg.toolUse.toolName, tmsg.toolUse.input, true);
                        if (activity.size() > 80) activity = activity.substr(0, 77) + "...";
                        elems.push_back(hbox({
                            text("  ⎿ "),
                            text(" " + tmsg.toolUse.toolName + " ") | bold | color(toolFgColor(tmsg.toolUse.toolName)) | bgcolor(toolBgColor(tmsg.toolUse.toolName)),
                            text(activity) | dim | color(MacCream),
                        }));
                    }
                } else {
                    String summary = msg.collapsedGroup.summaryText();
                    elems.push_back(hbox({
                        text("  ⎿ ") | color(MacSky),
                        text(summary) | color(MacSky),
                        text(" (ctrl+o to expand)") | dim | color(MacShadow),
                    }));
                }
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::PermissionPrompt) {
                // Permission prompts are rendered separately below
                i++;
                continue;
            }

            if (msg.type == DisplayMessage::Type::AssistantThinking) {
                if (msg.expanded) {
                    elems.push_back(hbox({
                        text("╭─ 💭 Thinking ─") | color(MacLavender) | dim,
                        filler() | color(MacLavender) | dim,
                        text("╮") | color(MacLavender) | dim,
                    }));
                    std::istringstream thinkStream(msg.thinking.text);
                    String thinkLine;
                    while (std::getline(thinkStream, thinkLine)) {
                        elems.push_back(hbox({
                            text("│ ") | color(MacLavender) | dim,
                            text(thinkLine) | dim | color(MacCream),
                            filler(),
                            text(" │") | color(MacLavender) | dim,
                        }));
                    }
                    elems.push_back(hbox({
                        text("╰─ ✓ Done ─") | color(MacMint),
                        filler() | color(MacLavender) | dim,
                        text("╯") | color(MacLavender) | dim,
                    }));
                } else {
                    String summary = msg.thinking.text;
                    if (summary.size() > 80) summary = summary.substr(0, 77) + "...";
                    elems.push_back(hbox({
                        text("╭─ 💭 ") | color(MacLavender) | dim,
                        text("Thinking") | color(MacLavender),
                        text(" ─ ") | color(MacLavender) | dim,
                        text(summary) | dim | color(MacCream),
                        text(" (ctrl+o)") | dim | color(MacShadow),
                        filler() | color(MacLavender) | dim,
                        text("╮") | color(MacLavender) | dim,
                    }));
                }
                i++;
                continue;
            }

            // Collect a block of Assistant + Tool messages (one "assistant turn")
            std::vector<Element> blockElems;
            bool blockHasAssistantText = false;

            while (i < totalMessages) {
                const auto& bmsg = r->messages_[i];

                if (bmsg.type == DisplayMessage::Type::AssistantText) {
                    if (!bmsg.text.empty()) {
                        blockHasAssistantText = true;
                        auto mdBlocks = FtxuiMarkdown::render(bmsg.text);
                        for (auto& mdElem : mdBlocks) {
                            blockElems.push_back(std::move(mdElem));
                        }
                    }
                    i++;
                } else if (bmsg.type == DisplayMessage::Type::AssistantToolUse) {
                    // Check if this is a collapsible read/search tool
                    if (isReadSearchTool(bmsg.toolUse.toolName)) {
                        // Accumulate consecutive collapsible tools
                        CollapsedToolGroup group;
                        group.active = true;

                        while (i < totalMessages &&
                               r->messages_[i].type == DisplayMessage::Type::AssistantToolUse &&
                               isReadSearchTool(r->messages_[i].toolUse.toolName)) {
                            const auto& tmsg = r->messages_[i];
                            group.toolIndices.push_back(i);

                            // Check if next message is a result for this tool
                            bool hasResult = false;
                            if (i + 1 < totalMessages &&
                                r->messages_[i + 1].type == DisplayMessage::Type::UserToolResult &&
                                r->messages_[i + 1].toolResult.toolUseId == tmsg.toolUse.toolId) {
                                hasResult = true;
                            }

                            if (tmsg.toolUse.toolName == "Read" || tmsg.toolUse.toolName == "FileReadTool") {
                                group.readCount++;
                                if (!tmsg.toolUse.input.empty()) {
                                    size_t pathStart = tmsg.toolUse.input.find("path");
                                    if (pathStart == String::npos) pathStart = tmsg.toolUse.input.find("file_path");
                                    if (pathStart != String::npos) {
                                        size_t q1 = tmsg.toolUse.input.find('"', pathStart);
                                        if (q1 != String::npos) {
                                            size_t q2 = tmsg.toolUse.input.find('"', q1 + 1);
                                            if (q2 != String::npos) {
                                                String path = tmsg.toolUse.input.substr(q1 + 1, q2 - q1 - 1);
                                                group.readFilePaths.push_back(path);
                                                group.latestHint = path;
                                            }
                                        }
                                    }
                                    if (group.readFilePaths.empty()) {
                                        String summary = FtxuiRepl::truncate(tmsg.toolUse.input, 60);
                                        group.readFilePaths.push_back(summary);
                                        group.latestHint = summary;
                                    }
                                }
                            } else if (tmsg.toolUse.toolName == "Grep" || tmsg.toolUse.toolName == "GrepTool") {
                                group.searchCount++;
                                if (!tmsg.toolUse.input.empty()) {
                                    size_t pStart = tmsg.toolUse.input.find("pattern");
                                    if (pStart == String::npos) pStart = tmsg.toolUse.input.find("query");
                                    if (pStart != String::npos) {
                                        size_t q1 = tmsg.toolUse.input.find('"', pStart);
                                        if (q1 != String::npos) {
                                            size_t q2 = tmsg.toolUse.input.find('"', q1 + 1);
                                            if (q2 != String::npos) {
                                                String pattern = tmsg.toolUse.input.substr(q1 + 1, q2 - q1 - 1);
                                                group.searchPatterns.push_back(pattern);
                                                group.latestHint = pattern;
                                            }
                                        }
                                    }
                                    if (group.searchPatterns.empty()) {
                                        group.latestHint = FtxuiRepl::truncate(tmsg.toolUse.input, 60);
                                    }
                                }
                            } else if (tmsg.toolUse.toolName == "Glob" || tmsg.toolUse.toolName == "GlobTool") {
                                group.searchCount++;
                                if (!tmsg.toolUse.input.empty()) {
                                    size_t pStart = tmsg.toolUse.input.find("pattern");
                                    if (pStart == String::npos) pStart = tmsg.toolUse.input.find("glob");
                                    if (pStart != String::npos) {
                                        size_t q1 = tmsg.toolUse.input.find('"', pStart);
                                        if (q1 != String::npos) {
                                            size_t q2 = tmsg.toolUse.input.find('"', q1 + 1);
                                            if (q2 != String::npos) {
                                                String pattern = tmsg.toolUse.input.substr(q1 + 1, q2 - q1 - 1);
                                                group.searchPatterns.push_back(pattern);
                                                group.latestHint = pattern;
                                            }
                                        }
                                    }
                                    if (group.searchPatterns.empty()) {
                                        group.latestHint = FtxuiRepl::truncate(tmsg.toolUse.input, 60);
                                    }
                                }
                            } else if (tmsg.toolUse.toolName == "Bash" || tmsg.toolUse.toolName == "BashTool") {
                                group.bashCount++;
                                if (!tmsg.toolUse.input.empty()) {
                                    // Extract command from input JSON
                                    size_t cmdStart = tmsg.toolUse.input.find("command");
                                    if (cmdStart != String::npos) {
                                        size_t q1 = tmsg.toolUse.input.find('"', cmdStart);
                                        if (q1 != String::npos) {
                                            size_t q2 = tmsg.toolUse.input.find('"', q1 + 1);
                                            if (q2 != String::npos) {
                                                String cmd = tmsg.toolUse.input.substr(q1 + 1, q2 - q1 - 1);
                                                group.latestHint = cmd;
                                            }
                                        }
                                    }
                                    if (group.latestHint.empty()) {
                                        group.latestHint = FtxuiRepl::truncate(tmsg.toolUse.input, 60);
                                    }
                                }
                            }

                            if (hasResult) group.active = false;

                            i++;
                            // Skip the paired result message if present
                            if (hasResult) i++;
                        }

                        // Render collapsed or expanded
                        if (r->verboseTools_) {
                            for (size_t idx : group.toolIndices) {
                                const auto& tmsg = r->messages_[idx];
                                String activity = getActivityDescription(tmsg.toolUse.toolName, tmsg.toolUse.input, true);
                                if (activity.size() > 80) activity = activity.substr(0, 77) + "...";
                                blockElems.push_back(hbox({
                                    text("  ⎿ "),
                                    text(" " + tmsg.toolUse.toolName + " ") | bold | color(toolFgColor(tmsg.toolUse.toolName)) | bgcolor(toolBgColor(tmsg.toolUse.toolName)),
                                    text(activity) | dim | color(MacCream),
                                }));
                                // Check for paired result
                                if (idx + 1 < totalMessages &&
                                    r->messages_[idx + 1].type == DisplayMessage::Type::UserToolResult) {
                                    const auto& result = r->messages_[idx + 1].toolResult.result;
                                    if (!result.empty()) {
                                        String resultSummary = FtxuiRepl::truncate(result, 300);
                                        blockElems.push_back(hbox({
                                            text("    ⎿ ") | color(MacShadow),
                                            paragraph(resultSummary) | dim | color(MacCream),
                                        }));
                                    }
                                }
                            }
                        } else {
                            String summary = group.summaryText();

                            blockElems.push_back(hbox({
                                text("  ⎿ ") | color(MacSky),
                                text(summary) | color(MacSky),
                                text(group.active ? "" : " ") | dim,
                                text("(ctrl+o to expand)") | dim | color(MacShadow),
                            }));

                            if (!group.latestHint.empty()) {
                                String hint = group.latestHint;
                                if (hint.size() > 80) hint = hint.substr(0, 77) + "...";
                                blockElems.push_back(hbox({
                                    text("    ⎿ ") | color(MacShadow),
                                    text(hint) | dim | color(MacCream),
                                }));
                            }
                        }
                    } else {
                        // Non-collapsible tool (Edit, Write, LSP, WebFetch, etc.)
                        // Show tool name + human-readable activity. Result shown only in verbose mode.
                        String activity = getActivityDescription(bmsg.toolUse.toolName, bmsg.toolUse.input, true);
                        if (activity.size() > 60) activity = activity.substr(0, 57) + "...";
                        blockElems.push_back(hbox({
                            text("  ⎿ "),
                            text(" " + bmsg.toolUse.toolName + " ") | bold | color(toolFgColor(bmsg.toolUse.toolName)) | bgcolor(toolBgColor(bmsg.toolUse.toolName)),
                            activity.empty() ? text("") : text(activity) | dim | color(MacCream),
                        }));

                        // Check for paired result message next
                        if (i + 1 < totalMessages &&
                            r->messages_[i + 1].type == DisplayMessage::Type::UserToolResult &&
                            r->messages_[i + 1].toolResult.toolUseId == bmsg.toolUse.toolId) {
                            const auto& result = r->messages_[i + 1].toolResult.result;
                            if (!result.empty()) {
                                if (r->verboseTools_) {
                                    // Verbose mode: show full result with diff highlighting
                                    bool hasDiff = result.find("\n-") != String::npos ||
                                                   result.find("\n+") != String::npos ||
                                                   result.find("@@") != String::npos;
                                    if (hasDiff) {
                                        std::istringstream diffStream(result);
                                        String diffLine;
                                        while (std::getline(diffStream, diffLine)) {
                                            if (diffLine.starts_with("---") || diffLine.starts_with("+++")) {
                                                blockElems.push_back(text("    " + diffLine) | bold | color(MacSky));
                                            } else if (diffLine.starts_with("@@")) {
                                                blockElems.push_back(text("    " + diffLine) | color(MacSky));
                                            } else if (diffLine.starts_with("-")) {
                                                blockElems.push_back(text("    " + diffLine) | color(MacRose));
                                            } else if (diffLine.starts_with("+")) {
                                                blockElems.push_back(text("    " + diffLine) | color(MacMint));
                                            } else {
                                                blockElems.push_back(text("    " + diffLine) | dim | color(MacCream));
                                            }
                                        }
                                    } else {
                                        String resultSummary = FtxuiRepl::truncate(result, 500);
                                        blockElems.push_back(hbox({
                                            text("    ⎿ ") | color(MacShadow),
                                            paragraph(resultSummary) | dim | color(MacCream),
                                        }));
                                    }
                                } else {
                                    // Compact mode: show one-line summary only
                                    String resultSummary = FtxuiRepl::truncate(result, 80);
                                    blockElems.push_back(hbox({
                                        text("    ⎿ ") | color(MacShadow),
                                        text(resultSummary) | dim | color(MacCream),
                                        text(" (ctrl+o)") | dim | color(MacShadow),
                                    }));
                                }
                            }
                            i++; // skip the result message
                        }
                        i++;
                    }
                } else if (bmsg.type == DisplayMessage::Type::UserToolResult) {
                    // Orphan tool result (no paired tool_use visible) — just show summary
                    if (!bmsg.toolResult.result.empty()) {
                        String resultSummary = FtxuiRepl::truncate(bmsg.toolResult.result, 300);
                        blockElems.push_back(hbox({
                            text("    ⎿ ") | color(MacShadow),
                            paragraph(resultSummary) | dim | color(MacCream),
                        }));
                    }
                    i++;
                } else if (bmsg.type == DisplayMessage::Type::AssistantThinking) {
                    // Thinking within a block — bordered box design
                    if (bmsg.expanded) {
                        blockElems.push_back(hbox({
                            text("╭─ 💭 Thinking ─") | color(MacLavender) | dim,
                            filler() | color(MacLavender) | dim,
                            text("╮") | color(MacLavender) | dim,
                        }));
                        std::istringstream thinkStream(bmsg.thinking.text);
                        String thinkLine;
                        while (std::getline(thinkStream, thinkLine)) {
                            blockElems.push_back(hbox({
                                text("│ ") | color(MacLavender) | dim,
                                text(thinkLine) | dim | color(MacCream),
                                filler(),
                                text(" │") | color(MacLavender) | dim,
                            }));
                        }
                        blockElems.push_back(hbox({
                            text("╰─ ✓ Done ─") | color(MacMint),
                            filler() | color(MacLavender) | dim,
                            text("╯") | color(MacLavender) | dim,
                        }));
                    } else {
                        String summary = bmsg.thinking.text;
                        if (summary.size() > 80) summary = summary.substr(0, 77) + "...";
                        blockElems.push_back(hbox({
                            text("╭─ 💭 ") | color(MacLavender) | dim,
                            text("Thinking") | color(MacLavender),
                            text(" ─ ") | color(MacLavender) | dim,
                            text(summary) | dim | color(MacCream),
                            text(" (ctrl+o)") | dim | color(MacShadow),
                            filler() | color(MacLavender) | dim,
                            text("╮") | color(MacLavender) | dim,
                        }));
                    }
                    i++;
                } else {
                    // Different type — end the block
                    break;
                }
            }

            // Render the block with assistant marker
            if (!blockElems.empty()) {
                elems.push_back(text(""));
                if (blockHasAssistantText) {
                    auto firstElem = std::move(blockElems[0]);
                    blockElems[0] = hbox({
                        text("● ") | color(MacSky),
                        firstElem | flex,
                    });
                }
                for (auto& e : blockElems) {
                    elems.push_back(std::move(e));
                }
            }
        }

        // Streaming text — progressive markdown rendering
        // Use StreamingRenderer to render completed paragraphs as markdown
        // while showing the incomplete tail as plain text with a cursor.
        if (r->isStreaming_ && !r->streamingText_.empty()) {
            if (!elems.empty()) elems.push_back(text(""));
            auto streamElems = r->streamingRenderer_.render();
            bool firstElem = true;
            for (auto& elem : streamElems) {
                if (firstElem) {
                    // Prefix the first element with the assistant marker
                    // Use flex on content so paragraph() knows available width and wraps
                    elems.push_back(hbox({
                        text("● ") | color(MacSky),
                        std::move(elem) | flex,
                    }));
                    firstElem = false;
                } else {
                    elems.push_back(std::move(elem));
                }
            }
            // Blinking cursor at end with glimmer
            bool streamStalled = false;
            if (r->lastOutputTime_.time_since_epoch().count() > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - r->lastOutputTime_).count();
                streamStalled = (elapsed >= 3);
            }
            Color cursorColor = streamStalled ? MacRose : MacPeach;
            bool glimmerPhase = (tickCounter_ % 20) < 10;
            elems.push_back(hbox({
                text(glimmerPhase ? "  ◉" : "  ○") | color(cursorColor) | blink,
            }));
        }

        // Thinking state — show spinner + thinking summary
        if (r->isThinking_ && !r->isStreaming_) {
            if (!elems.empty()) elems.push_back(text(""));
            // Stall detection: red text if no output for 3+ seconds
            bool stalled = false;
            if (r->lastOutputTime_.time_since_epoch().count() > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - r->lastOutputTime_).count();
                stalled = (elapsed >= 3);
            }
            Color thinkColor = stalled ? MacRose : MacLavender;
            // Glimmer effect: pulsing brightness on spinner
            bool glimmerPhase = (tickCounter_ % 20) < 10;  // 0.5s cycle at 20fps
            std::vector<Element> thinkingElems;
            thinkingElems.push_back(spinner(1, tickCounter_) | color(thinkColor));
            thinkingElems.push_back(text(stalled ? " Thinking (stalled)" : " Thinking") | bold | color(thinkColor));
            if (glimmerPhase) {
                thinkingElems.push_back(text(" ●") | color(thinkColor) | dim);
            }
            if (!r->thinkingSummary_.empty()) {
                thinkingElems.push_back(text("  ") | dim);
                String summary = r->thinkingSummary_;
                if (summary.size() > 60) {
                    summary = "..." + summary.substr(summary.size() - 57);
                }
                thinkingElems.push_back(text(summary) | dim | color(MacCream));
            }
            thinkingElems.push_back(text(" (ctrl+o)") | dim | color(MacShadow));
            elems.push_back(hbox(std::move(thinkingElems)));
        }

        // Permission prompt is rendered as an overlay (see below), not inline

        // Show empty content area until first message arrives — no welcome screen
        if (elems.empty() && !r->isStreaming_ && !r->isThinking_) {
            // Intentionally blank — match official Claude Code
        }

        // --- Scroll ---
        // FTXUI yframe auto-scrolls to show focused position.
        // We use focusPositionRelative with a Y coordinate to control scroll.
        // During streaming or auto-scroll: position at bottom (1.0f)
        // When user scrolled up: use their scroll position

        float scrollY = 1.0f; // Default: bottom
        if (!r->virtualScroll_.isPinnedToBottom()) {
            scrollY = scrollRatio_;
        }
        // During streaming/thinking: always pin to bottom
        if (r->isStreaming_ || r->isThinking_) {
            scrollY = 1.0f;
            autoScroll_ = true;
            r->virtualScroll_.setPinToBottom(true);
        }

        // Scroll-up indicator: show how many messages are above
        Element scrollIndicator = text("");
        if (!r->virtualScroll_.isPinnedToBottom() && r->virtualScroll_.firstVisibleIndex() > 0) {
            scrollIndicator = hbox({
                text("  ↑ " + std::to_string(r->virtualScroll_.firstVisibleIndex()) + " messages above") | dim | color(MacShadow) | bold,
                filler(),
            });
        }

        auto messagesContent = vbox(std::move(elems))
            | focusPositionRelative(0, scrollY)
            | yframe
            | flex;

        // Compose messages area with optional scroll indicator
        Element messagesArea;
        if (!r->virtualScroll_.isPinnedToBottom() && r->virtualScroll_.firstVisibleIndex() > 0) {
            messagesArea = vbox({
                scrollIndicator,
                messagesContent,
            });
        } else {
            messagesArea = std::move(messagesContent);
        }

        // --- Permission prompt overlay ---
        // Render as an overlay on the content area (not inline in messages),
        // matching the official Claude Code's overlay slot in FullscreenLayout.
        // Uses FTXUI's dbox() to stack the prompt on top of messages.
        Element permOverlay = emptyElement();
        if (r->permissionPromptActive_) {
            // Tool badge
            auto badgeBg = toolBgColor(r->permissionToolName_);
            auto badgeFg = toolFgColor(r->permissionToolName_);

            std::vector<Element> permElems;

            permElems.push_back(hbox({
                text("╭─ ⚠ Permission Required ─") | color(MacGold),
                filler() | color(MacGold),
                text("╮") | color(MacGold),
            }));

            permElems.push_back(hbox({
                text("│ ") | color(MacGold),
                text(" " + r->permissionToolName_ + " ") | bold | bgcolor(badgeBg) | color(badgeFg),
                filler(),
                text(" │") | color(MacGold),
            }));

            // Description (tool-specific)
            if (!r->permissionDescription_.empty()) {
                String descSummary = r->permissionDescription_;
                if (descSummary.size() > 80) descSummary = descSummary.substr(0, 77) + "...";
                permElems.push_back(hbox({
                    text("│ ") | color(MacGold),
                    text(descSummary) | dim | color(MacCream),
                    filler(),
                    text(" │") | color(MacGold),
                }));
            }

            // Activity (fallback, shown if description is empty)
            if (r->permissionDescription_.empty() && !r->permissionActivity_.empty()) {
                String actSummary = r->permissionActivity_;
                if (actSummary.size() > 80) actSummary = actSummary.substr(0, 77) + "...";
                permElems.push_back(hbox({
                    text("│ ") | color(MacGold),
                    text(actSummary) | dim | color(MacCream),
                    filler(),
                    text(" │") | color(MacGold),
                }));
            }

            permElems.push_back(hbox({
                text("│") | color(MacGold),
                filler(),
                text("│") | color(MacGold),
            }));

            const char* optionLabels[] = {
                "Yes (once)",
                "Allow for this session",
                "Yes, always allow",
                "No (once)",
                "No, always deny"
            };
            const Color optionColors[] = {
                MacMint,    // AllowOnce
                MacSage,    // AllowSession
                MacSage,    // AlwaysAllow
                MacRose,    // DenyOnce
                MacRose,    // AlwaysDeny
            };

            for (int i = 0; i < 5; i++) {
                bool focused = (i == r->permissionFocusedIndex_);
                permElems.push_back(hbox({
                    text("│ ") | color(MacGold),
                    text(focused ? "❯ " : "  "),
                    text(optionLabels[i])
                        | (focused ? bold : dim)
                        | color(focused ? optionColors[i] : MacCream),
                    filler(),
                    text(" │") | color(MacGold),
                }));
            }
            permElems.push_back(hbox({
                text("│ ") | color(MacGold),
                text("↑↓ select · Enter confirm · Esc cancel") | dim | color(MacShadow),
                filler(),
                text(" │") | color(MacGold),
            }));
            permElems.push_back(hbox({
                text("╰") | color(MacGold),
                filler() | color(MacGold),
                text("╯") | color(MacGold),
            }));

            // Use clear_under so the prompt doesn't show transparency artifacts
            // over the messages behind it
            permOverlay = clear_under(vbox(std::move(permElems)));
        }

        // Compose content area: overlay permission prompt on messages using dbox
        // dbox stacks elements — first is the base, subsequent are overlaid on top.
        // filler() pushes the overlay to the bottom of the content area,
        // hcenter centers it horizontally, and size constrains width to 70.
        Element contentArea;
        if (r->permissionPromptActive_) {
            contentArea = dbox({
                messagesArea | flex,
                vbox({ filler(), permOverlay | hcenter | size(WIDTH, LESS_THAN, 70) }),
            }) | flex;
        } else {
            contentArea = std::move(messagesArea);
        }

        // --- Status bar ---
        // Always visible: shows model + context% + cost at idle,
        // spinner + elapsed + tokens during streaming
        Element statusBar;
        if (r->isStreaming_ || r->isThinking_) {
            int elapsed = 0;
            if (r->startTime_.time_since_epoch().count() > 0) {
                elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - r->startTime_).count();
            }
            String ts = FtxuiRepl::formatElapsed(elapsed);
            String label = r->isThinking_ ? "Thinking" : "Running";
            int tokens = static_cast<int>(r->streamingText_.size() / 4);

            // Context percentage
            String contextPct;
            if (r->contextMaxTokens_ > 0) {
                int pct = static_cast<int>(r->contextUsedTokens_ * 100 / r->contextMaxTokens_);
                contextPct = " · " + std::to_string(pct) + "%";
            }

            statusBar = hbox({
                spinner(1, tickCounter_) | color(MacPeach),
                text(" " + label) | bold | color(MacPeach),
                text("  ") | dim,
                text(ts) | color(MacGold),
                text(" · ") | dim | color(MacShadow),
                text(std::to_string(tokens) + " tok") | color(MacCream),
                text(contextPct) | dim | color(MacCream),
                filler(),
            });
            tickCounter_++;
        } else {
            // Idle: show model + context + cost
            // Use reactive accessors when AppState is linked, fall back to local fields
            std::vector<Element> idleParts;

            // Model: prefer reactive accessor, fall back to local modelInfo_
            String displayModel;
            if (r->appState_) {
                displayModel = reactive::accessors::currentModel(*r->appState_);
            } else {
                displayModel = r->modelInfo_;
            }
            if (!displayModel.empty()) {
                idleParts.push_back(text(displayModel) | color(MacCream));
            }

            if (r->contextMaxTokens_ > 0) {
                int pct = static_cast<int>(r->contextUsedTokens_ * 100 / r->contextMaxTokens_);
                Color pctColor = pct >= 85 ? MacRose : pct >= 70 ? MacGold : MacCream;
                if (!idleParts.empty()) idleParts.push_back(text(" · ") | dim | color(MacShadow));
                idleParts.push_back(text(std::to_string(pct) + "% ctx") | color(pctColor));
            }

            // Cost: prefer reactive accessor from AppState, fall back to local costUsd_
            double displayCost = r->costUsd_;
            if (r->appState_) {
                displayCost = r->appState_->totalCostUSD();
            }
            if (displayCost > 0.0) {
                char costBuf[32];
                snprintf(costBuf, sizeof(costBuf), "$%.4f", displayCost);
                if (!idleParts.empty()) idleParts.push_back(text(" · ") | dim | color(MacShadow));
                idleParts.push_back(text(costBuf) | color(MacCream));
            }
            if (!idleParts.empty()) {
                idleParts.push_back(filler());
                statusBar = hbox(std::move(idleParts));
            } else {
                statusBar = hbox({ filler() });
            }
        }

        // --- Input line ---
        // Match TS original: inverted character at cursor position (reverse video).
        // At end of text: inverted space (solid block). On a character: that char inverted.
        Element inputLine;
        if (r->isStreaming_) {
            inputLine = hbox({
                text("❯ ") | color(MacSage) | bold,
            });
        } else {
            // Clamp cursor position to a valid UTF-8 character boundary
            if (cursorPos_ > input_.size()) cursorPos_ = input_.size();
            // Ensure cursorPos_ is at a UTF-8 lead byte, not mid-character
            while (cursorPos_ > 0 && cursorPos_ < input_.size()) {
                auto c = static_cast<unsigned char>(input_[cursorPos_]);
                if ((c & 0xC0) == 0x80) cursorPos_--;
                else break;
            }
            String beforeCursor = input_.substr(0, cursorPos_);
            // Get the full UTF-8 character at cursor position
            String cursorChar = " ";
            size_t nextCharPos = cursorPos_;
            if (cursorPos_ < input_.size()) {
                // Find the end of the current UTF-8 character
                nextCharPos = cursorPos_ + 1;
                while (nextCharPos < input_.size()) {
                    auto c = static_cast<unsigned char>(input_[nextCharPos]);
                    if ((c & 0xC0) != 0x80) break;
                    nextCharPos++;
                }
                cursorChar = input_.substr(cursorPos_, nextCharPos - cursorPos_);
            }
            String afterCursor = (nextCharPos < input_.size())
                ? input_.substr(nextCharPos) : "";
            inputLine = hbox({
                text("❯ ") | color(MacSage) | bold,
                text(beforeCursor),
                text(cursorChar) | inverted,
                text(afterCursor),
            });
        }

        // --- Completion suggestions (above input line) ---
        Element completionArea = text("");
        if (!r->isStreaming_ && !r->completer_.currentCompletions().empty()) {
            const auto& completions = r->completer_.currentCompletions();
            std::vector<Element> compElems;
            compElems.push_back(hbox({
                text("  ╭─ completions ─") | color(MacSky) | dim,
                filler() | color(MacSky) | dim,
                text("╮") | color(MacSky) | dim,
            }));

            size_t displayCount = std::min(completions.size(), size_t(8));
            for (size_t ci = 0; ci < displayCount; ++ci) {
                bool selected = (ci == completionIndex_);
                String compText = completions[ci];
                if (compText.size() > 60) compText = compText.substr(0, 57) + "...";
                compElems.push_back(hbox({
                    text("  │ ") | color(MacSky) | dim,
                    text(selected ? "❯ " : "  "),
                    text(compText)
                        | (selected ? bold : dim)
                        | color(selected ? MacPeach : MacCream),
                    filler(),
                    text(" │") | color(MacSky) | dim,
                }));
            }
            if (completions.size() > displayCount) {
                compElems.push_back(hbox({
                    text("  │ ") | color(MacSky) | dim,
                    text("  ..." + std::to_string(completions.size() - displayCount) + " more")
                        | dim | color(MacShadow),
                    filler(),
                    text(" │") | color(MacSky) | dim,
                }));
            }
            compElems.push_back(hbox({
                text("  ╰") | color(MacSky) | dim,
                text(" Tab accept · ↑↓ cycle · Esc dismiss ") | dim | color(MacShadow),
                filler() | color(MacSky) | dim,
                text("╯") | color(MacSky) | dim,
            }));
            completionArea = vbox(std::move(compElems));
        }

        // Build footer state
        ftxui_footer::FooterState footerState;
        footerState.mode = r->currentMode_;
        footerState.modeHintDismissed = r->modeHintDismissed_;
        footerState.isAuthenticated = r->isAuthenticated_;
        footerState.modelInfo = r->modelInfo_;
        footerState.contextPct = (r->contextMaxTokens_ > 0)
            ? static_cast<int>(100 * r->contextUsedTokens_ / r->contextMaxTokens_) : 0;
        footerState.costUsd = r->costUsd_;
        footerState.isStreaming = r->isStreaming_;
        footerState.cwd = r->cwd_;
        footerState.gitBranch = r->gitBranch_;

        auto footer = ftxui_footer::renderFooter(footerState);

        return vbox({
            header,
            separatorLight(),
            contentArea | flex,
            separatorLight(),
            statusBar,
            completionArea,
            inputLine | (r->isStreaming_ ? dim : bold),
            footer,
        });
    }

    bool OnEvent(ftxui::Event event) override {
        using namespace ftxui;
        auto* r = owner_;

        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            // Left click on input line area: position cursor
            if (mouse.button == Mouse::Left && !r->isStreaming_) {
                // Input line is at the bottom of the screen.
                // mouse.y is relative to the component — check if it's on the input row.
                // The prompt "❯ " takes 2 columns. mouse.x starts from 0.
                // We estimate: if click is on the last rendered row, treat as input click.
                // FTXUI doesn't give us the exact row index easily, so we use a heuristic:
                // any left click while not streaming positions the cursor based on x.
                int clickX = mouse.x - 2;  // Subtract "❯ " prompt width
                if (clickX >= 0 && static_cast<size_t>(clickX) <= static_cast<int>(input_.size())) {
                    cursorPos_ = static_cast<size_t>(clickX);
                } else if (clickX < 0) {
                    cursorPos_ = 0;
                } else {
                    cursorPos_ = input_.size();
                }
                return true;
            }
            if (mouse.button == Mouse::WheelUp) {
                scrollRatio_ = std::max(0.0f, scrollRatio_ - 0.05f);
                autoScroll_ = false;
                r->virtualScroll_.setPinToBottom(false);
                r->virtualScroll_.scrollUp();
                return true;
            }
            if (mouse.button == Mouse::WheelDown) {
                scrollRatio_ = std::min(1.0f, scrollRatio_ + 0.05f);
                if (scrollRatio_ >= 0.95f) {
                    autoScroll_ = true;
                    r->virtualScroll_.setPinToBottom(true);
                } else {
                    autoScroll_ = false;
                    r->virtualScroll_.setPinToBottom(false);
                }
                return true;
            }
            return false;
        }

        if (event == Event::CtrlP || event == Event::F5) {
            scrollRatio_ = std::max(0.0f, scrollRatio_ - 0.05f);
            autoScroll_ = false;
            r->virtualScroll_.setPinToBottom(false);
            if (r->virtualScroll_.firstVisibleIndex() > 0) r->virtualScroll_.scrollUp();
            return true;
        }
        if (event == Event::CtrlN || event == Event::F6) {
            scrollRatio_ = std::min(1.0f, scrollRatio_ + 0.05f);
            if (scrollRatio_ >= 0.95f) {
                autoScroll_ = true;
                r->virtualScroll_.setPinToBottom(true);
            } else {
                autoScroll_ = false;
                r->virtualScroll_.setPinToBottom(false);
            }
            return true;
        }
        if (event == Event::F7 || event == Event::PageUp) {
            scrollRatio_ = std::max(0.0f, scrollRatio_ - 0.2f);
            autoScroll_ = false;
            r->virtualScroll_.setPinToBottom(false);
            r->virtualScroll_.pageUp(24);
            return true;
        }
        if (event == Event::F8 || event == Event::PageDown) {
            scrollRatio_ = std::min(1.0f, scrollRatio_ + 0.2f);
            if (scrollRatio_ >= 0.95f) {
                autoScroll_ = true;
                r->virtualScroll_.setPinToBottom(true);
            } else {
                autoScroll_ = false;
                r->virtualScroll_.setPinToBottom(false);
            }
            return true;
        }
        if (event == Event::End) {
            scrollRatio_ = 1.0f;
            autoScroll_ = true;
            r->virtualScroll_.setPinToBottom(true);
            return true;
        }
        if (event == Event::Home) {
            scrollRatio_ = 0.0f;
            autoScroll_ = false;
            r->virtualScroll_.setPinToBottom(false);
            r->virtualScroll_.scrollToTop();
            return true;
        }

        // Ctrl+O: toggle expanded tool view for all thinking/collapsed messages
        if (event == Event::CtrlO) {
            r->verboseTools_ = !r->verboseTools_;
            for (auto& m : r->messages_) {
                if (m.type == DisplayMessage::Type::AssistantThinking ||
                    m.type == DisplayMessage::Type::CollapsedReadSearch) {
                    m.expanded = r->verboseTools_;
                }
            }
            return true;
        }

        // Permission prompt — MUST be checked BEFORE isStreaming_
        // When a tool needs permission, isStreaming_ is still true but
        // we need to intercept arrow keys and Enter for the selection UI.
        if (r->permissionPromptActive_) {
            if (event == Event::ArrowUp || event == Event::CtrlP) {
                r->permissionFocusedIndex_ = (r->permissionFocusedIndex_ > 0)
                    ? r->permissionFocusedIndex_ - 1 : 4;
                return true;
            }
            if (event == Event::ArrowDown || event == Event::CtrlN) {
                r->permissionFocusedIndex_ = (r->permissionFocusedIndex_ < 4)
                    ? r->permissionFocusedIndex_ + 1 : 0;
                return true;
            }
            if (event == Event::Return) {
                // Convert focused index to PermissionChoice
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

                // Signal agent thread to continue
                {
                    std::lock_guard lock(r->permissionMutex_);
                    r->permissionResult_ = choice;
                    r->permissionAnswered_ = true;
                }
                r->permissionCv_.notify_one();
                return true;
            }
            if (event == Event::Escape) {
                // Escape = deny
                r->permissionPromptActive_ = false;
                {
                    std::lock_guard lock(r->permissionMutex_);
                    r->permissionResult_ = PermissionChoice::DenyOnce;
                    r->permissionAnswered_ = true;
                }
                r->permissionCv_.notify_one();
                return true;
            }
            // Swallow all other keys while permission prompt is active
            return true;
        }

        if (r->isStreaming_) {
            // ESC or Ctrl+C during streaming → cancel the running task
            if (event == Event::Escape || event == Event::CtrlC) {
                if (r->onCancel_) {
                    r->onCancel_();
                }

                r->isStreaming_ = false;
                r->isThinking_ = false;

                String partial = std::move(r->streamingText_);
                r->streamingText_.clear();
                r->streamingRenderer_.reset();
                if (!partial.empty()) {
                    auto msg = DisplayMessage::assistantText(std::move(partial));
                    msg.messageId = MessageIdGenerator::next();
                    r->messages_.push_back(std::move(msg));
                }
                {
                    auto msg = DisplayMessage::systemInfo("Cancelled");
                    msg.messageId = MessageIdGenerator::next();
                    r->messages_.push_back(std::move(msg));
                }

                r->stopRefreshThread();
                return true;
            }
            return false;
        }

        if (event == Event::Return) {
            if (!input_.empty()) {
                r->ctrlCPending_ = false;
                String current = input_;
                input_.clear();
                cursorPos_ = 0;
                auto msg = DisplayMessage::userPrompt(current);
                msg.messageId = MessageIdGenerator::next();
                r->messages_.push_back(std::move(msg));
                r->isStreaming_ = true;
                r->isThinking_ = true;
                r->streamingText_.clear();
                r->streamingRenderer_.reset();
                r->thinkingSummary_.clear();
                r->startTime_ = std::chrono::steady_clock::now();
                autoScroll_ = true;
                scrollRatio_ = 1.0f;
                tickCounter_ = 0;

                // Add to completer history and clear completions
                r->completer_.addHistory(current);
                r->completer_.clearCompletions();
                completionIndex_ = 0;
                lastCompletionInput_.clear();

                r->startRefreshThread();

                if (!current.empty() && current[0] == '/' && r->onCommand_) {
                    r->onCommand_(current);
                } else if (r->onSubmit_) {
                    r->onSubmit_(current);
                }
            }
            return true;
        }
        // Shift+Tab: cycle permission mode
        if (event == Event::TabReverse) {
            const char* modes[] = {"default", "acceptEdits", "auto", "bypass", "dontAsk", "plan"};
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
        // Tab completion: accept common prefix or cycle through completions
        if (event == Event::Tab) {
            const auto& completions = r->completer_.currentCompletions();
            if (!completions.empty()) {
                if (completions.size() == 1) {
                    // Single completion: accept it directly
                    input_ = completions[0];
                    cursorPos_ = input_.size();
                    r->completer_.clearCompletions();
                    completionIndex_ = 0;
                    lastCompletionInput_.clear();
                } else if (completionIndex_ < completions.size()) {
                    // Cycle through completions on repeated Tab
                    // First Tab: insert common prefix. Subsequent Tabs: cycle.
                    if (lastCompletionInput_ == input_) {
                        // Cycling mode
                        completionIndex_ = (completionIndex_ + 1) % completions.size();
                        input_ = completions[completionIndex_];
                        cursorPos_ = input_.size();
                    } else {
                        // First Tab: accept common prefix
                        String prefix = r->completer_.commonPrefix(input_);
                        if (prefix != input_) {
                            input_ = prefix;
                            cursorPos_ = input_.size();
                            lastCompletionInput_ = input_;
                            // Re-update completions for the new prefix
                            r->completer_.updateCompletions(input_, cursorPos_);
                            completionIndex_ = 0;
                        } else {
                            // Common prefix is same as input — cycle instead
                            completionIndex_ = 0;
                            input_ = completions[completionIndex_];
                            cursorPos_ = input_.size();
                            lastCompletionInput_ = input_;
                        }
                    }
                }
                return true;
            }
            // No completions: Tab does nothing
            return true;
        }
        // Arrow up/down in completion list
        if (event == Event::ArrowUp && !r->completer_.currentCompletions().empty()) {
            if (completionIndex_ > 0) {
                completionIndex_--;
            } else {
                completionIndex_ = r->completer_.currentCompletions().size() - 1;
            }
            return true;
        }
        if (event == Event::ArrowDown && !r->completer_.currentCompletions().empty()) {
            completionIndex_ = (completionIndex_ + 1) % r->completer_.currentCompletions().size();
            return true;
        }
        if (event.is_character()) {
            // Insert character at cursor position
            input_.insert(cursorPos_, event.character());
            cursorPos_ += event.character().size();
            // Update completions for new input
            r->completer_.updateCompletions(input_, cursorPos_);
            completionIndex_ = 0;
            lastCompletionInput_ = input_;
            return true;
        }
        if (event == Event::ArrowLeft && cursorPos_ > 0) {
            // Move cursor left, skipping UTF-8 continuation bytes
            while (cursorPos_ > 0) {
                cursorPos_--;
                auto c = static_cast<unsigned char>(input_[cursorPos_]);
                if ((c & 0xC0) != 0x80) break;  // Found start of UTF-8 sequence
            }
            return true;
        }
        if (event == Event::ArrowRight && cursorPos_ < input_.size()) {
            // Move cursor right, skipping UTF-8 continuation bytes
            while (cursorPos_ < input_.size()) {
                cursorPos_++;
                if (cursorPos_ >= input_.size()) break;
                auto c = static_cast<unsigned char>(input_[cursorPos_]);
                if ((c & 0xC0) != 0x80) break;  // Found start of next char
            }
            return true;
        }
        if (event == Event::Home) {
            cursorPos_ = 0;
            return true;
        }
        if (event == Event::End) {
            cursorPos_ = input_.size();
            return true;
        }
        if (event == Event::Backspace && cursorPos_ > 0) {
            // Delete character before cursor
            size_t deleteStart = cursorPos_;
            while (deleteStart > 0) {
                deleteStart--;
                auto c = static_cast<unsigned char>(input_[deleteStart]);
                if ((c & 0xC0) != 0x80) break;
            }
            input_.erase(deleteStart, cursorPos_ - deleteStart);
            cursorPos_ = deleteStart;
            // Update completions
            r->completer_.updateCompletions(input_, cursorPos_);
            completionIndex_ = 0;
            lastCompletionInput_ = input_;
            return true;
        }
        if (event == Event::Delete && cursorPos_ < input_.size()) {
            // Delete character after cursor
            size_t deleteEnd = cursorPos_;
            while (deleteEnd < input_.size()) {
                deleteEnd++;
                if (deleteEnd >= input_.size()) break;
                auto c = static_cast<unsigned char>(input_[deleteEnd]);
                if ((c & 0xC0) != 0x80) break;
            }
            input_.erase(cursorPos_, deleteEnd - cursorPos_);
            // Update completions
            r->completer_.updateCompletions(input_, cursorPos_);
            completionIndex_ = 0;
            lastCompletionInput_ = input_;
            return true;
        }
        // Ctrl+Y: copy selected text to clipboard
        if (event == Event::CtrlY && r->screen_) {
            auto selected = r->screen_->GetSelection();
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
                    auto msg = DisplayMessage::systemInfo("Copied " + std::to_string(len) + " chars to clipboard");
                    msg.messageId = MessageIdGenerator::next();
                    r->messages_.push_back(std::move(msg));
                });
            }
            return true;
        }
        if (event == Event::Escape) {
            // ESC: clear completions if active, otherwise no-op
            if (!r->completer_.currentCompletions().empty()) {
                r->completer_.clearCompletions();
                completionIndex_ = 0;
                lastCompletionInput_.clear();
                return true;
            }
            // ESC at idle prompt with no completions → no-op (matches TS behavior)
            return true;
        }
        if (event == Event::CtrlC) {
            auto now = std::chrono::steady_clock::now();
            if (r->ctrlCPending_) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - r->lastCtrlC_).count();
                if (elapsed < 800) {
                    r->exit();
                    return true;
                }
            }
            r->ctrlCPending_ = true;
            r->lastCtrlC_ = now;
            {
                auto msg = DisplayMessage::systemInfo("Press Ctrl+C again to exit");
                msg.messageId = MessageIdGenerator::next();
                r->messages_.push_back(std::move(msg));
            }
            return true;
        }
        return false;
    }

    bool Focusable() const override { return true; }

private:
    String input_;
    size_t cursorPos_ = 0;
    FtxuiRepl* owner_;
    bool autoScroll_ = true;
    float scrollRatio_ = 1.0f;
    int tickCounter_ = 0;
    size_t completionIndex_ = 0;      // Currently selected completion (for cycling)
    String lastCompletionInput_;       // Last input used to compute completions
};

} // namespace detail

// ========== BuildMainComponent — delegates to detail::MainComponent ==========

ftxui::Component FtxuiRepl::BuildMainComponent() {
    return std::make_shared<detail::MainComponent>(this);
}

} // namespace claude

#endif // HAS_FTXUI
