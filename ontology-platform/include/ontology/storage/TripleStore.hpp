#pragma once

#include "../Core.hpp"
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace ontology {

// ============================================================================
// TripleStore - 内存三元组存储
// ============================================================================

class TripleStore {
public:
    /// 添加三元组
    bool add(const Triple& triple);

    /// 移除三元组
    bool remove(const Triple& triple);

    /// 检查三元组是否存在
    bool contains(const Triple& triple) const;

    /// 按主语查询
    std::vector<Triple> findBySubject(const String& subject) const;

    /// 按谓词查询
    std::vector<Triple> findByPredicate(const String& predicate) const;

    /// 按客体查询
    std::vector<Triple> findByObject(const String& object) const;

    /// 按主语-谓词查询
    std::vector<Triple> findBySP(const String& subject, const String& predicate) const;

    /// 按谓词-客体查询
    std::vector<Triple> findByPO(const String& predicate, const String& object) const;

    /// 按主语-客体查询
    std::vector<Triple> findBySO(const String& subject, const String& object) const;

    /// 精确查找
    std::optional<Triple> find(const String& subject, const String& predicate, const String& object) const;

    /// 模式查询
    struct TriplePattern {
        String subject;
        String predicate;
        String object;
        bool subjectIsVar = false;
        bool predicateIsVar = false;
        bool objectIsVar = false;
    };
    std::vector<Triple> query(const TriplePattern& pattern) const;

    /// 时间点查询: 返回在指定时间戳有效的三元组 (validFrom <= timestamp <= validTo)
    std::vector<Triple> queryAtTime(const TriplePattern& pattern, const String& timestamp) const;

    /// 时间范围查询: 返回在指定时间范围内有效的三元组 (与 [from, to] 有重叠)
    std::vector<Triple> queryTemporalRange(const TriplePattern& pattern, const String& from, const String& to) const;

    /// 路径查询
    std::vector<std::vector<String>> findPath(
        const String& from,
        const String& to,
        const String& predicate = "",
        int maxDepth = 10
    ) const;

    /// 工具方法
    std::vector<String> getObjects(const String& subject, const String& predicate) const;
    std::vector<String> getSubjects(const String& predicate, const String& object) const;
    std::vector<String> getAllSubjects() const;
    std::vector<String> getAllPredicates() const;
    std::vector<String> getAllObjects() const;

    size_t count() const;
    void clear();

    std::vector<Triple> all() const;

private:
    std::vector<Triple> triples_;
    std::unordered_map<String, std::vector<size_t>> subjectIndex_;
    std::unordered_map<String, std::vector<size_t>> predicateIndex_;
    std::unordered_map<String, std::vector<size_t>> objectIndex_;
    mutable std::shared_mutex mutex_;

    void rebuildIndexes();  // must be called while holding unique_lock

    // Private unlocked implementations (called under lock by public methods or other _impl)
    std::vector<Triple> findBySubjectImpl(const String& subject) const;
    std::vector<Triple> findByPredicateImpl(const String& predicate) const;
    std::vector<Triple> findByObjectImpl(const String& object) const;
    std::vector<Triple> findBySPImpl(const String& subject, const String& predicate) const;
    std::vector<Triple> findByPOImpl(const String& predicate, const String& object) const;
    std::vector<Triple> findBySOImpl(const String& subject, const String& object) const;
    std::optional<Triple> findImpl(const String& subject, const String& predicate, const String& object) const;
    std::vector<Triple> queryImpl(const TriplePattern& pattern) const;
};

} // namespace ontology
