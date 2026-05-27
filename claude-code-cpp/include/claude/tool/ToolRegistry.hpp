#pragma once

#include "Tool.hpp"
#include "../mcp/McpClient.hpp"
#include "../mcp/McpManager.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <spdlog/spdlog.h>

namespace claude {

/// 工具注册中心 —— 支持延迟加载和工具搜索
///
/// 工具分两类：
/// - 核心工具: 立即注册，总是发送给 API
/// - 延迟工具: 注册但标记为 deferred，需要通过 ToolSearch 按需加载
///
/// Thread safety: All public methods are guarded by shared_mutex.
/// - READ methods use shared_lock (concurrent reads allowed)
/// - WRITE methods use unique_lock (exclusive access)
/// - Internal "Unsafe" helpers skip locking and are called from
///   within already-locked contexts to avoid recursive locking.
class ToolRegistry {
public:
    ToolRegistry() = default;

    // ========== 注册 ==========

    /// 注册工具
    /// 若工具 isEnabled() 返回 false 则跳过
    void registerTool(ToolPtr tool) {
        std::unique_lock lock(registryMutex_);
        if (!tool) {
            spdlog::warn("Attempted to register null tool");
            return;
        }

        if (!tool->isEnabled()) {
            spdlog::debug("Tool [{}] not enabled, skipping registration", tool->name());
            return;
        }

        String name = tool->name();
        if (tools_.contains(name)) {
            spdlog::warn("Tool [{}] already registered, will be overridden", name);
        }

        tools_[name] = std::move(tool);
        spdlog::debug("Registered tool: [{}]", name);
    }

    /// 批量注册
    template<typename... Tools>
    void registerAll(Tools... tools) {
        (registerTool(std::move(tools)), ...);
    }

    /// 注册内置工具
    void registerBuiltinTools();

    /// Factory: create a tool instance by name.
    /// Used by AgentTool and SwarmCoordinator to build isolated tool registries.
    /// Returns nullptr for unknown tool names.
    static ToolPtr createToolByName(const String& name);

    /// 注册认知工具 (需要 MCP 客户端连接到认知后端)
    void registerCognitiveTools(std::shared_ptr<McpClient> mcpClient);

    /// 注册 MCP 服务器提供的动态工具
    /// 为 McpManager 中每个服务器的每个工具创建 MCPTool 包装器
    void registerMcpTools(std::shared_ptr<McpManager> manager);

    /// 获取 MCP 管理器
    std::shared_ptr<McpManager> getMcpManager() const {
        std::shared_lock lock(registryMutex_);
        return mcpManager_;
    }

    // ========== 查找 ==========

    /// 按名称查找
    Tool* findByName(const String& name) const {
        std::shared_lock lock(registryMutex_);
        return findByNameUnsafe(name);
    }

    /// 检查是否存在
    bool has(const String& name) const {
        std::shared_lock lock(registryMutex_);
        return tools_.contains(name);
    }

    // ========== 遍历 ==========

    /// 获取所有已注册工具
    std::vector<Tool*> getTools() const {
        std::shared_lock lock(registryMutex_);
        std::vector<Tool*> result;
        result.reserve(tools_.size());
        for (const auto& [_, tool] : tools_) {
            result.push_back(tool.get());
        }
        return result;
    }

    /// 获取所有工具名称
    std::vector<String> getToolNames() const {
        std::shared_lock lock(registryMutex_);
        std::vector<String> names;
        names.reserve(tools_.size());
        for (const auto& [name, _] : tools_) {
            names.push_back(name);
        }
        return names;
    }

    // ========== 延迟工具 ==========

    /// 判断工具是否为延迟工具
    bool isDeferredTool(const String& name) const {
        std::shared_lock lock(registryMutex_);
        return isDeferredToolUnsafe(name);
    }

    /// 获取所有核心 (非延迟) 工具
    std::vector<Tool*> getCoreTools() const {
        std::shared_lock lock(registryMutex_);
        std::vector<Tool*> result;
        for (const auto& [_, tool] : tools_) {
            if (!isDeferredToolUnsafe(tool->name())) {
                result.push_back(tool.get());
            }
        }
        return result;
    }

    /// 获取所有延迟工具名称
    std::vector<String> getDeferredToolNames() const {
        std::shared_lock lock(registryMutex_);
        std::vector<String> names;
        for (const auto& [name, _] : tools_) {
            if (isDeferredToolUnsafe(name)) {
                names.push_back(name);
            }
        }
        return names;
    }

