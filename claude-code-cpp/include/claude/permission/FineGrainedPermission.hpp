#pragma once

#include "../core/Types.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>

namespace claude {

/// 细粒度权限 - 按路径和参数控制权限
class FineGrainedPermission {
public:
    struct Rule {
        String pattern;       // 路径模式 (支持通配符)
        String toolName;      // 工具名称
        String paramName;     // 参数名称
        String allowedValues; // 允许的值 (逗号分隔)
        bool allowed = true;
    };

    /// 检查路径权限
    bool isPathAllowed(const String& toolName, const std::filesystem::path& path) const {
        std::lock_guard<std::mutex> lock(mutex_);

        String pathStr = path.string();

        // 检查精确匹配
        auto key = toolName + ":" + pathStr;
        if (pathRules_.count(key)) {
            return pathRules_.at(key);
        }

        // 检查通配符模式
        for (const auto& [pattern, allowed] : pathRules_) {
            if (matchPattern(pattern, key)) {
                return allowed;
            }
        }

        // 默认允许
        return true;
    }

    /// 检查参数权限
    bool isParamAllowed(const String& toolName, const String& paramName,
                        const String& value) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = toolName + ":" + paramName + ":" + value;
        if (paramRules_.count(key)) {
            return paramRules_.at(key);
        }

        return true;
    }

    /// 添加路径规则
    void addPathRule(const String& toolName, const String& pattern, bool allowed) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = toolName + ":" + pattern;
        pathRules_[key] = allowed;
    }

    /// 添加参数规则
    void addParamRule(const String& toolName, const String& paramName,
                      const String& value, bool allowed) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = toolName + ":" + paramName + ":" + value;
        paramRules_[key] = allowed;
    }

    /// 获取所有规则
    std::vector<Rule> getRules() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Rule> rules;

        for (const auto& [key, allowed] : pathRules_) {
            Rule r;
            r.pattern = key;
            r.allowed = allowed;
            rules.push_back(r);
        }

        return rules;
    }

private:
    bool matchPattern(const String& pattern, const String& str) const {
        // 简单通配符匹配
        if (pattern == "*") return true;
        if (pattern.find("*") == String::npos) {
            return pattern == str;
        }

        // 前缀匹配
        if (pattern.back() == '*') {
            return str.find(pattern.substr(0, pattern.size() - 1)) == 0;
        }

        return false;
    }

    mutable std::mutex mutex_;
    std::unordered_map<String, bool> pathRules_;
    std::unordered_map<String, bool> paramRules_;
};

/// 权限持久化 - 保存和学习用户偏好
class PermissionPersistence {
public:
    struct Decision {
        String toolName;
        String activity;
        bool allowed;
        long timestamp;
        int useCount;
    };

    explicit PermissionPersistence(const std::filesystem::path& dataDir)
        : dataDir_(dataDir) {
        load();
    }

    /// 保存决策
    void saveDecision(const String& toolName, const String& activity, bool allowed) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = toolName + ":" + activity;
        auto& decision = decisions_[key];
        decision.toolName = toolName;
        decision.activity = activity;
        decision.allowed = allowed;
        decision.timestamp = currentTime();
        decision.useCount++;

        persist();
    }

    /// 查询之前的决策
    std::optional<bool> getDecision(const String& toolName, const String& activity) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = toolName + ":" + activity;
        auto it = decisions_.find(key);
        if (it != decisions_.end() && it->second.useCount >= 3) {
            // 用户连续 3 次以上做相同选择，记住它
            return it->second.allowed;
        }
        return std::nullopt;
    }

    /// 学习用户偏好
    void learnPreference(const String& pattern, bool alwaysAllow) {
        std::lock_guard<std::mutex> lock(mutex_);
        learnedPreferences_[pattern] = alwaysAllow;
        persist();
    }

    /// 检查学习到的偏好
    std::optional<bool> checkLearned(const String& activity) const {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& [pattern, allowed] : learnedPreferences_) {
            if (activity.find(pattern) != String::npos) {
                return allowed;
            }
        }
        return std::nullopt;
    }

    /// 清除所有决策
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        decisions_.clear();
        learnedPreferences_.clear();
        persist();
    }

private:
    void load() {
        auto filePath = dataDir_ / "permission_learning.json";
        if (!std::filesystem::exists(filePath)) return;

        try {
            std::ifstream ifs(filePath);
            if (!ifs) return;
            auto data = Json::parse(ifs);

            if (data.contains("decisions") && data["decisions"].is_object()) {
                for (auto& [key, val] : data["decisions"].items()) {
                    if (!val.is_object()) continue;
                    Decision d;
                    d.toolName = val.value("toolName", "");
                    d.activity = val.value("activity", "");
                    d.allowed = val.value("allowed", false);
                    d.timestamp = val.value("timestamp", 0L);
                    d.useCount = val.value("useCount", 0);
                    decisions_[key] = std::move(d);
                }
            }

            if (data.contains("learnedPreferences") && data["learnedPreferences"].is_object()) {
                for (auto& [pattern, val] : data["learnedPreferences"].items()) {
                    if (val.is_boolean()) {
                        learnedPreferences_[pattern] = val.get<bool>();
                    }
                }
            }
        } catch (...) {}
    }

    void persist() {
        try {
            std::filesystem::create_directories(dataDir_);

            Json data = Json::object();

            Json decObj = Json::object();
            for (const auto& [key, d] : decisions_) {
                decObj[key] = {
                    {"toolName", d.toolName},
                    {"activity", d.activity},
                    {"allowed", d.allowed},
                    {"timestamp", d.timestamp},
                    {"useCount", d.useCount}
                };
            }
            data["decisions"] = decObj;

            Json prefObj = Json::object();
            for (const auto& [pattern, allowed] : learnedPreferences_) {
                prefObj[pattern] = allowed;
            }
            data["learnedPreferences"] = prefObj;

            auto filePath = dataDir_ / "permission_learning.json";
            std::ofstream ofs(filePath);
            if (ofs) {
                ofs << data.dump(2);
            }
        } catch (...) {}
    }

    long currentTime() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::filesystem::path dataDir_;
    mutable std::mutex mutex_;
    std::unordered_map<String, Decision> decisions_;
    std::unordered_map<String, bool> learnedPreferences_;
};

} // namespace claude
