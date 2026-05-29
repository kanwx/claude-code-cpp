#pragma once

#ifdef HAS_FTXUI

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "UiMessageTypes.hpp"
#include "FtxuiMarkdown.hpp"
#include "VirtualScroll.hpp"
#include "MessagePipeline.hpp"
#include "components/MessageRenderer.hpp"
#include "../permission/PermissionTypes.hpp"
#include "../console/CreativeVerbs.hpp"
#include "../repl/Completer.hpp"
#include "../core/ReactiveState.hpp"
#include <functional>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace claude {

namespace detail { class MainComponent; } // forward-declare for friend access

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
    void setModelInfo(const String& info) { modelInfo_ = info; }
    void setContextInfo(long usedTokens, long maxTokens, double costUsd) {
        contextUsedTokens_ = usedTokens;
        contextMaxTokens_ = maxTokens;
        costUsd_ = costUsd;
    }

    void setTokenCounts(int inputTokens, int outputTokens) {
        inputTokens_ = inputTokens;
        outputTokens_ = outputTokens;
    }

    /// Link to AppState for reactive state accessors.
    /// When set, the status bar can derive values via reactive::accessors.
    void setAppState(AppState* state) { appState_ = state; }

    /// Access the ReplCompleter for configuration (add commands, tools, set workDir)
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

    // Thread-safe streaming — uses screen_->Post() for immediate UI update
    void appendStreamText(const String& chunk);
    void finishStream(bool success, const String& error = "");

    // Thread-safe thinking update
    void updateThinkingSummary(const String& summary);
    void addThinkingMessage(const String& fullText);

    // ========== Permission prompt (thread-safe, blocking for caller) ==========

    /// Show permission prompt and wait for user selection.
    /// Called from agent background thread. Blocks until user chooses.
    /// Returns the user's PermissionChoice.
    PermissionChoice promptPermission(const String& toolName, const String& activity);

    void run();
    void exit();
    bool isRunning() const { return running_; }

    // Access to display messages (for rendering)
    const std::vector<DisplayMessage>& messages() const { return messages_; }
    std::vector<DisplayMessage>& messages() { return messages_; }

    // State setters (called from main.cpp during setup)
    void setCurrentMode(const String& mode) { currentMode_ = mode; modeHintDismissed_ = false; }
    void setAuthStatus(bool authenticated) { isAuthenticated_ = authenticated; }
    void setCwd(const String& cwd) { cwd_ = cwd; }
    void setGitBranch(const String& branch) { gitBranch_ = branch; }
    void dismissModeHint() { modeHintDismissed_ = true; }

private:
    friend class detail::MainComponent;  // extracted render component needs private access

    ftxui::Component BuildMainComponent();
    static String formatElapsed(int seconds);
    static String truncate(const String& s, size_t maxLen);

    void startRefreshThread();
    void stopRefreshThread();
    void refreshLoop();

    /// Initialize the renderer registry with all component renderers
    void initRendererRegistry();

    /// Render a single DisplayMessage using the registry (or inline fallback)
    std::vector<ftxui::Element> renderMessage(const DisplayMessage& msg, const RendererContext& ctx);

    // UI state — only modified from closures posted to the UI thread via screen_->Post()
    std::vector<DisplayMessage> messages_;
    String streamingText_;
    FtxuiMarkdown::StreamingRenderer streamingRenderer_;
    bool isStreaming_ = false;
    bool isThinking_ = false;
    String thinkingSummary_;
    String thinkingText_;     // full thinking text (for expand/collapse)
    String modelInfo_;
    long contextUsedTokens_ = 0;
    long contextMaxTokens_ = 0;
    double costUsd_ = 0.0;
    int inputTokens_ = 0;
    int outputTokens_ = 0;
    std::chrono::steady_clock::time_point startTime_{};

    // Virtual scrolling — replaces crude visibleCount_=50
    VirtualScroll virtualScroll_;
    bool verboseTools_ = false;       // Ctrl+O toggles expanded tool view
    std::chrono::steady_clock::time_point lastOutputTime_{};  // stall detection

    // Message pipeline — replaces inline tool-pairing and grouping
    MessagePipeline messagePipeline_;

    // Renderer registry — replaces inline rendering dispatch
    RendererRegistry rendererRegistry_;
    bool registryInitialized_ = false;

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

    // Refresh thread — spinner animation + safety net flush
    std::thread refreshThread_;
    std::atomic<bool> refreshActive_{false};

    // ========== Completion state ==========
    ReplCompleter completer_;

    // ========== Permission prompt state ==========
    bool permissionPromptActive_ = false;   // UI is showing permission prompt
    int permissionFocusedIndex_ = 0;         // Currently focused option (0-4: 5 options)
    String permissionToolName_;              // Tool name being asked about
    String permissionActivity_;              // Activity description
    String permissionDescription_;           // Tool-specific description like "Allow Bash to run: npm test"

    // Synchronization: agent thread waits for user choice
    std::mutex permissionMutex_;
    std::condition_variable permissionCv_;
    PermissionChoice permissionResult_ = PermissionChoice::DenyOnce;
    bool permissionAnswered_ = false;

    // Brand color
    static const ftxui::Color BrandOrange;

    // Creative verb index for turn duration
    std::atomic<size_t> turnVerbIndex_{0};

    // Mode state
    String currentMode_;
    bool modeHintDismissed_ = false;

    // Auth state
    bool isAuthenticated_ = false;

    // Cwd/git state
    String cwd_;
    String gitBranch_;

    // Optional AppState link for reactive state accessors
    AppState* appState_ = nullptr;
};

} // namespace claude

#endif // HAS_FTXUI
