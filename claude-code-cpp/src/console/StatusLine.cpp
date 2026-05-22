#include <claude/console/StatusLine.hpp>
#include <claude/console/AnsiStyle.hpp>

#ifdef __APPLE__
#include <sys/ioctl.h>
#include <unistd.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <fstream>
#include <filesystem>

namespace claude {

StatusLine::StatusLine(std::ostream& out)
    : out_(out), sessionStartTime_(std::chrono::steady_clock::now()) {}

StatusLine::~StatusLine() {
    stopAutoRefresh();
}

void StatusLine::enable(const String& model, TokenTracker& tracker) {
    modelName_ = model;
    tokenTracker_ = &tracker;
    workDir_ = abbreviatePath(std::filesystem::current_path().string());
    workspace_ = std::filesystem::current_path().string();
    sessionStartTime_ = std::chrono::steady_clock::now();
    enabled_ = true;
    startAutoRefresh();
}

void StatusLine::disable() {
    stopAutoRefresh();
    enabled_ = false;
    clearStatusLine();
}

int StatusLine::getTerminalHeight() const {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_row;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_row;
    return 24;
}

int StatusLine::getTerminalWidth() const {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_col;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_col;
    return 80;
}

int StatusLine::getContextPercentage() const {
    if (!tokenTracker_) return 0;
    long total = tokenTracker_->getTotalTokens();
    long window = tokenTracker_->getContextWindow();
    if (window <= 0) return 0;
    return static_cast<int>((total * 100) / window);
}

String StatusLine::formatTokenCount(long tokens) {
    if (tokens >= 1'000'000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (tokens / 1'000'000.0) << "M";
        return oss.str();
    }
    if (tokens >= 10'000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (tokens / 1'000.0) << "K";
        return oss.str();
    }
    if (tokens >= 1'000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (tokens / 1'000.0) << "K";
        return oss.str();
    }
    return std::to_string(tokens);
}

void StatusLine::refresh() {
    if (!enabled_ || !tokenTracker_) return;

    // Check debouncing — only execute command at most once per second
    auto now = std::chrono::steady_clock::now();
    if (now - lastCommandTime_ >= std::chrono::seconds(1)) {
        cachedCommandOutput_ = executeStatusLineCommand();
        lastCommandTime_ = now;
    }

    String status;
    if (!cachedCommandOutput_.empty()) {
        status = cachedCommandOutput_;
    } else {
        status = buildStatusText();
    }

    int height = getTerminalHeight();

    out_ << AnsiStyle::SAVE_CURSOR;
    out_ << AnsiStyle::moveCursor(height, 1);
    out_ << AnsiStyle::CLEAR_LINE;
    out_ << status;
    out_ << AnsiStyle::RESTORE_CURSOR;
    out_ << std::flush;
}

String StatusLine::renderInline() {
    if (!enabled_ || !tokenTracker_) return "";

    auto now = std::chrono::steady_clock::now();
    if (now - lastCommandTime_ >= std::chrono::seconds(1)) {
        cachedCommandOutput_ = executeStatusLineCommand();
        lastCommandTime_ = now;
    }

    if (!cachedCommandOutput_.empty()) {
        return cachedCommandOutput_;
    }
    return buildStatusText();
}

