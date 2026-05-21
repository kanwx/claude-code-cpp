#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace claude {

// ========== 基础类型 ==========

using Json = nlohmann::json;
using String = std::string;

// ========== Provider 类型 ==========

/// API Provider 枚举 (匹配原版 TS)
enum class APIProvider {
    FirstParty,  // Anthropic 官方
    Bedrock,     // AWS Bedrock
    Vertex,      // GCP Vertex AI
    Foundry      // Azure Foundry
};

/// Provider 转字符串
inline String providerToString(APIProvider p) {
    switch (p) {
        case APIProvider::FirstParty: return "firstParty";
        case APIProvider::Bedrock: return "bedrock";
        case APIProvider::Vertex: return "vertex";
        case APIProvider::Foundry: return "foundry";
    }
    return "firstParty";
}

/// Provider 名称用于 API 客户端
inline String providerToClientName(APIProvider p) {
    switch (p) {
        case APIProvider::FirstParty: return "anthropic";
        case APIProvider::Bedrock: return "bedrock";
        case APIProvider::Vertex: return "vertex";
        case APIProvider::Foundry: return "foundry";
    }
    return "anthropic";
}

// ========== Cache 类型 ==========

/// 缓存作用域
enum class CacheScope {
    Global,  // 全局缓存 (仅 first-party)
    Org,     // 组织级缓存
    None     // 不缓存
};

/// 缓存控制
struct CacheControl {
    String type = "ephemeral";
    std::optional<String> ttl;        // "5m" or "1h"
    std::optional<CacheScope> scope;  // 缓存作用域

    Json toJson() const {
        Json j = {{"type", type}};
        if (ttl) j["ttl"] = *ttl;
        if (scope && *scope == CacheScope::Global) {
            j["scope"] = "global";
        }
        return j;
    }
};

/// 文本块参数 (用于 system prompt)
struct TextBlockParam {
    String type = "text";
    String text;
    std::optional<CacheControl> cache_control;

    Json toJson() const {
        Json j = {{"type", type}, {"text", text}};
        if (cache_control) {
            j["cache_control"] = cache_control->toJson();
        }
        return j;
    }
};

// ========== 消息类型 ==========

/// 工具调用请求
struct ToolCall {
    String id;           // 调用ID
    String name;         // 工具名称
    String arguments;    // JSON参数字符串
};

/// 工具响应
struct ToolResponse {
    String callId;       // 对应的调用ID
    String toolName;     // 工具名称
    String content;      // 响应内容
    bool isError = false;
};

/// 消息角色
enum class MessageRole {
    System,
    User,
    Assistant,
    ToolResult
};

/// 消息
struct Message {
    MessageRole role;
    String content;
    std::vector<ToolCall> toolCalls;
    std::vector<ToolResponse> toolResults;

    // Extended Thinking (Anthropic)
    std::optional<String> thinking;    // 思考内容
    std::optional<String> signature;   // 签名 (用于验证)

    // 元数据
    std::map<String, String> metadata;

    // 时间戳 (用于 microcompact 时间判断)
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

    // API round this message belongs to (0-based, set by AgentLoop)
    int apiRound = 0;

    // 辅助方法
    static Message system(String content) {
        return {MessageRole::System, std::move(content)};
    }

    static Message user(String content) {
        return {MessageRole::User, std::move(content)};
    }

    static Message assistant(String content, std::vector<ToolCall> calls = {}) {
        return {MessageRole::Assistant, std::move(content), std::move(calls)};
    }

    static Message toolResult(std::vector<ToolResponse> results) {
        return {MessageRole::ToolResult, {}, {}, std::move(results)};
    }

    bool hasToolCalls() const { return !toolCalls.empty(); }
    bool isToolResult() const { return role == MessageRole::ToolResult; }
    bool hasThinking() const { return thinking.has_value() && !thinking->empty(); }
};

// ========== API 响应 ==========

/// 使用量统计
struct Usage {
    long promptTokens = 0;
    long completionTokens = 0;
    long totalTokens = 0;

    // Cache tokens (Anthropic)
    long cacheCreationTokens = 0;  // 创建缓存的 token
    long cacheReadTokens = 0;      // 从缓存读取的 token
    long cacheDeletedInputTokens = 0;  // 缓存删除的 input token

    // Cache breakdown (from cache_creation breakdown in streaming)
    long cacheEphemeral5m = 0;     // 5分钟缓存
    long cacheEphemeral1h = 0;     // 1小时缓存

    // Stream idle watchdog
    int stallCount = 0;            // Number of stall detections
    double totalStallTimeMs = 0.0; // Total stall duration in ms

    Usage& operator+=(const Usage& other) {
        promptTokens += other.promptTokens;
        completionTokens += other.completionTokens;
        totalTokens += other.totalTokens;
        cacheCreationTokens += other.cacheCreationTokens;
        cacheReadTokens += other.cacheReadTokens;
        cacheDeletedInputTokens += other.cacheDeletedInputTokens;
        cacheEphemeral5m += other.cacheEphemeral5m;
        cacheEphemeral1h += other.cacheEphemeral1h;
        stallCount += other.stallCount;
        totalStallTimeMs += other.totalStallTimeMs;
        return *this;
    }

