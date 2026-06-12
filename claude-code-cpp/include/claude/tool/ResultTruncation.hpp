#pragma once

#include "../core/Types.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <spdlog/spdlog.h>

namespace claude {

/// 工具结果截断策略
///
/// 当工具结果超过 per-tool 或 per-message 预算时：
/// 1. 保留头部 + 尾部，中间截断
/// 2. 完整结果持久化到磁盘临时文件
/// 3. 截断位置插入磁盘引用路径
class ResultTruncation {
public:
    /// 每条消息的聚合字符预算 (100K — 防止大量工具结果迅速填满上下文)
    static constexpr size_t AGGREGATE_BUDGET = 100000;

    /// 截断时保留的头部字符数 (前10K字保留结构/开头信息)
    static constexpr size_t HEAD_CHARS = 10000;

    /// 截断时保留的尾部字符数 (后3K字保留结尾上下文)
    static constexpr size_t TAIL_CHARS = 3000;

    /// 磁盘持久化目录
    static String persistDir() {
        return "/tmp/claude-results";
    }

    /// 单个结果截断
    /// @param result 原始结果
    /// @param maxSize 最大字符数 (来自 Tool::maxResultSizeChars())
    /// @param toolName 工具名 (用于文件命名)
    /// @return 截断后的结果 (含磁盘引用)
    static String truncate(const String& result, size_t maxSize, const String& toolName) {
        if (result.size() <= maxSize) {
            return result;
        }

        // 持久化完整结果到磁盘
        String diskPath = persistToDisk(result, toolName);
        if (diskPath.empty()) {
            // 持久化失败，硬截断
            return result.substr(0, maxSize) +
                "\n\n[...truncated - full output could not be saved to disk...]";
        }

        // 构建截断结果：head + reference + tail
        size_t budget = maxSize;
        String truncated;

        // Head
        size_t headLen = std::min(HEAD_CHARS, result.size());
        truncated += result.substr(0, headLen);
        budget -= headLen;

        // Reference + tail estimate
        String ref = "\n\n[...truncated " +
            std::to_string(result.size() - headLen - std::min(TAIL_CHARS, result.size() - headLen)) +
            " characters... See " + diskPath + " for full output]\n\n";
        size_t refLen = ref.size();

        if (refLen + TAIL_CHARS <= budget) {
            // Both reference and tail fit
            size_t tailStart = result.size() - std::min(TAIL_CHARS, result.size() - headLen);
            if (tailStart > headLen) {
                truncated += ref;
                truncated += result.substr(tailStart);
            } else {
                // Result is small enough after head
                truncated += ref;
            }
        } else {
            // Only reference fits
            truncated += ref;
        }

        return truncated;
    }

    /// 按聚合预算截断多个工具结果 (最老的先截断)
    /// @param results 工具结果列表 (callId, toolName, content, isError)
    /// @param budget 聚合预算 (默认 200K)
    /// @return 截断后的结果列表
    struct ToolResultEntry {
        String callId;
        String toolName;
        String content;
        bool isError;
    };

    static std::vector<ToolResultEntry> truncateByAggregateBudget(
        std::vector<ToolResultEntry> results,
        size_t budget = AGGREGATE_BUDGET
    ) {
        // 计算总大小
        size_t totalSize = 0;
        for (const auto& r : results) {
            totalSize += r.content.size();
        }

        if (totalSize <= budget) {
            return results;
        }

        // 从最老的结果开始截断
        for (size_t i = 0; i < results.size() && totalSize > budget; ++i) {
            auto& r = results[i];
            if (r.content.size() <= HEAD_CHARS + TAIL_CHARS + 200) {
                // 已经很小，跳过
                continue;
            }

            size_t oldSize = r.content.size();
            r.content = truncate(r.content, HEAD_CHARS + TAIL_CHARS + 200, r.toolName);
            totalSize -= (oldSize - r.content.size());
        }

        return results;
    }

private:
    /// 将完整结果持久化到磁盘
    static String persistToDisk(const String& content, const String& toolName) {
        try {
            // 确保目录存在
            std::filesystem::create_directories(persistDir());

            // 生成文件名 (工具名 + 内容hash前8位)
            size_t hash = std::hash<String>{}(content);
            std::ostringstream oss;
            oss << persistDir() << "/claude-result-"
                << toolName << "-"
                << std::hex << std::setw(8) << std::setfill('0') << (hash & 0xFFFFFFFF)
                << ".txt";
            String path = oss.str();

            // 写入文件
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                spdlog::warn("Failed to persist tool result to {}", path);
                return "";
            }
            out.write(content.data(), content.size());
            out.close();

            spdlog::debug("Persisted tool result ({} chars) to {}", content.size(), path);
            return path;
        } catch (const std::exception& e) {
            spdlog::warn("Failed to persist tool result: {}", e.what());
            return "";
        }
    }
};

} // namespace claude
