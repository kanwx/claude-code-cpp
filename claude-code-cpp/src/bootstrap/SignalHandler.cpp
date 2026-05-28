#include <claude/bootstrap/SignalHandler.hpp>
#include <claude/core/AgentLoop.hpp>

#include <unistd.h>
#include <termios.h>

namespace claude {

// Global variable definitions
std::atomic<bool> g_agentStreaming{false};
AgentLoop* g_agentLoop = nullptr;
std::atomic<bool> g_interruptRequested{false};
std::atomic<int> g_ctrlCCount{0};
std::chrono::steady_clock::time_point g_lastCtrlCTime{};

void restoreTerminal() {
    // Disable all mouse tracking modes (DECSET 1000/1002/1003/1006)
    // These are the sequences FTXUI's TrackMouse() enables. Writing them
    // directly ensures cleanup even when FTXUI's Uninstall() is bypassed.
    write(STDOUT_FILENO, "\x1b[?1000l", 8);  // Disable basic mouse tracking
    write(STDOUT_FILENO, "\x1b[?1002l", 8);  // Disable button-event tracking
    write(STDOUT_FILENO, "\x1b[?1003l", 8);  // Disable any-event tracking
    write(STDOUT_FILENO, "\x1b[?1006l", 8);  // Disable SGR mouse mode

    // Restore cursor visibility and reset text attributes
    write(STDOUT_FILENO, "\x1b[?25h", 6);    // Show cursor
    write(STDOUT_FILENO, "\x1b[0m", 4);      // Reset all attributes

    // Restore terminal from raw mode (tcsetattr with original settings)
    // FTXUI's Install() sets raw mode; if we bypass Uninstall(), we need
    // to restore canonical mode so the shell works normally after exit.
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag |= (ICANON | ECHO | ISIG);
        t.c_lflag &= ~(VMIN | VTIME);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
}

void signalHandler(int signal) {
    if (signal == SIGTERM) {
        restoreTerminal();
        _Exit(143);
    }

    if (signal == SIGINT) {
        // If agent is streaming, set the interrupt flag.
        // The agent loop checks this flag and cancels itself.
        if (g_agentStreaming.load(std::memory_order_acquire)) {
            g_interruptRequested.store(true, std::memory_order_release);
            // Also abort the API client's stream directly (atomic store, signal-safe)
            if (g_agentLoop) {
                g_agentLoop->cancel();
            }
            return;
        }

        // At idle prompt: double-press-to-exit
        // write() is async-signal-safe
        const char msg[] = "\nPress Ctrl+C again to exit\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);

        auto now = std::chrono::steady_clock::now();
        int count = g_ctrlCCount.fetch_add(1, std::memory_order_relaxed) + 1;

        if (count >= 2) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_lastCtrlCTime).count();
            if (elapsed < 800) {
                restoreTerminal();
                _Exit(0);
            }
            // Too slow — reset and treat as first press
            g_ctrlCCount.store(1, std::memory_order_relaxed);
        }

        g_lastCtrlCTime = now;
    }
}

void installSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

} // namespace claude
