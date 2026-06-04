#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/ui/components/AppLayout.hpp"
#include "claude/console/CreativeVerbs.hpp"
#include "FtxuiColors.hpp"
#include <spdlog/spdlog.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstdlib>
#include <termios.h>
#include <algorithm>

namespace claude {

using namespace ftxui_colors;

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

    // Content — point messages pointer directly (no copy)
    ls.content.messages = &messages_;
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
    ls.content.messagesAbove = static_cast<int>(virtualScroll_.firstVisibleIndex());
    ls.content.autoScroll = ls.autoScroll;
    ls.content.scrollRatio = ls.scrollRatio;
    if (ls.autoScroll) {
        ls.scrollRatio = 1.0f;
        ls.content.scrollRatio = 1.0f;
    }

    // Input
    ls.input.streaming = isStreaming_;

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

    // Completions
    const auto& completions = completer_.currentCompletions();
    ls.completions.assign(completions.begin(), completions.end());

    // Verbose tools
    ls.verboseTools = verboseTools_;

    // Text selection
    ls.selectionActive = selectionActive_;
    ls.selectionStartY = selectionHighlightStartY_;
    ls.selectionEndY = selectionHighlightEndY_;
}

// ========== Constructor / Destructor ==========

FtxuiRepl::FtxuiRepl() = default;

FtxuiRepl::~FtxuiRepl() {
    running_ = false;
    stopRefreshThread();
}

// ========== Thread-safe message operations ==========

void FtxuiRepl::addUserMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        auto msg = DisplayMessage::userPrompt(std::move(c));
        msg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(msg));
    });
}

void FtxuiRepl::addAssistantMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        auto msg = DisplayMessage::assistantText(std::move(c));
        msg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(msg));
    });
}

void FtxuiRepl::addToolMessage(const String& toolName, const String& input, const String& result) {
    if (!screen_) return;
    screen_->Post([this, tn = String(toolName), inp = String(input), res = String(result)]() {
        // If this is a result callback (empty input, non-empty result),
        // find the EARLIEST unmatched ToolUse message with the same name.
        // Using earliest (not latest) ensures correct pairing when multiple
        // tools of the same type run concurrently (e.g. 3 Read files).
        if (inp.empty() && !res.empty()) {
            for (size_t i = 0; i < messages_.size(); ++i) {
                if (messages_[i].type == DisplayMessage::Type::AssistantToolUse &&
                    messages_[i].toolUse.toolName == tn) {
                    // Check if this tool_use already has a paired result
                    bool alreadyPaired = false;
                    for (size_t j = i + 1; j < messages_.size(); ++j) {
                        if (messages_[j].type == DisplayMessage::Type::UserToolResult &&
                            messages_[j].toolResult.toolUseId == messages_[i].toolUse.toolId) {
                            alreadyPaired = true;
                            break;
                        }
                    }
                    if (!alreadyPaired) {
                        auto resultMsg = DisplayMessage::userToolResult(
                            ToolResultBlock{messages_[i].toolUse.toolId, tn, std::move(res), false});
                        resultMsg.messageId = MessageIdGenerator::next();
                        messages_.push_back(std::move(resultMsg));
                        return;
                    }
                }
            }
        }
        // Tool start: add as AssistantToolUse
        auto msg = DisplayMessage::assistantToolUse(
            ToolUseBlock{MessageIdGenerator::next(), std::move(tn), std::move(inp)});
        msg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(msg));
    });
}

void FtxuiRepl::addSystemMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        auto msg = DisplayMessage::systemInfo(std::move(c));
        msg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(msg));
    });
}

void FtxuiRepl::addErrorMessage(const String& content) {
    if (!screen_) return;
    screen_->Post([this, c = String(content)]() {
        auto msg = DisplayMessage::systemError(std::move(c));
        msg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(msg));
    });
}

void FtxuiRepl::addTurnDurationMessage(int durationMs) {
    if (!screen_) return;
    int seconds = durationMs / 1000;
    String msg = console::CreativeVerbs::randomCreativeVerb() + " for " + formatElapsed(seconds);
    screen_->Post([this, m = std::move(msg)]() {
        auto dmsg = DisplayMessage::turnDuration(std::move(m));
        dmsg.messageId = MessageIdGenerator::next();
        messages_.push_back(std::move(dmsg));
    });
}

