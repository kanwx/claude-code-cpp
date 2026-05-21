#pragma once

#include "Types.hpp"
#include "TokenTracker.hpp"
#include "HookManager.hpp"
#include "StreamingToolExecutor.hpp"
#include "compact/CompactService.hpp"
#include "compact/AutoCompact.hpp"
#include "compact/CompactWarningHook.hpp"
#include "../tool/ToolRegistry.hpp"
#include "../tool/ToolContext.hpp"
#include "../permission/RuleEngine.hpp"
#include "../api/ApiClient.hpp"
#include "../mcp/McpClient.hpp"
#include "../ui/MessagePipeline.hpp"
#include <functional>
#include <vector>
#include <memory>
#include <optional>
#include <expected>
#include <atomic>

namespace claude {

/// Agent 循环 —— 借鉴 Java AgentLoop 设计
///
/// 核心对话管理，支持两种模式：
/// - run() —— 阻塞模式，等待完整响应
/// - runStreaming() —— 流式模式，逐 token 实时输出
///
/// 流式输出采用 Think-Act-Observe-Repeat (TAOR) 循环：
/// - Think: 模型流式输出文本 + 思考
/// - Act: 执行工具调用（只读工具并发）
/// - Observe: 工具结果实时回传
/// - Repeat: 将结果追加到历史，继续循环
class AgentLoop {
public:
    /// 单轮最大迭代次数，防止无限循环
    static constexpr int DEFAULT_MAX_ITERATIONS = 50;

    /// max_output_tokens 恢复最大重试次数
    static constexpr int MAX_OUTPUT_TOKENS_RECOVERY = 3;

    /// Escalated max_tokens for recovery (when default is too small)
    static constexpr int ESCALATED_MAX_TOKENS = 65536;

    /// Maximum reactive compact attempts on 413 errors
    static constexpr int MAX_REACTIVE_COMPACT_ATTEMPTS = 2;

    /// Result from a stop hook evaluation.
    /// If shouldContinue is true, the loop continues even though the model said end_turn.
    struct StopHookResult {
        bool shouldContinue = false;
        String reason;
    };

    /// Stop hook callback type.
    /// Called when the model stops with end_turn (not max_tokens).
    using OnStopHook = std::function<StopHookResult()>;

    // ========== 构造 ==========

    AgentLoop(
        ApiClient& apiClient,
        ToolRegistry& tools,
        const String& systemPrompt
    );

    AgentLoop(
        ApiClient& apiClient,
        ToolRegistry& tools,
        const String& systemPrompt,
        TokenTracker& tokenTracker
    );

    ~AgentLoop() = default;

    // ========== 阻塞模式 ==========

    /// 阻塞执行一轮用户输入的完整 Agent 循环
    /// 等待完整响应后才返回
    std::expected<String, String> run(const String& userInput);

    // ========== 流式模式 ==========

    /// 流式执行一轮用户输入的完整 Agent 循环
    /// 文本逐 token 通过 onToken 回调实时输出
    std::expected<String, String> runStreaming(
        const String& userInput,
        OnToken onToken
    );

    // ========== 回调注册 (借鉴 Java 函数式设计) ==========

    /// 设置工具事件回调
    void setOnToolEvent(OnToolEvent callback) {
        onToolEvent_ = std::move(callback);
    }

    /// 设置权限确认回调
    void setOnPermissionRequest(
        std::function<PermissionChoice(const PermissionRequest&)> callback
    ) {
        onPermissionRequest_ = std::move(callback);
    }

    /// 设置流式开始回调
    void setOnStreamStart(OnStreamStart callback) {
        onStreamStart_ = std::move(callback);
    }

    /// 设置思考内容回调
    void setOnThinking(OnThinking callback) {
        onThinking_ = std::move(callback);
    }

    /// 设置助手消息回调
    void setOnAssistantMessage(std::function<void(const String&)> callback) {
        onAssistantMessage_ = std::move(callback);
    }

    /// 设置内容块完成回调 — 每个 content_block_stop 时触发
    /// 这是流畅输出的关键：每个文本块/工具块完成时立即通知UI，
    /// 而不是等待整个 response 完成
    void setOnContentBlockStop(std::function<void(const String& type, int index, const String& content)> callback) {
        onContentBlockStop_ = std::move(callback);
    }

    /// 设置工具结果流式回调 — 每个工具完成时立即触发
    /// 允许UI在工具执行过程中逐步显示结果，而非等待所有工具完成
    void setOnToolResult(std::function<void(const String& toolName, const String& result, bool isError)> callback) {
        onToolResult_ = std::move(callback);
    }