    /// 获取延迟工具列表的格式化文本 (每行一个)
    String getDeferredToolList() const {
        std::shared_lock lock(registryMutex_);
        std::ostringstream oss;
        for (const auto& [name, tool] : tools_) {
            if (isDeferredToolUnsafe(name)) {
                String hint = tool->searchHint();
                if (hint.empty()) {
                    oss << name << "\n";
                } else {
                    oss << name << " — " << hint << "\n";
                }
            }
        }
        return oss.str();
    }

    // ========== 工具搜索 ==========

    /// 搜索结果项
    struct SearchResult {
        String name;
        int score;
    };

    /// 通过关键词搜索延迟工具
    /// @param query 搜索查询 (支持 "select:Name1,Name2" 或关键词)
    /// @param maxResults 最大结果数 (默认 5)
    /// @return 匹配的工具名称和得分
    std::vector<SearchResult> searchTools(const String& query, int maxResults = 5) const {
        std::shared_lock lock(registryMutex_);
        // 检查 select: 前缀
        if (query.starts_with("select:")) {
            return searchBySelectUnsafe(query.substr(7), maxResults);
        }
        return searchByKeywordsUnsafe(query, maxResults);
    }

    // ========== 已发现工具追踪 ==========

    /// 记录已发现的延迟工具
    void markDiscovered(const String& name) {
        std::unique_lock lock(registryMutex_);
        discoveredTools_.insert(name);
    }

    /// 批量记录已发现的延迟工具
    void markDiscovered(const std::vector<String>& names) {
        std::unique_lock lock(registryMutex_);
        for (const auto& name : names) {
            discoveredTools_.insert(name);
        }
    }

    /// 检查延迟工具是否已被发现
    bool isDiscovered(const String& name) const {
        std::shared_lock lock(registryMutex_);
        return discoveredTools_.contains(name);
    }

    /// 获取所有已发现的延迟工具名称
    const std::unordered_set<String>& getDiscoveredTools() const {
        std::shared_lock lock(registryMutex_);
        return discoveredTools_;
    }

    /// 清除已发现状态
    void clearDiscovered() {
        std::unique_lock lock(registryMutex_);
        discoveredTools_.clear();
    }

    // ========== 转换 ==========

    /// 转换为 API 调用格式 (核心工具 + 已发现的延迟工具)
    std::vector<ToolDefinition> toToolDefinitions() const {
        std::shared_lock lock(registryMutex_);
        std::vector<ToolDefinition> defs;
        defs.reserve(tools_.size());

        for (const auto& [_, tool] : tools_) {
            // 解析 JSON Schema 字符串
            Json schema;
            try {
                schema = Json::parse(tool->inputSchema());
            } catch (const Json::parse_error& e) {
                spdlog::debug("Failed to parse schema for tool [{}]: {}", tool->name(), e.what());
                schema = {{"type", "object"}};
            }

            bool deferred = isDeferredToolUnsafe(tool->name());

            // 延迟但未发现的工具: 跳过 (不发送给 API)
            // 但在启用 tool search 时，已发现的延迟工具发送 defer_loading=true
            if (deferred && !discoveredTools_.contains(tool->name())) {
                continue;  // 未发现的延迟工具不发送
            }

            defs.push_back({
                tool->name(),
                tool->description(),
                schema,
                std::nullopt,  // cache_control
                deferred && discoveredTools_.contains(tool->name()),  // defer_loading
                false  // strict
            });
        }

        return defs;
    }

    /// 转换为 API 格式 (不使用延迟加载 — 全部工具)
    std::vector<ToolDefinition> toToolDefinitionsNoDefer() const {
        std::shared_lock lock(registryMutex_);
        std::vector<ToolDefinition> defs;
        defs.reserve(tools_.size());

        for (const auto& [_, tool] : tools_) {
            Json schema;
            try {
                schema = Json::parse(tool->inputSchema());
            } catch (const Json::parse_error& e) {
                spdlog::debug("Failed to parse schema for tool [{}]: {}", tool->name(), e.what());
                schema = {{"type", "object"}};
            }

            defs.push_back({
                tool->name(),
                tool->description(),
                schema
            });
        }

        return defs;
    }

    // ========== 统计 ==========

    size_t size() const {
        std::shared_lock lock(registryMutex_);
        return tools_.size();
    }

    bool empty() const {
        std::shared_lock lock(registryMutex_);
        return tools_.empty();
    }

    size_t deferredCount() const { return getDeferredToolNames().size(); }
    size_t coreCount() const { return getCoreTools().size(); }

private:
    std::unordered_map<String, ToolPtr> tools_;
    std::unordered_set<String> discoveredTools_;
    std::shared_ptr<McpManager> mcpManager_;
    mutable std::shared_mutex registryMutex_;

    // ========== Internal unsafe helpers (no lock — caller must hold registryMutex_) ==========

    /// Lookup by name without locking. Caller must hold registryMutex_.
    Tool* findByNameUnsafe(const String& name) const {
        auto it = tools_.find(name);
        return it != tools_.end() ? it->second.get() : nullptr;
    }

