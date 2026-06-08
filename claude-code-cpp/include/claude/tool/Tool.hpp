#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "ToolContext.hpp"
#include "PermissionResult.hpp"
#include "../core/Types.hpp"

namespace claude {

/// 工具接口 —— 借鉴 Java 的极简设计
///
/// 每个工具是一个完整的协议实现，包含：
/// - 工具定义（name、description、inputSchema）
/// - 执行逻辑
/// - 权限检查
/// - 特性门控
/// - 活动描述
class Tool {
public:
    virtual ~Tool() = default;

    // ========== 核心定义 (必须实现) ==========

    /// 工具唯一名称标识
    virtual String name() const = 0;

    /// 给 LLM 看的工具描述
    virtual String description() const = 0;

    /// 输入参数的 JSON Schema 定义 (字符串形式)
    /// 示例: R"({"type": "object", "properties": {...}, "required": [...]})"
    virtual String inputSchema() const = 0;

    /// 执行工具
    /// @param input JSON 解析后的输入参数
    /// @param context 执行上下文
    /// @return 执行结果文本
    virtual String execute(const Json& input, ToolContext& context) = 0;

    /// Chunk callback for streaming execution
    /// Receives partial output as it's produced. Return false to cancel.
    using ChunkCallback = std::function<bool(const String& chunk)>;

    /// Streaming execution — override to produce output incrementally.
    /// Default implementation calls execute() and delivers the full result in one chunk.
    /// @param input JSON 解析后的输入参数
    /// @param context 执行上下文
    /// @param onChunk Called with each output chunk. Return false to cancel.
    /// @return Final result text (may be empty if all output went through onChunk)
    virtual String executeStreaming(const Json& input, ToolContext& context,
                                    ChunkCallback onChunk) {
        String result = execute(input, context);
        if (onChunk) {
            onChunk(result);
        }
        return result;
    }

    /// Whether this tool supports streaming output
    virtual bool supportsStreaming() const { return false; }

    // ========== 可选方法 (安全默认值) ==========

    /// 权限前置检查，在 execute 之前调用
    virtual PermissionResult checkPermission(const Json& input, ToolContext& context) {
        return PermissionResult::allow();
    }

    /// 工具是否启用 (特性门控)
    /// 返回 false 则不注册到工具列表
    virtual bool isEnabled() const {
        return true;
    }

    /// 是否为只读操作
    virtual bool isReadOnly() const {
        return false;
    }

    /// 是否为破坏性操作
    virtual bool isDestructive(const Json& input) const {
        return false;
    }

    /// 是否可以并发执行
    virtual bool isConcurrencySafe(const Json& input) const {
        return false;  // 失败-关闭策略
    }

    /// 工具结果最大字符数 (超过则截断并持久化到磁盘)
    /// 默认 50K; 各工具可 override (GrepTool=30K, BashTool=50K, etc.)
    virtual size_t maxResultSizeChars() const {
        return 50000;
    }

    /// 人类可读的活动描述，用于 UI 显示执行进度
    virtual String activityDescription(const Json& input) const {
        return "Running " + name() + "...";
    }

    /// 用户友好的名称
    virtual String userFacingName() const {
        return name();
    }

    /// 是否应该延迟加载 (deferred tool search feature)
    virtual bool shouldDefer() const {
        return false;
    }

    /// 是否必须总是加载 (不延迟)
    virtual bool alwaysLoad() const {
        return false;
    }

    /// 搜索提示 (3-10 个词的工具能力描述，用于关键词搜索匹配)
    virtual String searchHint() const {
        return "";
    }

    /// Whether this tool invocation is non-blocking (returns quickly, work continues in background)
    /// Used by StreamingToolExecutor to enable parallel execution of background agents
    virtual bool isNonBlocking(const Json& input) const {
        return false;
    }

    // ========== Tool-owned rendering ==========

    /// Render tool use invocation (ANSI terminal output).
    /// Default returns empty string (falls back to ToolStatusRenderer).
    virtual String renderToolUse(const String& args, bool isStreaming) const {
        return "";
    }

    /// Render tool result summary (structured data for UI rendering).
    /// Default returns empty summary (falls back to ToolStatusRenderer).
    virtual ToolResultSummary renderToolResult(const String& result, bool isError,
                                               bool isCancelled, bool isRejected) const {
        return ToolResultSummary{};
    }

    /// Render grouped tool use (multiple same-type tools merged).
    /// Default returns empty string (falls back to default grouped rendering).
    virtual String renderGroupedToolUse(
        const std::vector<ToolUseRenderData>& group) const {
        return "";
    }

    // ========== Tool classification (for collapsing) ==========

    virtual bool isCollapsible() const { return false; }
    virtual bool isSearchTool() const { return false; }
    virtual bool isReadTool() const { return false; }
    virtual bool isListTool() const { return false; }
    virtual bool isMemoryTool() const { return false; }
};

// ========== 工具定义 (用于API调用) ==========

/// 工具定义结构
struct ToolDefinition {
    String name;
    String description;
    Json inputSchema;

    // Caching support (Anthropic)
    std::optional<CacheControl> cache_control;
    bool defer_loading = false;  // For tool search feature
    bool strict = false;         // Structured output mode

    /// 转换为 API 格式
    /// @param provider "anthropic" 或 "openai"
    Json toJson(const String& provider = "anthropic") const {
        if (provider == "openai") {
            // OpenAI 格式
            Json result = {
                {"type", "function"},
                {"function", {
                    {"name", name},
                    {"description", description},
                    {"parameters", inputSchema}
                }}
            };
            if (strict) {
                result["function"]["strict"] = true;
            }
            return result;
        } else {
            // Anthropic 格式
            Json result = {
                {"name", name},
                {"description", description},
                {"input_schema", inputSchema}
            };
            if (cache_control) {
                result["cache_control"] = cache_control->toJson();
            }
            if (defer_loading) {
                result["defer_loading"] = true;
            }
            return result;
        }
    }
};

// ========== 工具指针类型 ==========

using ToolPtr = std::unique_ptr<Tool>;
using ToolSharedPtr = std::shared_ptr<Tool>;

} // namespace claude