String StatusLine::buildStatusText() {
    double cost = tokenTracker_->estimateCost();
    int ctxPercent = getContextPercentage();
    long inputTokens = tokenTracker_->getInputTokens();
    long outputTokens = tokenTracker_->getOutputTokens();

    // Cache values for status line command env vars
    {
        std::ostringstream tmp;
        tmp << std::fixed << std::setprecision(4) << cost;
        costStr_ = tmp.str();
    }
    contextPctStr_ = std::to_string(ctxPercent);

    std::ostringstream ss;
    ss << AnsiStyle::Semantic::STATUS_DIM;

    // Model name
    ss << modelName_;

    // Context percentage with color-coded threshold
    ss << " \xc2\xb7 ";  // ·

    if (ctxPercent < 50) {
        ss << AnsiStyle::Semantic::CONTEXT_OK;
    } else if (ctxPercent < 80) {
        ss << AnsiStyle::Semantic::CONTEXT_WARN;
    } else {
        ss << AnsiStyle::Semantic::CONTEXT_CRIT;
    }
    ss << ctxPercent << "%" << " ctx";

    // Token breakdown: input/output
    ss << AnsiStyle::Semantic::STATUS_DIM;
    ss << " \xc2\xb7 ";
    ss << formatTokenCount(inputTokens) << " in";
    ss << "/";
    ss << formatTokenCount(outputTokens) << " out";

    // Cost with 4 decimal places (matching TS design)
    ss << " \xc2\xb7 ";
    ss << "$" << std::fixed << std::setprecision(4) << cost;

    // Session duration
    auto now = std::chrono::steady_clock::now();
    auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - sessionStartTime_).count();
    if (elapsedSec >= 60) {
        int mins = elapsedSec / 60;
        int secs = elapsedSec % 60;
        ss << " \xc2\xb7 " << mins << "m" << secs << "s";
    }

    // Working directory
    ss << " \xc2\xb7 " << workDir_;

    // Streaming indicator
    if (streamingProvider_ && streamingProvider_()) {
        ss << " \xc2\xb7 " << AnsiStyle::BRIGHT_CYAN << "\xe2\x97\x8c" << AnsiStyle::RESET
           << AnsiStyle::Semantic::STATUS_DIM << " streaming";
    }

    ss << AnsiStyle::RESET;

    return ss.str();
}

String StatusLine::loadStatusLineCommand() const {
    auto home = std::getenv("HOME");
    if (!home) return "";
    auto settingsPath = std::filesystem::path(home) / ".claude" / "settings.json";
    std::ifstream ifs(settingsPath);
    if (!ifs) return "";
    try {
        auto settings = Json::parse(ifs);
        return settings.value("statusLineCommand", "");
    } catch (...) {
        return "";
    }
}

String StatusLine::executeStatusLineCommand() {
    String cmd = loadStatusLineCommand();
    if (cmd.empty()) return "";

    // Inject environment variables
    setenv("CLAUDE_MODEL", modelName_.c_str(), 1);
    setenv("CLAUDE_COST", costStr_.c_str(), 1);
    setenv("CLAUDE_CONTEXT_PCT", contextPctStr_.c_str(), 1);
    setenv("CLAUDE_SESSION", sessionId_.c_str(), 1);
    setenv("CLAUDE_WORKSPACE", workspace_.c_str(), 1);
    setenv("CLAUDE_VERSION", "0.1.0", 1);

    // Execute with popen
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[256];
    String output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    pclose(pipe);

    // Remove trailing newline
    while (!output.empty() && output.back() == '\n') output.pop_back();
    return output;
}

void StatusLine::clearStatusLine() {
    int height = getTerminalHeight();
    out_ << AnsiStyle::SAVE_CURSOR;
    out_ << AnsiStyle::moveCursor(height, 1);
    out_ << AnsiStyle::CLEAR_LINE;
    out_ << AnsiStyle::RESTORE_CURSOR;
    out_ << std::flush;
}

String StatusLine::abbreviatePath(const String& path) {
    const char* home = std::getenv("HOME");
    if (home && path.starts_with(home)) {
        return "~" + path.substr(strlen(home));
    }
    return path;
}

// ========== Reactive Auto-Refresh ==========

void StatusLine::startAutoRefresh() {
    if (autoRefreshRunning_.exchange(true)) return; // already running
    refreshThread_ = std::thread(&StatusLine::refreshLoop, this);
}

void StatusLine::stopAutoRefresh() {
    if (!autoRefreshRunning_.exchange(false)) return;
    if (refreshThread_.joinable()) {
        refreshThread_.join();
    }
}

void StatusLine::refreshLoop() {
    while (autoRefreshRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(refreshIntervalMs_));
        if (!autoRefreshRunning_.load()) break;
        if (enabled_ && tokenTracker_) {
            refresh();
        }
    }
}

} // namespace claude