void FtxuiRepl::clearMessages() {
    if (!screen_) return;
    screen_->Post([this]() { messages_.clear(); });
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

    spdlog::debug("FTXUI: Building component...");
    auto component = BuildMainComponent();
    spdlog::debug("FTXUI: Creating screen...");
    auto screen = ScreenInteractive::Fullscreen();
    screen_ = &screen;
    spdlog::debug("FTXUI: Enabling mouse tracking...");
    screen.TrackMouse();
    spdlog::debug("FTXUI: Starting loop...");

    try {
        screen.Loop(component);
    } catch (const std::exception& e) {
        spdlog::error("FTXUI loop exception: {}", e.what());
    }

    screen_ = nullptr;
    stopRefreshThread();
    spdlog::debug("FTXUI: Loop ended");
}

void FtxuiRepl::exit() {
    running_ = false;
    stopRefreshThread();
    if (screen_) {
        screen_->Exit();
    }
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

    // Link messages to layoutState (rendered by AppLayout)
    layoutState_.content.messages = &messages_;

    // Build the AppLayout renderer component
    auto layoutComp = ui::AppLayoutComponent(layoutState_, *renderContext_);

    // Wrap with CatchEvent for all keyboard/mouse handling
    // This preserves the same event handling as the old MainComponent
    auto* r = this;
    auto* ls = &layoutState_;

    auto eventHandler = CatchEvent([r, ls](Event event) -> bool {
        // Sync state from FtxuiRepl to layoutState_ before processing
        r->syncLayoutState();

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

        // Ctrl+O: toggle expanded tool view
        if (event == Event::CtrlO) {
            r->verboseTools_ = !r->verboseTools_;
            ls->verboseTools = r->verboseTools_;
            for (auto& m : r->messages_) {
                if (m.type == DisplayMessage::Type::AssistantThinking ||
                    m.type == DisplayMessage::Type::CollapsedReadSearch) {
                    m.expanded = r->verboseTools_;
                }
            }
            return true;
        }

        // Permission prompt — MUST be checked BEFORE isStreaming_
        if (r->permissionPromptActive_) {
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

                {
                    std::lock_guard lock(r->permissionMutex_);
                    r->permissionResult_ = choice;
                    r->permissionAnswered_ = true;
                }
                r->permissionCv_.notify_one();
                return true;
            }
            if (event == Event::Escape) {
                r->permissionPromptActive_ = false;
                ls->permissionActive = false;
                {
                    std::lock_guard lock(r->permissionMutex_);
                    r->permissionResult_ = PermissionChoice::DenyOnce;
                    r->permissionAnswered_ = true;
                }
                r->permissionCv_.notify_one();
                return true;
            }
            return true;
        }

        if (r->isStreaming_) {
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
            if (!ls->input.text.empty()) {
                r->ctrlCPending_ = false;
                String current = ls->input.text;
                ls->input.text.clear();
                ls->input.cursorPos = 0;
                auto msg = DisplayMessage::userPrompt(current);
                msg.messageId = MessageIdGenerator::next();
                r->messages_.push_back(std::move(msg));
                r->isStreaming_ = true;
                r->isThinking_ = true;
                r->streamingText_.clear();
                r->streamingRenderer_.reset();
                r->thinkingSummary_.clear();
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
                    r->onCommand_(current);
                } else if (r->onSubmit_) {
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
                    auto msg = DisplayMessage::systemInfo("Copied " + std::to_string(len) + " chars to clipboard");
                    msg.messageId = MessageIdGenerator::next();
                    r->messages_.push_back(std::move(msg));
                });
                // Clear selection after copy
                r->selectionActive_ = false;
                r->selectionText_.clear();
            }
            return true;
        }
        if (event == Event::Escape) {
            if (!r->completer_.currentCompletions().empty()) {
                r->completer_.clearCompletions();
                ls->completions.clear();
                ls->completionIndex = 0;
                ls->lastCompletionInput.clear();
                return true;
            }
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