    /// Check deferred status without locking. Caller must hold registryMutex_.
    bool isDeferredToolUnsafe(const String& name) const {
        auto* tool = findByNameUnsafe(name);
        if (!tool) return false;
        if (tool->alwaysLoad()) return false;
        if (tool->shouldDefer()) return true;
        return false;
    }

    // ========== 搜索实现 (no lock — caller must hold registryMutex_) ==========

    /// select: 前缀精确选择
    std::vector<SearchResult> searchBySelectUnsafe(const String& names, int maxResults) const {
        std::vector<SearchResult> results;

        // 逗号分隔的工具名称
        std::istringstream iss(names);
        String name;
        while (std::getline(iss, name, ',')) {
            // trim
            size_t start = name.find_first_not_of(" \t");
            size_t end = name.find_last_not_of(" \t");
            if (start == String::npos) continue;
            name = name.substr(start, end - start + 1);

            if (tools_.contains(name)) {
                results.push_back({name, 100});  // 精确匹配最高分
            }
            if (static_cast<int>(results.size()) >= maxResults) break;
        }

        return results;
    }

    /// 关键词搜索
    std::vector<SearchResult> searchByKeywordsUnsafe(const String& query, int maxResults) const {
        std::vector<SearchResult> results;

        // 解析查询词
        std::vector<String> requiredTerms;
        std::vector<String> optionalTerms;
        std::istringstream iss(query);
        String term;
        while (iss >> term) {
            if (term.starts_with("+")) {
                requiredTerms.push_back(term.substr(1));
            } else {
                optionalTerms.push_back(term);
            }
        }

        // 评分每个延迟工具
        for (const auto& [name, tool] : tools_) {
            if (!isDeferredToolUnsafe(name)) continue;

            // 检查必要条件
            String lowerName = toLower(name);
            String lowerDesc = toLower(tool->description());
            String lowerHint = toLower(tool->searchHint());

            bool allRequiredMatch = true;
            for (const auto& req : requiredTerms) {
                String lowerReq = toLower(req);
                if (lowerName.find(lowerReq) == String::npos &&
                    lowerDesc.find(lowerReq) == String::npos &&
                    lowerHint.find(lowerReq) == String::npos) {
                    allRequiredMatch = false;
                    break;
                }
            }
            if (!allRequiredMatch) continue;

            // 计算得分
            int score = 0;

            // 分解工具名 (CamelCase -> 分词)
            auto nameParts = splitToolName(name);
            for (const auto& opt : optionalTerms) {
                String lowerOpt = toLower(opt);

                // 名称部分精确匹配
                for (const auto& part : nameParts) {
                    if (toLower(part) == lowerOpt) score += 10;
                    else if (toLower(part).find(lowerOpt) != String::npos) score += 5;
                }

                // 完整名称部分匹配
                if (lowerName.find(lowerOpt) != String::npos) score += 5;

                // searchHint 匹配
                if (!lowerHint.empty() && lowerHint.find(lowerOpt) != String::npos) score += 4;

                // 描述匹配
                if (lowerDesc.find(lowerOpt) != String::npos) score += 2;
            }

            // 必要词也加一些分
            for (const auto& req : requiredTerms) {
                if (lowerName.find(toLower(req)) != String::npos) score += 3;
            }

            if (score > 0) {
                results.push_back({name, score});
            }
        }

        // 精确名称匹配 (快速路径)
        auto exactIt = tools_.find(query);
        if (exactIt != tools_.end() && isDeferredToolUnsafe(query)) {
            // 检查是否已在结果中
            bool found = false;
            for (auto& r : results) {
                if (r.name == query) { r.score = 100; found = true; break; }
            }
            if (!found) results.push_back({query, 100});
        }

        // 按得分降序排序
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });

        // 截取 maxResults
        if (static_cast<int>(results.size()) > maxResults) {
            results.resize(maxResults);
        }

        return results;
    }

    // ========== 辅助方法 ==========

    /// CamelCase 分词
    static std::vector<String> splitToolName(const String& name) {
        std::vector<String> parts;
        String current;
        for (size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (i > 0 && std::isupper(c)) {
                if (!current.empty()) {
                    parts.push_back(toLowerStatic(current));
                }
                current = c;
            } else if (c == '_' || c == '-') {
                if (!current.empty()) {
                    parts.push_back(toLowerStatic(current));
                }
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            parts.push_back(toLowerStatic(current));
        }
        return parts;
    }

    static String toLower(const String& s) {
        return toLowerStatic(s);
    }

    static String toLowerStatic(const String& s) {
        String result = s;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return result;
    }
};

} // namespace claude
