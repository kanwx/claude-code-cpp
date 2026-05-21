#pragma once

#include "Types.hpp"
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>

namespace claude {

/// Token 使用追踪器 —— 借鉴 Java TokenTracker 设计
class TokenTracker {
public:
    // 自动压缩阈值 (93%)
    static constexpr double AUTO_COMPACT_THRESHOLD = 0.93;

    // 上下文窗口大小 (不同模型)
    static constexpr long DEFAULT_CONTEXT_WINDOW = 200000;  // 200K

    TokenTracker() = default;

    explicit TokenTracker(long contextWindow)
        : contextWindow_(contextWindow) {}

    // 允许移动构造 (atomic 不可复制，但可以移动)
    TokenTracker(TokenTracker&& other) noexcept
        : inputTokens_(other.inputTokens_.load())
        , outputTokens_(other.outputTokens_.load())
        , apiCallCount_(other.apiCallCount_.load())
        , contextWindow_(other.contextWindow_)
        , model_(std::move(other.model_))
        , lastUpdateTime_(other.lastUpdateTime_) {}

    TokenTracker& operator=(TokenTracker&& other) noexcept {
        if (this != &other) {
            inputTokens_ = other.inputTokens_.load();
            outputTokens_ = other.outputTokens_.load();
            apiCallCount_ = other.apiCallCount_.load();
            contextWindow_ = other.contextWindow_;
            model_ = std::move(other.model_);
            lastUpdateTime_ = other.lastUpdateTime_;
        }
        return *this;
    }

    // 禁止复制
    TokenTracker(const TokenTracker&) = delete;
    TokenTracker& operator=(const TokenTracker&) = delete;

    // ========== 记录 ==========

    /// 记录使用量
    void recordUsage(long promptTokens, long completionTokens) {
        inputTokens_ += promptTokens;
        outputTokens_ += completionTokens;
        apiCallCount_++;
        lastUpdateTime_ = std::chrono::steady_clock::now();

        spdlog::debug("Token usage: input={}, output={}, total={}",
                     inputTokens_.load(), outputTokens_.load(), getTotalTokens());
    }

    /// 记录一次 API 调用
    void recordApiCall() {
        apiCallCount_++;
    }

    /// Set output tokens (for real-time display during streaming)
    /// This doesn't increment apiCallCount
    void setOutputTokens(long tokens) {
        outputTokens_ = tokens;
        lastUpdateTime_ = std::chrono::steady_clock::now();
    }

    /// Get current output tokens (for real-time display)
    long getCurrentOutputTokens() const { return outputTokens_; }

    // ========== 查询 ==========

    long getInputTokens() const { return inputTokens_; }
    long getOutputTokens() const { return outputTokens_; }
    long getTotalTokens() const { return inputTokens_ + outputTokens_; }
    long getApiCallCount() const { return apiCallCount_; }
    long getContextWindow() const { return contextWindow_; }

    /// 获取使用率 (相对于上下文窗口)
    double getUsagePercentage() const {
        return static_cast<double>(getTotalTokens()) / contextWindow_;
    }

    /// 是否应该自动压缩
    bool shouldAutoCompact() const {
        return getUsagePercentage() >= AUTO_COMPACT_THRESHOLD;
    }

    /// 估算费用 (美元)
    double estimateCost(const String& model = "gpt-4o") const {
        auto it = MODEL_PRICING.find(model);
        if (it != MODEL_PRICING.end()) {
            return it->second.calculateCost(inputTokens_, outputTokens_);
        }
        return 0.0;
    }

    /// 估算费用 (指定价格)
    double estimateCost(double inputPrice, double outputPrice) const {
        return (inputTokens_ * inputPrice + outputTokens_ * outputPrice) / 1'000'000.0;
    }

    // ========== 设置 ==========

    /// Set per-task token budget (0 = unlimited). Resets budget used counter.
    void setTaskBudget(long budget) { taskBudget_ = budget; taskBudgetUsed_ = 0; }
    long getTaskBudget() const { return taskBudget_; }

    /// Get tokens consumed in current task
    long getTaskBudgetUsed() const { return taskBudgetUsed_; }

    /// Check if task budget is exceeded
    bool isTaskBudgetExceeded() const {
        return taskBudget_ > 0 && taskBudgetUsed_ >= taskBudget_;
    }

    /// Record token usage toward task budget
    void recordTaskUsage(long inputTokens, long outputTokens) {
        taskBudgetUsed_ += inputTokens + outputTokens;
    }

    void setContextWindow(long window) {
        contextWindow_ = window;
    }

    void setModel(const String& model) {
        model_ = model;
    }

    // ========== 重置 ==========

    void reset() {
        inputTokens_ = 0;
        outputTokens_ = 0;
        apiCallCount_ = 0;
    }

    /// Adjust token counts after compaction.
    /// After compressing history, input tokens drop but we can't know the exact
    /// new count without re-counting. Set inputTokens to the estimated compacted size.
    void adjustAfterCompaction(long newInputTokens) {
        inputTokens_ = newInputTokens;
        spdlog::info("TokenTracker: adjusted after compaction, input tokens → {}", newInputTokens);
    }

    // ========== 转换 ==========

    /// 获取统计摘要字符串
    String getSummary() const {
        return fmt::format("Tokens: {} in / {} out | Cost: ${:.4f} | API calls: {}",
                          inputTokens_.load(),
                          outputTokens_.load(),
                          estimateCost(model_),
                          apiCallCount_.load());
    }

private:
    std::atomic<long> inputTokens_{0};
    std::atomic<long> outputTokens_{0};
    std::atomic<long> apiCallCount_{0};
    long contextWindow_ = DEFAULT_CONTEXT_WINDOW;
    long taskBudget_ = 0;        // 0 = unlimited
    long taskBudgetUsed_ = 0;
    String model_ = "gpt-4o";
    std::chrono::steady_clock::time_point lastUpdateTime_;
};

} // namespace claude
