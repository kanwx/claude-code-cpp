#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"
#include "claude/console/CreativeVerbs.hpp"
#include <spdlog/spdlog.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstdlib>
#include <termios.h>

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

namespace claude {

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

} // namespace claude

#endif // HAS_FTXUI
