#pragma once

#include "../core/Types.hpp"
#include "../core/TokenTracker.hpp"

#include <string>
#include <ostream>
#include <chrono>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdio>

namespace claude {

/// 底部状态行渲染器 —— 匹配原版 TS 设计
/// 格式: claude-sonnet-4-6 · 45% ctx · 3.2K in/840 out · $0.0312 · ~/project
/// - 上下文使用率颜色编码 (绿/黄/红)
/// - Token breakdown (input/output)
/// - 费用 $0.0000 格式 (4 decimals)
/// · 分隔符
/// - 正确的终端底部定位 (ioctl TIOCGWINSZ)
/// - Reactive auto-refresh via background thread
class StatusLine {
public:
    explicit StatusLine(std::ostream& out);
    ~StatusLine();

    // ========== 控制 ==========

    void enable(const String& model, TokenTracker& tracker);
    void disable();

    // ========== 渲染 ==========

    /// 刷新底部状态行 (使用光标控制)
    void refresh();

    /// 渲染内联状态 (不使用光标控制)
    String renderInline();

    /// Get current context usage percentage
    int getContextPercentage() const;

    /// Set a callback to check if streaming is active (for status indicator)
    void setStreamingProvider(std::function<bool()> provider) {
        streamingProvider_ = std::move(provider);
    }

    /// Set refresh interval in milliseconds (default 2000)
    void setRefreshInterval(int ms) { refreshIntervalMs_ = ms; }

    /// Set state values used as environment variables in status line command
    void setModelName(const String& name) { modelName_ = name; }
    void setCostStr(const String& cost) { costStr_ = cost; }
    void setContextPctStr(const String& pct) { contextPctStr_ = pct; }
    void setSessionId(const String& id) { sessionId_ = id; }
    void setWorkspace(const String& ws) { workspace_ = ws; }

private:
    String buildStatusText();
    void clearStatusLine();
    String abbreviatePath(const String& path);
    int getTerminalHeight() const;
    int getTerminalWidth() const;

    /// Background refresh thread
    void refreshLoop();
    void startAutoRefresh();
    void stopAutoRefresh();

    /// Format token count with K/M suffix
    static String formatTokenCount(long tokens);

    /// Load statusLineCommand from ~/.claude/settings.json
    String loadStatusLineCommand() const;

    /// Execute the status line command with injected env vars, return output
    String executeStatusLineCommand();

    std::ostream& out_;
    bool enabled_ = false;
    String modelName_;
    TokenTracker* tokenTracker_ = nullptr;
    String workDir_;
    std::chrono::steady_clock::time_point sessionStartTime_;

    // Reactive refresh
    std::atomic<bool> autoRefreshRunning_{false};
    std::thread refreshThread_;
    int refreshIntervalMs_ = 2000;
    std::function<bool()> streamingProvider_;

    // Configurable status line command
    String costStr_;
    String contextPctStr_;
    String sessionId_;
    String workspace_;
    String cachedCommandOutput_;
    std::chrono::steady_clock::time_point lastCommandTime_ =
        std::chrono::steady_clock::now() - std::chrono::seconds(10);
};

} // namespace claude
