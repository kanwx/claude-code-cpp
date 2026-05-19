#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <optional>
#include <spdlog/spdlog.h>

namespace claude {

/// System prompt 块 (带缓存作用域)
struct SystemPromptBlock {
    String text;
    std::optional<CacheScope> cacheScope;
};

/// 动态边界标记 (用于分离静态和动态内容)
inline const String SYSTEM_PROMPT_DYNAMIC_BOUNDARY = "<dynamic_boundary/>";

/// 分割 system prompt 为多个块 (匹配原版 TS splitSysPromptPrefix)
///
/// 行为:
/// 1. MCP tools 存在 (skipGlobalCache=true): 返回 org-level 缓存块
/// 2. 全局缓存模式有边界标记: 返回 4 块 (global + dynamic)
/// 3. 默认模式: 返回 3 块 org-level 缓存
inline std::vector<SystemPromptBlock> splitSysPromptPrefix(
    const String& systemPrompt,
    bool skipGlobalCacheForSystemPrompt = false,
    bool enableGlobalCache = true
) {
    std::vector<SystemPromptBlock> blocks;

    // 查找动态边界
    size_t boundaryPos = systemPrompt.find(SYSTEM_PROMPT_DYNAMIC_BOUNDARY);

    // 模式 1: 跳过全局缓存 (MCP tools 存在)
    if (skipGlobalCacheForSystemPrompt || !enableGlobalCache) {
        // 单块，org-level 缓存
        blocks.push_back({
            systemPrompt,
            CacheScope::Org
        });
        return blocks;
    }

    // 模式 2: 有边界标记，分离静态/动态
    if (boundaryPos != String::npos && enableGlobalCache) {
        String staticPart = systemPrompt.substr(0, boundaryPos);
        String dynamicPart = systemPrompt.substr(boundaryPos + SYSTEM_PROMPT_DYNAMIC_BOUNDARY.length());

        // Attribution header (不缓存)
        // System prompt prefix (不缓存)
        // Static content (global 缓存)
        // Dynamic content (不缓存)

        if (!staticPart.empty()) {
            blocks.push_back({
                staticPart,
                CacheScope::Global  // 静态部分使用全局缓存
            });
        }
        if (!dynamicPart.empty()) {
            blocks.push_back({
                dynamicPart,
                std::nullopt  // 动态部分不缓存
            });
        }
        return blocks;
    }

    // 模式 3: 默认，org-level 缓存
    blocks.push_back({
        systemPrompt,
        CacheScope::Org
    });
    return blocks;
}

/// 构建 system prompt 块 (匹配原版 TS buildSystemPromptBlocks)
///
/// @param systemPrompt 原始 system prompt 字符串
/// @param enablePromptCaching 是否启用缓存
/// @param skipGlobalCache 是否跳过全局缓存 (MCP tools 存在时)
/// @param querySource 查询来源 (影响 TTL)
inline std::vector<TextBlockParam> buildSystemPromptBlocks(
    const String& systemPrompt,
    bool enablePromptCaching = true,
    bool skipGlobalCache = false,
    const String& querySource = ""
) {
    std::vector<TextBlockParam> result;

    auto blocks = splitSysPromptPrefix(systemPrompt, skipGlobalCache, enablePromptCaching);

    for (const auto& block : blocks) {
        TextBlockParam textBlock;
        textBlock.type = "text";
        textBlock.text = block.text;

        // 添加缓存控制
        if (enablePromptCaching && block.cacheScope.has_value()) {
            CacheControl cache;
            cache.type = "ephemeral";

            // 根据查询来源设置 TTL
            // 某些来源可以使用 1h TTL
            if (querySource == "api" || querySource == "hook") {
                cache.ttl = "1h";
            }

            // 设置作用域
            cache.scope = *block.cacheScope;

            textBlock.cache_control = cache;
        }

        result.push_back(textBlock);
    }

    spdlog::debug("Built {} system prompt blocks (caching={})", result.size(), enablePromptCaching);
    return result;
}

/// 添加缓存断点到消息 (匹配原版 TS addCacheBreakpoints)
///
/// 规则: 每个请求恰好一个消息级 cache_control 标记
/// 默认在最后一条消息上，skipCacheWrite 时在倒数第二条
template<typename MessageParam>
inline void addCacheBreakpoint(
    std::vector<MessageParam>& messages,
    bool enablePromptCaching,
    bool skipCacheWrite = false,
    const String& querySource = ""
) {
    if (!enablePromptCaching || messages.empty()) return;

    size_t markerIndex = skipCacheWrite && messages.size() >= 2
        ? messages.size() - 2
        : messages.size() - 1;

    // Apply cache_control to the target message.
    // For Json messages (Anthropic format), set on the content block.
    // For other MessageParam types with a metadata map, store there.
    if constexpr (std::is_same_v<MessageParam, nlohmann::json>) {
        auto& msg = messages[markerIndex];
        if (msg.contains("content") && msg["content"].is_string()) {
            // Convert string content to content-block format with cache_control
            String textContent = msg["content"].template get<String>();
            Json contentBlocks = Json::array();
            Json block;
            block["type"] = "text";
            block["text"] = textContent;
            block["cache_control"] = {{"type", "ephemeral"}};
            contentBlocks.push_back(block);
            msg["content"] = contentBlocks;
        } else if (msg.contains("content") && msg["content"].is_array()) {
            // Add cache_control to the last content block
            if (!msg["content"].empty()) {
                msg["content"].back()["cache_control"] = {{"type", "ephemeral"}};
            }
        }
    }

    spdlog::debug("Cache breakpoint at message index {}", markerIndex);
}

/// 检查是否应该使用 1h TTL
inline bool shouldUse1hCacheTTL(const String& querySource) {
    return querySource == "api" || querySource == "hook";
}

/// 获取缓存控制对象
inline CacheControl getCacheControl(
    std::optional<CacheScope> scope = std::nullopt,
    const String& querySource = ""
) {
    CacheControl cache;
    cache.type = "ephemeral";

    if (shouldUse1hCacheTTL(querySource)) {
        cache.ttl = "1h";
    }

    if (scope && *scope == CacheScope::Global) {
        cache.scope = CacheScope::Global;
    }

    return cache;
}

/// System prompt 构建器类
class SystemPromptBuilder {
public:
    SystemPromptBuilder& withPrompt(const String& prompt) {
        prompt_ = prompt;
        return *this;
    }

    SystemPromptBuilder& withCaching(bool enable) {
        enableCaching_ = enable;
        return *this;
    }

    SystemPromptBuilder& withGlobalCache(bool enable) {
        enableGlobalCache_ = enable;
        return *this;
    }

    SystemPromptBuilder& skipGlobalCache(bool skip) {
        skipGlobalCache_ = skip;
        return *this;
    }

    SystemPromptBuilder& withQuerySource(const String& source) {
        querySource_ = source;
        return *this;
    }

    std::vector<TextBlockParam> build() {
        return buildSystemPromptBlocks(
            prompt_,
            enableCaching_,
            skipGlobalCache_ || !enableGlobalCache_,
            querySource_
        );
    }

private:
    String prompt_;
    bool enableCaching_ = true;
    bool enableGlobalCache_ = true;
    bool skipGlobalCache_ = false;
    String querySource_;
};

} // namespace claude
