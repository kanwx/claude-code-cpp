#include <ontology/Storage.hpp>
#include <ontology/Temporal.hpp>
#include <algorithm>

namespace ontology {

// ============================================================================
// TripleStore 实现
// ============================================================================
//
// Thread-safety model:
//   - Write methods (add, remove, clear) acquire unique_lock (exclusive access)
//   - Read methods acquire shared_lock (concurrent readers allowed)
//   - rebuildIndexes() is private and must only be called while already
//     holding a unique_lock (e.g., from remove())
//   - Composite read methods (query, findPath, etc.) acquire a single
//     shared_lock and call _impl helpers to avoid double-locking / deadlock.
// ============================================================================

// ----------------------------------------------------------------------------
// Private unlocked implementations
// ----------------------------------------------------------------------------

std::vector<Triple> TripleStore::findBySubjectImpl(const String& subject) const {
    std::vector<Triple> result;
    auto it = subjectIndex_.find(subject);
    if (it != subjectIndex_.end()) {
        for (size_t idx : it->second) {
            result.push_back(triples_[idx]);
        }
    }
    return result;
}

std::vector<Triple> TripleStore::findByPredicateImpl(const String& predicate) const {
    std::vector<Triple> result;
    auto it = predicateIndex_.find(predicate);
    if (it != predicateIndex_.end()) {
        for (size_t idx : it->second) {
            result.push_back(triples_[idx]);
        }
    }
    return result;
}

std::vector<Triple> TripleStore::findByObjectImpl(const String& object) const {
    std::vector<Triple> result;
    auto it = objectIndex_.find(object);
    if (it != objectIndex_.end()) {
        for (size_t idx : it->second) {
            result.push_back(triples_[idx]);
        }
    }
    return result;
}

std::vector<Triple> TripleStore::findBySPImpl(const String& subject, const String& predicate) const {
    std::vector<Triple> result;
    auto it = subjectIndex_.find(subject);
    if (it != subjectIndex_.end()) {
        for (size_t idx : it->second) {
            if (triples_[idx].predicate == predicate) {
                result.push_back(triples_[idx]);
            }
        }
    }
    return result;
}

std::vector<Triple> TripleStore::findByPOImpl(const String& predicate, const String& object) const {
    std::vector<Triple> result;
    auto it = predicateIndex_.find(predicate);
    if (it != predicateIndex_.end()) {
        for (size_t idx : it->second) {
            if (triples_[idx].object == object) {
                result.push_back(triples_[idx]);
            }
        }
    }
    return result;
}

std::vector<Triple> TripleStore::findBySOImpl(const String& subject, const String& object) const {
    std::vector<Triple> result;
    auto it = subjectIndex_.find(subject);
    if (it != subjectIndex_.end()) {
        for (size_t idx : it->second) {
            if (triples_[idx].object == object) {
                result.push_back(triples_[idx]);
            }
        }
    }
    return result;
}

std::optional<Triple> TripleStore::findImpl(const String& subject, const String& predicate, const String& object) const {
    for (const auto& t : triples_) {
        if (t.subject == subject && t.predicate == predicate && t.object == object) {
            return t;
        }
    }
    return std::nullopt;
}

std::vector<Triple> TripleStore::queryImpl(const TriplePattern& pattern) const {
    // 根据模式选择最优查询策略
    if (!pattern.subjectIsVar && !pattern.predicateIsVar && !pattern.objectIsVar) {
        auto t = findImpl(pattern.subject, pattern.predicate, pattern.object);
        if (t) return {*t};
        return {};
    } else if (!pattern.subjectIsVar && !pattern.predicateIsVar) {
        return findBySPImpl(pattern.subject, pattern.predicate);
    } else if (!pattern.predicateIsVar && !pattern.objectIsVar) {
        return findByPOImpl(pattern.predicate, pattern.object);
    } else if (!pattern.subjectIsVar && !pattern.objectIsVar) {
        return findBySOImpl(pattern.subject, pattern.object);
    } else if (!pattern.subjectIsVar) {
        return findBySubjectImpl(pattern.subject);
    } else if (!pattern.predicateIsVar) {
        return findByPredicateImpl(pattern.predicate);
    } else if (!pattern.objectIsVar) {
        return findByObjectImpl(pattern.object);
    } else {
        return triples_;
    }
}

// ----------------------------------------------------------------------------
// Write methods — unique_lock (exclusive)
// ----------------------------------------------------------------------------

bool TripleStore::add(const Triple& triple) {
    std::unique_lock lock(mutex_);

    // 检查是否已存在
    for (const auto& t : triples_) {
        if (t.subject == triple.subject &&
            t.predicate == triple.predicate &&
            t.object == triple.object) {
            return false;
        }
    }

    size_t idx = triples_.size();
    triples_.push_back(triple);

    // 更新索引
    subjectIndex_[triple.subject].push_back(idx);
    predicateIndex_[triple.predicate].push_back(idx);
    objectIndex_[triple.object].push_back(idx);

    return true;
}

bool TripleStore::remove(const Triple& triple) {
    std::unique_lock lock(mutex_);

    for (auto it = triples_.begin(); it != triples_.end(); ++it) {
        if (it->subject == triple.subject && it->predicate == triple.predicate && it->object == triple.object) {
            triples_.erase(it);
            rebuildIndexes();  // already under unique_lock — no extra lock needed
            return true;
        }
    }
    return false;
}

void TripleStore::clear() {
    std::unique_lock lock(mutex_);

    triples_.clear();
    subjectIndex_.clear();
    predicateIndex_.clear();
    objectIndex_.clear();
}

void TripleStore::rebuildIndexes() {
    // Private — must only be called while holding unique_lock
    subjectIndex_.clear();
    predicateIndex_.clear();
    objectIndex_.clear();

    for (size_t i = 0; i < triples_.size(); ++i) {
        const auto& t = triples_[i];
        subjectIndex_[t.subject].push_back(i);
        predicateIndex_[t.predicate].push_back(i);
        objectIndex_[t.object].push_back(i);
    }
}

// ----------------------------------------------------------------------------
// Read methods — shared_lock (concurrent readers)
// ----------------------------------------------------------------------------

bool TripleStore::contains(const Triple& triple) const {
    std::shared_lock lock(mutex_);

    for (const auto& t : triples_) {
        if (t.subject == triple.subject && t.predicate == triple.predicate && t.object == triple.object) {
            return true;
        }
    }
    return false;
}

std::vector<Triple> TripleStore::findBySubject(const String& subject) const {
    std::shared_lock lock(mutex_);
    return findBySubjectImpl(subject);
}

std::vector<Triple> TripleStore::findByPredicate(const String& predicate) const {
    std::shared_lock lock(mutex_);
    return findByPredicateImpl(predicate);
}

std::vector<Triple> TripleStore::findByObject(const String& object) const {
    std::shared_lock lock(mutex_);
    return findByObjectImpl(object);
}

std::vector<Triple> TripleStore::findBySP(const String& subject, const String& predicate) const {
    std::shared_lock lock(mutex_);
    return findBySPImpl(subject, predicate);
}

std::vector<Triple> TripleStore::findByPO(const String& predicate, const String& object) const {
    std::shared_lock lock(mutex_);
    return findByPOImpl(predicate, object);
}

std::vector<Triple> TripleStore::findBySO(const String& subject, const String& object) const {
    std::shared_lock lock(mutex_);
    return findBySOImpl(subject, object);
}

std::optional<Triple> TripleStore::find(const String& subject, const String& predicate, const String& object) const {
    std::shared_lock lock(mutex_);
    return findImpl(subject, predicate, object);
}

std::vector<Triple> TripleStore::query(const TriplePattern& pattern) const {
    std::shared_lock lock(mutex_);
    return queryImpl(pattern);
}

std::vector<Triple> TripleStore::queryAtTime(const TriplePattern& pattern, const String& timestamp) const {
    std::shared_lock lock(mutex_);

    // First get candidates from the standard pattern query, then filter by temporal validity
    auto candidates = queryImpl(pattern);

    std::vector<Triple> result;
    for (const auto& triple : candidates) {
        if (isValidAt(triple.validFrom, triple.validTo, timestamp)) {
            result.push_back(triple);
        }
    }
    return result;
}

std::vector<Triple> TripleStore::queryTemporalRange(const TriplePattern& pattern, const String& from, const String& to) const {
    std::shared_lock lock(mutex_);

    // First get candidates from the standard pattern query, then filter by temporal overlap
    auto candidates = queryImpl(pattern);

    // A triple is valid during [from, to] if its validity interval overlaps with [from, to].
    // Overlap condition: triple.validFrom <= to AND (triple.validTo is empty OR triple.validTo >= from)
    // We use epoch milliseconds for robust comparison.
    int64_t fromMs = isoToEpochMs(from);
    int64_t toMs = isoToEpochMs(to);

    std::vector<Triple> result;
    for (const auto& triple : candidates) {
        // If no temporal bounds, it's always valid and thus overlaps any range
        if (triple.validFrom.empty() && triple.validTo.empty()) {
            result.push_back(triple);
            continue;
        }

        int64_t tFrom = triple.validFrom.empty() ? 0 : isoToEpochMs(triple.validFrom);
        int64_t tTo = triple.validTo.empty() ? INT64_MAX : isoToEpochMs(triple.validTo);

        // Overlap: triple's [tFrom, tTo] intersects query's [fromMs, toMs]
        if (tFrom <= toMs && tTo >= fromMs) {
            result.push_back(triple);
        }
    }
    return result;
}

std::vector<std::vector<String>> TripleStore::findPath(
    const String& from,
    const String& to,
    const String& predicate,
    int maxDepth
) const {
    std::shared_lock lock(mutex_);

    std::vector<std::vector<String>> paths;

    if (from == to) {
        paths.push_back({from});
        return paths;
    }

    // BFS 搜索
    std::vector<std::vector<String>> queue;
    queue.push_back({from});

    std::unordered_set<String> visited;
    visited.insert(from);

    while (!queue.empty() && paths.empty()) {
        std::vector<std::vector<String>> nextQueue;

        for (const auto& path : queue) {
            String current = path.back();

            // 获取所有出边 (using _impl — already under shared_lock)
            auto triples = findBySubjectImpl(current);
            for (const auto& t : triples) {
                // 关系过滤
                if (!predicate.empty() && t.predicate != predicate) {
                    continue;
                }

                String next = t.object;

                // 找到目标
                if (next == to) {
                    auto newPath = path;
                    newPath.push_back(t.predicate);
                    newPath.push_back(next);
                    paths.push_back(newPath);
                    continue;
                }

                // 继续搜索
                if (visited.find(next) == visited.end() && (int)path.size() < maxDepth * 2 + 1) {
                    visited.insert(next);
                    auto newPath = path;
                    newPath.push_back(t.predicate);
                    newPath.push_back(next);
                    nextQueue.push_back(newPath);
                }
            }
        }

        queue = std::move(nextQueue);
    }

    return paths;
}

std::vector<String> TripleStore::getObjects(const String& subject, const String& predicate) const {
    std::shared_lock lock(mutex_);

    auto triples = findBySPImpl(subject, predicate);
    std::vector<String> result;
    for (const auto& t : triples) {
        result.push_back(t.object);
    }
    return result;
}

std::vector<String> TripleStore::getSubjects(const String& predicate, const String& object) const {
    std::shared_lock lock(mutex_);

    auto triples = findByPOImpl(predicate, object);
    std::vector<String> result;
    for (const auto& t : triples) {
        result.push_back(t.subject);
    }
    return result;
}

std::vector<String> TripleStore::getAllSubjects() const {
    std::shared_lock lock(mutex_);

    std::vector<String> result;
    for (const auto& [s, _] : subjectIndex_) {
        result.push_back(s);
    }
    return result;
}

std::vector<String> TripleStore::getAllPredicates() const {
    std::shared_lock lock(mutex_);

    std::vector<String> result;
    for (const auto& [p, _] : predicateIndex_) {
        result.push_back(p);
    }
    return result;
}

std::vector<String> TripleStore::getAllObjects() const {
    std::shared_lock lock(mutex_);

    std::vector<String> result;
    for (const auto& [o, _] : objectIndex_) {
        result.push_back(o);
    }
    return result;
}

size_t TripleStore::count() const {
    std::shared_lock lock(mutex_);
    return triples_.size();
}

std::vector<Triple> TripleStore::all() const {
    std::shared_lock lock(mutex_);
    return triples_;
}

} // namespace ontology