    /// 初始化 AutoCompact (需要 ApiClient 和上下文窗口大小)
    void initAutoCompact(int contextWindow) {
        autoCompact_.emplace(apiClient_, contextWindow);
    }

    /// 设置循环继续回调 — 当TAOR循环继续下一轮时触发
    /// 让UI知道模型正在继续思考/行动
    void setOnLoopContinue(std::function<void(int iteration, int totalIterations)> callback) {
        onLoopContinue_ = std::move(callback);
    }

    /// 设置统一流事件回调 — 替代5个独立回调的新接口
    /// 当设置后，AgentLoop 通过此回调发送 StreamEvent 事件
    /// 向后兼容：现有独立回调仍然生效（作为回退）
    void setOnStreamEvent(std::function<void(const StreamEvent&)> callback) {
        onStreamEvent_ = std::move(callback);
    }

    /// Set the stop hook callback.
    /// When the model stops (end_turn), this hook runs and can force continuation.
    void setOnStopHook(OnStopHook callback) {
        onStopHook_ = std::move(callback);
    }

    /// 设置上下文压缩预警回调 — 当 token 使用量接近上下文窗口上限时触发
    /// level 1: 80% (warning), level 2: 93% (critical)
    void setOnCompactWarning(std::function<void(int level, long currentTokens, long maxTokens)> callback) {
        compactWarningHook_.setCallback(std::move(callback));
    }

    // ========== Per-agent overrides ==========

    /// Set max iterations (overrides default 50). Used by sub-agents.
    void setMaxIterations(int maxIter) { maxIterations_ = maxIter; }
    int getMaxIterations() const { return maxIterations_; }

    /// Set temperature for API calls (-1 = use API default). Used by sub-agents.
    void setTemperature(double temp) { temperature_ = temp; }
    double getTemperature() const { return temperature_; }

    /// Set max_tokens override for API calls (-1 = use ApiClient default). Used by sub-agents.
    void setMaxTokensOverride(int maxTokens) { maxTokensOverride_ = maxTokens; }
    int getMaxTokensOverride() const { return maxTokensOverride_; }

    // ========== 权限引擎 ==========

    void setPermissionEngine(RuleEngine* engine) {
        permissionEngine_ = engine;
        if (engine) {
            toolContext_.set("permissionEngine", engine);
        }
    }

    RuleEngine* getPermissionEngine() const {
        return permissionEngine_;
    }

    // ========== 认知后端 ==========

    /// 设置认知后端 MCP 客户端并注册认知工具
    void setCognitiveBackend(std::shared_ptr<McpClient> mcpClient) {
        cognitiveMcpClient_ = std::move(mcpClient);
        // 自动注册认知工具
        tools_.registerCognitiveTools(cognitiveMcpClient_);
    }

    /// 获取认知后端客户端
    std::shared_ptr<McpClient> getCognitiveBackend() const {
        return cognitiveMcpClient_;
    }

    /// 检查是否启用认知后端
    bool hasCognitiveBackend() const {
        return cognitiveMcpClient_ != nullptr;
    }

    // ========== 中断 ==========

    /// Cancel the running agent loop and any in-flight API stream.
    /// Thread-safe: may be called from the UI thread or signal handler.
    void cancel();

    /// Check if cancel was requested
    bool isCancelled() const { return cancelled_.load(std::memory_order_acquire); }

    /// Reset cancellation state for a new turn
    void resetCancel();

    // ========== 状态访问 ==========

    const std::vector<Message>& getMessageHistory() const {
        return messageHistory_;
    }

    TokenTracker& getTokenTracker() {
        return tokenTracker_;
    }

    const TokenTracker& getTokenTracker() const {
        return tokenTracker_;
    }

    const String& getSystemPrompt() const {
        return systemPrompt_;
    }

    ApiClient& getApiClient() {
        return apiClient_;
    }

    ToolContext& getToolContext() {
        return toolContext_;
    }

    HookManager& getHookManager() {
        return hookManager_;
    }

    // ========== 历史管理 ==========

    /// 重置历史 (保留系统提示词)
    void reset();

    /// 替换消息历史 (用于上下文压缩后替换)
    void replaceHistory(std::vector<Message> newHistory);

    /// 获取消息数量
    size_t getMessageCount() const {
        return messageHistory_.size();
    }

private:
    // ========== 核心循环 ==========

