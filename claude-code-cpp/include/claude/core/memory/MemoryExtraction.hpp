#pragma once

#include "../Types.hpp"
#include <vector>

namespace claude {

class ApiClient;

namespace memory { class MarkdownMemoryService; }

/// 从对话中提取的记忆条目
struct ExtractedMemory {
    enum Type { User, Feedback, Project, Reference };
    Type type;
    String name;          // 简短标识名
    String description;   // 一行描述
    String content;       // 完整内容
    double confidence;    // 置信度 0-1
};

/// 记忆提取器 —— 在对话过程中自动提取可保存的记忆
///
/// 匹配原版 TS 的 memory extraction:
/// - 检测对话中的用户偏好、反馈、项目信息
/// - 基于模式匹配 + LLM 辅助
/// - 提供确认界面让用户选择保存哪些
class MemoryExtraction {
public:
    /// 提取候选项 (从最近消息中, 模式匹配)
    static std::vector<ExtractedMemory> extractFromMessages(
        const std::vector<Message>& messages,
        int maxRecentMessages = 10
    );

    /// Extract memories using LLM analysis of recent conversation.
    /// Sends recent messages to the LLM with a structured extraction prompt.
    /// Returns proposed memories (higher confidence than pattern matching).
    /// @param messages Recent conversation messages
    /// @param apiClient API client for LLM call
    /// @param maxMessages How many recent messages to include (default 10)
    static std::vector<ExtractedMemory> extractWithLLM(
        const std::vector<Message>& messages,
        ApiClient& apiClient,
        int maxMessages = 10
    );

    /// 检查消息是否包含可提取的记忆
    static bool hasExtractableContent(const Message& message);

    /// 保存提取的记忆到记忆服务
    static int saveExtracted(
        const std::vector<ExtractedMemory>& memories,
        memory::MarkdownMemoryService& service
    );
};

} // namespace claude
