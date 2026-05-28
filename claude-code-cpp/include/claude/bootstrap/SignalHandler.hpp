#pragma once

#include <csignal>
#include <atomic>
#include <chrono>

namespace claude {

class AgentLoop;  // forward declaration

/// Streaming state flag — set true when agent is streaming, false at idle.
/// Checked by signalHandler to decide Ctrl+C behavior.
extern std::atomic<bool> g_agentStreaming;

/// Pointer to the active AgentLoop — used by signal handler to cancel streaming.
extern AgentLoop* g_agentLoop;

/// Interrupt requested flag — set by SIGINT during streaming, checked by agent loop.
extern std::atomic<bool> g_interruptRequested;

/// Ctrl+C press counter for double-press-to-exit logic.
extern std::atomic<int> g_ctrlCCount;

/// Timestamp of last Ctrl+C press (steady clock for elapsed-time comparison).
extern std::chrono::steady_clock::time_point g_lastCtrlCTime;

/// Restore terminal to sane state — disables mouse tracking, restores cursor,
/// resets attributes, and restores canonical mode.
/// MUST be called before _Exit() on every exit path.
void restoreTerminal();

/// Signal handler — context-aware interrupt handling.
/// - During streaming: sets g_interruptRequested flag (async-signal-safe)
/// - At idle prompt: double-press-to-exit (800ms window)
/// - SIGTERM always exits immediately
void signalHandler(int sig);

/// Register SIGINT and SIGTERM handlers. Call once at program startup.
void installSignalHandlers();

} // namespace claude