    // 有效输入 token (扣除缓存)
    long effectiveInputTokens() const {
        return promptTokens - cacheReadTokens;
    }
};

/// 流式块类型
enum class StreamChunkType {
    MessageStart,
    ContentBlockStart,
    ContentBlockDelta,
    ContentBlockStop,
    MessageDelta,
    MessageStop,
    Error
};

/// 流式响应块
struct StreamChunk {
    StreamChunkType type;
    String content;      // 文本内容
    ToolCall toolCall;   // 工具调用 (如果有)
    Usage usage;         // 使用量 (通常在最后)
    String error;        // 错误信息
};

// ========== 工具事件 ==========

/// 工具事件阶段
enum class ToolEventPhase {
    Start,  // 工具开始执行
    End     // 工具执行结束
};

/// 工具事件
struct ToolEvent {
    ToolEventPhase phase;
    String toolName;
    String arguments;
    String result;
    std::optional<String> error;
};

// ========== 模型配置 ==========

/// 模型设置
struct ModelConfig {
    String provider = "openai";       // openai | anthropic
    String model = "gpt-4o";          // 模型名称
    String baseUrl;                   // API地址
    String apiKey;                    // API密钥
    int maxTokens = 4096;             // 最大输出token
    double temperature = 1.0;         // 温度

    // 默认配置
    static ModelConfig openai(String apiKey) {
        return {
            .provider = "openai",
            .model = "gpt-4o",
            .baseUrl = "https://api.openai.com/v1",
            .apiKey = std::move(apiKey)
        };
    }

    static ModelConfig anthropic(String apiKey) {
        return {
            .provider = "anthropic",
            .model = "claude-sonnet-4-20250514",
            .baseUrl = "https://api.anthropic.com/v1",
            .apiKey = std::move(apiKey),
            .maxTokens = 8192
        };
    }
};

// ========== 成本计算 ==========

/// 价格表 (每百万token)
struct Pricing {
    double inputPrice = 0.0;   // 输入价格
    double outputPrice = 0.0;  // 输出价格

    double calculateCost(long inputTokens, long outputTokens) const {
        return (inputTokens * inputPrice + outputTokens * outputPrice) / 1'000'000.0;
    }
};

// 常用模型价格
inline const std::map<String, Pricing> MODEL_PRICING = {
    {"gpt-4o", {2.50, 10.00}},
    {"gpt-4o-mini", {0.15, 0.60}},
    {"claude-sonnet-4-20250514", {3.00, 15.00}},
    {"claude-opus-4-20250514", {15.00, 75.00}},
};

// ========== 回调类型 ==========

/// 文本回调 (流式输出)
using OnToken = std::function<void(const String& token)>;

/// 工具事件回调
using OnToolEvent = std::function<void(const ToolEvent& event)>;

/// 流式开始回调
using OnStreamStart = std::function<void()>;

/// 思考内容回调
using OnThinking = std::function<void(const String& thinking)>;

// ========== 错误处理 ==========

/// 结果类型 (简化版 expected)
template<typename T>
class Result {
public:
    struct ErrorTag {};
    Result(T value) : value_(std::move(value)), ok_(true) {}
    Result(ErrorTag, String error) : error_(std::move(error)), ok_(false) {}

    static Result<T> success(T value) { return Result(std::move(value)); }
    static Result<T> err(String error) { return Result(ErrorTag{}, std::move(error)); }

    bool ok() const { return ok_; }
    bool isErr() const { return !ok_; }

    const T& value() const { return value_.value(); }
    const String& error() const { return error_; }

    T& value() { return value_.value(); }

    // 转换操作符
    operator bool() const { return ok_; }

private:
    std::optional<T> value_;
    String error_;
    bool ok_;
};

// 特化 String 结果 (避免 T=String 时构造函数歧义)
template<>
class Result<String> {
public:
    struct ErrorTag {};
    Result(String value) : value_(std::move(value)), ok_(true) {}
    Result(ErrorTag, String error) : error_(std::move(error)), ok_(false) {}

    static Result<String> success(String value) { return Result(std::move(value)); }
    static Result<String> err(String error) { return Result(ErrorTag{}, std::move(error)); }

    bool ok() const { return ok_; }
    bool isErr() const { return !ok_; }

    const String& value() const { return value_; }
    const String& error() const { return error_; }

    String& value() { return value_; }

    operator bool() const { return ok_; }

private:
    String value_;
    String error_;
    bool ok_;
};

// 特化 void 结果
template<>
class Result<void> {
public:
    struct ErrorTag {};
    Result() : ok_(true) {}
    Result(ErrorTag, String error) : error_(std::move(error)), ok_(false) {}

    static Result<void> success() { return Result(); }
    static Result<void> err(String error) { return Result(ErrorTag{}, std::move(error)); }

    bool ok() const { return ok_; }
    bool isErr() const { return !ok_; }
    const String& error() const { return error_; }

    operator bool() const { return ok_; }

private:
    String error_;
    bool ok_;
};

} // namespace claude
