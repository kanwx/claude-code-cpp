#pragma once

#include "Core.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <queue>
#include <set>
#include <functional>

namespace ontology {

// ============================================================================
// 真值维护系统 (Truth Maintenance System)
// 支持增量推理的回溯: 当基础事实被撤回时, 递归撤回依赖的派生事实
// ============================================================================

/// 推理正当性
struct Justification {
    String derivedFactId;               // 派生事实ID (subject:predicate:object)
    std::vector<String> supportingFactIds; // 支撑事实ID列表
    String ruleId;                      // 使用的规则
    String inferenceType;               // "forward_chain", "transitive", "inverse", etc.
    float confidence = 1.0f;           // 推理置信度

    Json toJson() const {
        Json j;
        j["derivedFactId"] = derivedFactId;
        j["supportingFactIds"] = supportingFactIds;
        j["ruleId"] = ruleId;
        j["inferenceType"] = inferenceType;
        j["confidence"] = confidence;
        return j;
    }
};

class TruthMaintenanceSystem {
public:
    /// 添加正当性
    void addJustification(const Justification& justification);

    /// 撤回基础事实并级联撤回依赖的派生事实
    /// 返回被撤回的事实列表 (包括输入的事实)
    std::vector<Triple> retractWithDependents(
        const Triple& removedFact,
        const std::unordered_map<String, Triple>& factRegistry
    );

    /// 检查事实是否有正当性支撑
    bool isSupported(const String& factId) const;

    /// 获取事实的所有正当性
    std::vector<Justification> getJustifications(const String& factId) const;

    /// 移除特定事实的所有正当性
    void removeJustifications(const String& factId);

    /// 清空所有正当性
    void clear();

    /// 获取统计
    Json getStats() const;

private:
    // factId -> 所有正当性 (一个事实可由多个正当性支撑)
    std::unordered_map<String, std::vector<Justification>> justificationIndex_;

    // factId -> 依赖它的事实ID集合 (反向索引, 用于级联撤回)
    std::unordered_map<String, std::unordered_set<String>> dependencyIndex_;

    void rebuildDependencyIndex();
};

// ============================================================================
// 时间查询辅助
// ============================================================================

/// 将 ISO 8601 时间字符串转换为 epoch 毫秒
int64_t isoToEpochMs(const String& isoTime);

/// 将 epoch 毫秒转换为 ISO 8601 字符串
String epochMsToIso(int64_t epochMs);

/// 检查时间戳是否在 [validFrom, validTo] 范围内
bool isValidAt(const String& validFrom, const String& validTo, const String& timestamp);

// ============================================================================
// Allen Interval Algebra
// ============================================================================

/// Allen interval relation (13 relations + Unknown)
enum class AllenRelation {
    Before,         // A ends before B starts
    After,          // A starts after B ends (inverse of Before)
    Meets,          // A end = B start
    MetBy,          // B end = A start (inverse of Meets)
    Overlaps,       // A starts before B, overlaps
    OverlappedBy,   // B starts before A, overlaps (inverse of Overlaps)
    During,         // A fully within B
    Contains,       // B fully within A (inverse of During)
    Starts,         // A and B start together, A ends first
    StartedBy,      // A and B start together, B ends first (inverse of Starts)
    Finishes,       // A and B end together, A starts later
    FinishedBy,     // A and B end together, B starts later (inverse of Finishes)
    Equals,         // Same interval
    Unknown         // Cannot determine
};

/// Convert AllenRelation to string
String allenRelationToString(AllenRelation r);

/// Convert string to AllenRelation
AllenRelation stringToAllenRelation(const String& s);

/// Temporal interval with ISO 8601 timestamps
struct TemporalInterval {
    String start;  // ISO 8601 start time
    String end;    // ISO 8601 end time

    /// Compute Allen relation between this interval and another
    AllenRelation relationTo(const TemporalInterval& other) const;

    /// Check if interval is valid (start <= end)
    bool isValid() const;
};

/// Allen algebra composition: given R1(A,B) and R2(B,C), infer possible R3(A,C)
std::set<AllenRelation> allenCompose(AllenRelation r1, AllenRelation r2);

/// Allen algebra inverse: given R, return R^{-1}
AllenRelation allenInverse(AllenRelation r);

/// Hash for std::pair<int,int>
struct PairHash {
    size_t operator()(const std::pair<int,int>& p) const {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

/// Path consistency: check if a set of interval relations is consistent
bool isPathConsistent(
    const std::vector<TemporalInterval>& intervals,
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>, PairHash>& relations);

} // namespace ontology
