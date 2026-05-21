#pragma once

#include "../Types.hpp"
#include <chrono>
#include <mutex>
#include <spdlog/spdlog.h>

namespace claude {

// Forward declarations
namespace memory { class MarkdownMemoryService; }
class ApiClient;

/// 自动梦境 / 记忆巩固 —— 在空闲时间整理和巩固记忆
///
/// 匹配原版 TS 的 auto-dream 功能：
/// - 在对话空闲时触发记忆巩固
/// - 合并重复/相似的记忆条目
/// - 提取隐含的用户偏好
/// - 更新记忆的关联性和时效性
class AutoDream {
public:
    AutoDream() = default;

    // ========== 配置 ==========

    /// 设置空闲阈值 (默认 5 分钟)
    void setIdleThreshold(std::chrono::seconds threshold) {
        idleThreshold_ = threshold;
    }

    /// 启用/禁用自动梦境
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /// 设置记忆服务
    void setMemoryService(memory::MarkdownMemoryService* service) { memoryService_ = service; }

    // ========== 触发 ==========

    /// 检查是否应该触发记忆巩固
    /// @param lastActivityTime 最后活动时间
    /// @return true 如果空闲时间超过阈值且有待巩固的记忆
    bool shouldDream(std::chrono::steady_clock::time_point lastActivityTime) const;

    /// 执行记忆巩固
    /// @param recentMessages 最近的消息 (用于提取隐含偏好)
    /// @return 巩固了的事项数量
    int dream(const std::vector<Message>& recentMessages);

    // ========== 巩固策略 ==========

    /// 合并重复/相似的记忆条目
    int mergeSimilarMemories();

    /// 过期项目记忆清理 (超过 30 天)
    int clearExpiredProjectMemories();

    /// 从最近的对话中提取隐含的用户偏好
    int extractImplicitPreferences(const std::vector<Message>& recentMessages);

    // ========== LLM-assisted consolidation ==========

    /// Merge semantically similar memories using LLM analysis.
    /// The LLM reviews pairs of memories and decides if they should be merged,
    /// then produces a merged description.
    /// @return Number of memories merged
    int mergeWithLLM(ApiClient& apiClient);

    /// Extract implicit preferences from recent conversation using LLM.
    /// The LLM reviews messages for patterns suggesting user preferences,
    /// project facts, and corrections — producing structured memory entries.
    /// @return Number of memories extracted
    int extractPreferencesWithLLM(
        const std::vector<Message>& recentMessages,
        ApiClient& apiClient);

    /// Full LLM-assisted dream: merge + extract in one call.
    int dreamWithLLM(const std::vector<Message>& recentMessages, ApiClient& apiClient);

    // ========== 统计 ==========

    int totalDreamsExecuted() const { return totalDreams_; }
    int totalMemoriesConsolidated() const { return totalConsolidated_; }

private:
    bool enabled_ = false;
    std::chrono::seconds idleThreshold_ = std::chrono::minutes(5);
    memory::MarkdownMemoryService* memoryService_ = nullptr;

    int totalDreams_ = 0;
    int totalConsolidated_ = 0;
    std::chrono::steady_clock::time_point lastDreamTime_;

    mutable std::mutex mutex_;
};

} // namespace claude