    /// 统一执行循环 (TAOR: Think-Act-Observe-Repeat)
    std::expected<String, String> executeLoop(
        bool streaming,
        OnToken onToken
    );

    /// 阻塞迭代
    struct IterationResult {
        Message message;
        Usage usage;
        String stopReason;  // "end_turn", "max_tokens", "tool_use", etc.
    };
    IterationResult blockingIteration(const Json& prompt);

    /// 流式迭代
    IterationResult streamingIteration(const Json& prompt, OnToken onToken);

    // ========== 工具执行 ==========

    /// 执行工具调用列表（只读工具并发执行）
    std::vector<ToolResponse> executeToolCalls(const std::vector<ToolCall>& calls);

    /// 执行单个工具
    String executeTool(const ToolCall& call);

    /// 检查工具是否为只读（可并发执行）
    bool isToolReadOnly(const String& toolName) const;

    /// 微压缩：清除过期的工具结果内容
    /// 匹配原版 TS 的 microcompact 行为：
    /// - 工具结果超过 60 分钟且不紧邻当前用户消息 → 替换为 [Old tool result content cleared]
    /// - 保留最后 N 个工具结果不压缩
    void applyMicrocompact();

    /// 自动压缩：当 token 使用量超过 93% 上下文窗口时
    /// 使用 LLM 摘要对话历史，保留最近 N 条消息
    /// 匹配原版 TS 的 auto-compact 行为
    bool applyAutoCompact();

    /// Reactive compact: attempt compact on 413 (prompt too long) errors.
    /// Returns true if compact succeeded and the caller should retry.
    /// @param tokenGap How many tokens over the limit (from 413 error body)
    bool attemptReactiveCompact(long tokenGap = 0);

    /// Generate synthetic error tool_results for any tool_use blocks
    /// in the last assistant message that lack matching tool_results.
    void addMissingToolResults();

    // ========== 辅助方法 ==========

    /// 构建 API 请求
    Json buildApiRequest();

    /// 提取思考内容
    void extractThinkingContent(const Json& response);

    /// 通知工具事件
    void notifyToolEvent(ToolEventPhase phase, const String& name,
                         const String& args, const String& result = {});

private:
    // 核心依赖
    ApiClient& apiClient_;
    ToolRegistry& tools_;
    String systemPrompt_;
    TokenTracker tokenTracker_;
    ToolContext toolContext_;
    HookManager hookManager_;
    compact::CompactService compactService_;
    std::optional<compact::AutoCompact> autoCompact_;
    compact::CompactWarningHook compactWarningHook_;

    // 认知后端 (可选)
    std::shared_ptr<McpClient> cognitiveMcpClient_;

    // 权限引擎 (可选)
    RuleEngine* permissionEngine_ = nullptr;

    // Streaming tool executor (lazy-initialized)
    std::optional<StreamingToolExecutor> toolExecutor_;

    // 消息历史
    std::vector<Message> messageHistory_;

    // 回调
    OnToolEvent onToolEvent_;
    std::function<PermissionChoice(const PermissionRequest&)> onPermissionRequest_;
    OnStreamStart onStreamStart_;
    OnThinking onThinking_;
    std::function<void(const String&)> onAssistantMessage_;

    // 新增回调：流畅输出的关键
    std::function<void(const String& type, int index, const String& content)> onContentBlockStop_;   // content_block_stop
    std::function<void(const String& toolName, const String& result, bool isError)> onToolResult_;  // 单工具完成
    std::function<void(int iteration, int totalIterations)> onLoopContinue_;  // TAOR循环继续
    std::function<void()> onCancelled_;  // Cancelled callback for UI notification
    OnStopHook onStopHook_;

    // 统一事件回调 (优先于独立回调)
    std::function<void(const StreamEvent&)> onStreamEvent_;

    /// 发射流事件 — 如果 onStreamEvent_ 已设置则使用它，否则回退到独立回调
    void emitStreamEvent(StreamEvent event);

    // Cancellation
    std::atomic<bool> cancelled_{false};

    // Per-agent overrides
    int maxIterations_ = DEFAULT_MAX_ITERATIONS;
    double temperature_ = -1;      // -1 = use API default
    int maxTokensOverride_ = -1;   // -1 = use ApiClient default

    // Reactive compact state
    int reactiveCompactAttempts_ = 0;

    // 当前用户输入 (用于回调)
    String currentUserInput_;
};

} // namespace claude
