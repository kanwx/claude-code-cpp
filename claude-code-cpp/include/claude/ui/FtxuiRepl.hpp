#pragma once

#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "FtxuiMarkdown.hpp"
#include "VirtualScroll.hpp"
#include "components/AppLayout.hpp"
#include "../permission/PermissionTypes.hpp"
#include "../repl/Completer.hpp"
#include "../bootstrap/AppState.hpp"
#include "../stream/DisplayEvent.hpp"
#include "../stream/ContentBlock.hpp"
#include "../stream/AnswerPostProcessor.hpp"
#include "../stream/MessagePipeline.hpp"
#include "../metrics/TurnMetricsCollector.hpp"
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace claude {

class FtxuiRepl {
public:
    using OnSubmit = std::function<void(const String&)>;
    using OnCommand = std::function<bool(const String&)>;
    using OnCancel = std::function<void()>;

    FtxuiRepl();
    ~FtxuiRepl();

    void setOnSubmit(OnSubmit cb) { onSubmit_ = std::move(cb); }
    void setOnCommand(OnCommand cb) { onCommand_ = std::move(cb); }
    void setOnCancel(OnCancel cb) { onCancel_ = std::move(cb); }
    void setModelInfo(const String& info);
    void setContextInfo(long usedTokens, long maxTokens, double costUsd);
    void setTokenCounts(int inputTokens, int outputTokens);

    /// Link to AppState for reactive state accessors.
    void setAppState(AppState* state) { appState_ = state; }

    /// Access the ReplCompleter for configuration
    ReplCompleter& completer() { return completer_; }
    const ReplCompleter& completer() const { return completer_; }

    // Thread-safe message operations (use Post internally)
    void addUserMessage(const String& content);
    void addAssistantMessage(const String& content);
    void addToolMessage(const String& toolName, const String& input, const String& result);
    void addSystemMessage(const String& content);
    void addErrorMessage(const String& content);
    void addTurnDurationMessage(int durationMs);
    void clearMessages();

    // Thread-safe streaming
    void appendStreamText(const String& chunk);
    void finishStream(bool success, const String& error = "");

    // Thread-safe thinking update
    void updateThinkingSummary(const String& summary);
    void addThinkingMessage(const String& chunk);

    // New 5-layer pipeline display event handler
    void handleDisplayEvent(DisplayEvent&& event);

    // Pipeline-aware message operations
    void addToolUseStart(const String& toolName, const String& toolId, const String& input);
    void addToolUseComplete(const String& toolId, const String& toolInput);
    void addToolResult(const String& toolName, const String& toolId, const String& result, bool isError = false,
                       bool isRejected = false, bool isCancelled = false);

    // ========== Permission prompt (thread-safe, blocking for caller) ==========
    PermissionChoice promptPermission(const String& toolName, const String& activity);

    void run();
    void exit();
    bool isRunning() const { return running_; }

    /// Handle Ctrl+C double-press exit: first press shows hint, second within
    /// timeout exits. Callable from tests to simulate the event handler.
    /// @param now Current steady_clock time (injected for test determinism)
    /// @param timeoutMs Double-press window (2000ms default)
    /// @return true if the event was handled (hint shown or exit triggered)
    bool handleCtrlC(std::chrono::steady_clock::time_point now, int timeoutMs = 2000);

    /// Enable turn-level metrics collection (for prompt A/B testing).
    /// @param outputPath Path to JSONL file for metrics output.
    void enableMetricsCollection(const std::string& outputPath);

    /// Trigger a screen refresh from any thread. Used by idle callbacks during
    /// long-running blocking tool execution (e.g. AgentTool sub-agent polling)
    /// to keep the terminal responsive.
    void triggerRefresh() {
        if (screen_) screen_->RequestAnimationFrame();
    }

    // Access to content blocks (for rendering)
    const std::vector<ContentBlock>& contentBlocks() const { return contentBlocks_; }
    std::vector<ContentBlock>& contentBlocks() { return contentBlocks_; }

    // State setters (called from main.cpp during setup)
    void setCurrentMode(const String& mode) { currentMode_ = mode; modeHintDismissed_ = false; }
    void setAuthStatus(bool authenticated) { isAuthenticated_ = authenticated; }
    void setCwd(const String& cwd) { cwd_ = cwd; }
    void setGitBranch(const String& branch) { gitBranch_ = branch; }
    void dismissModeHint() { modeHintDismissed_ = true; }

private:
    ftxui::Component BuildMainComponent();
    void syncLayoutState();
    static String formatElapsed(int seconds);
    static String truncate(const String& s, size_t maxLen);

    void startRefreshThread();
    void stopRefreshThread();
    void refreshLoop();

    void groupConsecutiveToolResults();  // Deprecated: kept for ANSI mode

    // Run the full MessagePipeline on contentBlocks_ (called at AnswerEnd)
    void runMessagePipeline();

    // Run pipeline incrementally on only the unstable tail [lastStableIndex_, end)
    void runIncrementalPipeline();

    // UI state — only modified from closures posted to the UI thread via screen_->Post()
    std::vector<ContentBlock> contentBlocks_;
    String streamingText_;

    // Turn boundary tracking (Fix B6)
    size_t currentTurnStartIndex_ = 0;   // current API response start (updated at AnswerStart)
    size_t metricsTurnStartIndex_ = 0;   // user turn start (updated ONLY at UserMessage)
    int apiRoundIndex_ = 0;              // which API call within current user turn (0-based)
    int userTurnIndex_ = 0;              // logical user turn id (incremented at each UserMessage)
    std::vector<size_t> turnBoundaries_;
    static constexpr size_t MAX_BLOCKS = 2000;

    // ToolProgress index tracking (Fix B5) — O(1) lookup for ToolResult→ToolProgress replacement
    std::map<String, size_t> toolProgressIndices_;  // toolCallId -> index in contentBlocks_
    FtxuiMarkdown::StreamingRenderer streamingRenderer_;
    bool isStreaming_ = false;
    bool isThinking_ = false;
    bool isFirstAnswerBlock_ = true;
    String thinkingSummary_;
    String thinkingText_;
    String thinkingVerb_;           // running verb per turn, e.g. "Wandering"
    String modelInfo_;
    long contextUsedTokens_ = 0;
    long contextMaxTokens_ = 0;
    double costUsd_ = 0.0;
    int inputTokens_ = 0;
    int outputTokens_ = 0;
    std::chrono::steady_clock::time_point startTime_{};
    bool turnStarted_ = false;           // Set on first AnswerStart, cleared at finishStream
    bool turnDurationEmitted_ = false;   // Guard against duplicate TurnDuration

    VirtualScroll virtualScroll_;
    bool verboseTools_ = false;
    std::chrono::steady_clock::time_point lastOutputTime_{};

    TurnMetadata newPipelineStatusMetadata_;

    ftxui::ScreenInteractive* screen_ = nullptr;

    OnSubmit onSubmit_;
    OnCommand onCommand_;
    OnCancel onCancel_;

    // Double Ctrl+C exit tracking
    std::chrono::steady_clock::time_point lastCtrlC_;
    bool ctrlCPending_ = false;

    std::atomic<bool> running_{true};

    // Stream buffer
    std::mutex streamMutex_;
    String streamBuffer_;

    // Thinking buffer
    std::mutex thinkingMutex_;
    String thinkingBuffer_;

    // Refresh thread
    std::thread refreshThread_;
    std::atomic<bool> refreshActive_{false};
    std::atomic<bool> refreshContentChanged_{false};  // Set by UI thread when new display events arrive

    // ========== Completion state ==========
    ReplCompleter completer_;

    // ========== Text selection state ==========
    bool selectionActive_ = false;
    int selectionStartX_ = 0;
    int selectionStartY_ = 0;
    int selectionEndX_ = 0;
    int selectionEndY_ = 0;
    String selectionText_;
    int selectionHighlightStartY_ = 0;
    int selectionHighlightEndY_ = 0;

    // ========== Permission prompt state ==========
    bool permissionPromptActive_ = false;
    int permissionFocusedIndex_ = 0;
    String permissionToolName_;
    String permissionActivity_;
    String permissionDescription_;
    bool permissionFeedbackActive_ = false;
    String permissionFeedbackText_;
    size_t permissionFeedbackCursorPos_ = 0;

    std::mutex permissionMutex_;
    std::condition_variable permissionCv_;
    PermissionChoice permissionResult_ = PermissionChoice::DenyOnce;
    String permissionFeedbackResult_;
    bool permissionAnswered_ = false;

    static const ftxui::Color BrandOrange;

    std::atomic<size_t> turnVerbIndex_{0};

    String currentMode_;
    bool modeHintDismissed_ = false;
    bool isAuthenticated_ = false;
    String cwd_;
    String gitBranch_;
    AppState* appState_ = nullptr;

    // ========== AppLayout state ==========
    ui::AppLayoutState layoutState_;
    std::unique_ptr<ui::RenderContext> renderContext_;

    // ========== Message pipeline ==========
    MessagePipeline messagePipeline_;

    // ========== Stable block ID counter ==========
    uint64_t nextStableId_ = 1;
    size_t lastStableIndex_ = 0;       // blocks before this index are pipeline-stable

    // ========== Metrics collector (optional, for A/B testing) ==========
    std::unique_ptr<TurnMetricsCollector> metricsCollector_;
};

} // namespace claude

#endif // HAS_FTXUI
