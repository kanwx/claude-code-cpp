#include <ontology/Storage.hpp>
#include <ontology/Persistence.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace ontology {

// ============================================================================
// HybridStorage 实现 - 混合存储 (图 + 向量)
// ============================================================================

HybridStorage::HybridStorage(GraphDatabasePtr graphDB, VectorDatabasePtr vectorDB)
    : graphDB_(std::move(graphDB)), vectorDB_(std::move(vectorDB)) {
}

bool HybridStorage::initialize(const String& collectionName, int embeddingDimension) {
    std::unique_lock lock(mutex_);
    // 初始化向量集合
    if (vectorDB_) {
        if (!vectorDB_->createCollection(collectionName, embeddingDimension)) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// 本体操作
// ============================================================================

bool HybridStorage::storeOntology(const Ontology& ontology) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        // 存储类
        for (const auto& [id, cls] : ontology.classes) {
            if (!addClassImpl_(cls)) return false;
        }

        // 存储关系
        for (const auto& [id, rel] : ontology.relations) {
            if (!addRelationImpl_(rel)) return false;
        }

        // 存储个体
        for (const auto& [id, ind] : ontology.individuals) {
            if (!addIndividualImpl_(ind)) return false;
        }

        // 存储三元组
        for (const auto& triple : ontology.triples) {
            if (!storeTripleImpl_(triple)) return false;
        }

        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::storeIndividual(const Individual& ind, const std::vector<float>& embedding) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        if (!addIndividualImpl_(ind)) return false;
    } catch (const std::runtime_error&) {
        return false;
    }

    // 存储向量嵌入
    if (vectorDB_ && !embedding.empty()) {
        Json meta;
        meta["name"] = ind.name;
        meta["classId"] = ind.classId;
        return vectorDB_->insert("individuals", ind.id, embedding, meta);
    }

    return true;
}

// ============================================================================
// 三元组操作
// ============================================================================

bool HybridStorage::storeTriple(const Triple& triple) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return storeTripleImpl_(triple);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::addTriple(const Triple& triple) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return addTripleImpl_(triple);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::removeTriple(const Triple& triple) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return removeTripleImpl_(triple);
    } catch (const std::runtime_error&) {
        return false;
    }
}

std::optional<Triple> HybridStorage::findTriple(const String& subject, const String& predicate, const String& object) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.find(subject, predicate, object);
}

std::vector<Triple> HybridStorage::findBySubject(const String& subject) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.findBySubject(subject);
}

std::vector<Triple> HybridStorage::findByPredicate(const String& predicate) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.findByPredicate(predicate);
}

std::vector<Triple> HybridStorage::findByObject(const String& object) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.findByObject(object);
}

std::vector<Triple> HybridStorage::findBySP(const String& subject, const String& predicate) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.findBySP(subject, predicate);
}

std::vector<Triple> HybridStorage::findByPO(const String& predicate, const String& object) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.findByPO(predicate, object);
}

std::vector<Triple> HybridStorage::getAllTriples() const {
    std::shared_lock lock(mutex_);
    return tripleStore_.all();
}

std::vector<Triple> HybridStorage::queryTriples(const TripleStore::TriplePattern& pattern) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.query(pattern);
}

// ============================================================================
// 个体操作
// ============================================================================

std::optional<Individual> HybridStorage::getIndividual(const String& id) const {
    std::shared_lock lock(mutex_);
    return getIndividualImpl_(id);
}

std::vector<Individual> HybridStorage::getIndividualsByClass(const String& classId) const {
    std::shared_lock lock(mutex_);
    std::vector<Individual> result;
    for (const auto& [id, ind] : individuals_) {
        if (ind.classId == classId) {
            result.push_back(ind);
        }
    }
    return result;
}

bool HybridStorage::addIndividual(const Individual& ind) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return addIndividualImpl_(ind);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::updateIndividual(const Individual& ind) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return updateIndividualImpl_(ind);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::removeIndividual(const String& id) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return removeIndividualImpl_(id);
    } catch (const std::runtime_error&) {
        return false;
    }
}

// ============================================================================
// 类和关系操作
// ============================================================================

bool HybridStorage::addClass(const Class& cls) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return addClassImpl_(cls);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::addRelation(const Relation& rel) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return addRelationImpl_(rel);
    } catch (const std::runtime_error&) {
        return false;
    }
}

std::optional<Class> HybridStorage::getClass(const String& id) const {
    std::shared_lock lock(mutex_);
    return getClassImpl_(id);
}

std::optional<Relation> HybridStorage::getRelation(const String& id) const {
    std::shared_lock lock(mutex_);
    return getRelationImpl_(id);
}

// ============================================================================
// 混合查询
// ============================================================================

HybridStorage::HybridResult HybridStorage::hybridQuery(
    const Query& query,
    const std::vector<float>& queryEmbedding,
    float symbolWeight,
    float vectorWeight
) const {
    HybridResult result;

    // Copy local data under read lock
    std::unordered_map<String, Individual> individualsCopy;
    VectorDatabasePtr vectorDBCopy;
    {
        std::shared_lock lock(mutex_);
        individualsCopy = individuals_;
        vectorDBCopy = vectorDB_;
    }

    // 符号匹配 (no lock held)
    for (const auto& [id, ind] : individualsCopy) {
        // 简单匹配: 检查类ID是否在查询的类列表中
        if (query.selectClasses.empty() ||
            std::find(query.selectClasses.begin(), query.selectClasses.end(), ind.classId) != query.selectClasses.end()) {
            result.individuals.push_back(ind);
        }
    }

    // 向量搜索 (no lock held)
    if (vectorDBCopy && !queryEmbedding.empty()) {
        result.vectorMatches = vectorDBCopy->search("individuals", queryEmbedding, 10);
    }

    // 混合排序
    std::unordered_map<String, float> scores;

    // 符号匹配得分
    for (size_t i = 0; i < result.individuals.size(); ++i) {
        scores[result.individuals[i].id] += symbolWeight * (1.0f / (1.0f + i));
    }

    // 向量匹配得分
    for (const auto& match : result.vectorMatches) {
        scores[match.id] += vectorWeight * match.score;
    }

    // 合并结果 (look up from local copy, not from getIndividual)
    for (const auto& [id, score] : scores) {
        auto it = individualsCopy.find(id);
        if (it != individualsCopy.end()) {
            result.combined.push_back({it->second, score});
        }
    }

    // 按得分排序
    std::sort(result.combined.begin(), result.combined.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    return result;
}

std::vector<Individual> HybridStorage::semanticSearch(
    const std::vector<float>& embedding,
    const String& classFilter,
    int topK
) const {
    // Copy local data under read lock
    std::unordered_map<String, Individual> individualsCopy;
    VectorDatabasePtr vectorDBCopy;
    {
        std::shared_lock lock(mutex_);
        individualsCopy = individuals_;
        vectorDBCopy = vectorDB_;
    }

    // Now do vector search without holding the lock
    if (!vectorDBCopy) return {};

    Json filter;
    if (!classFilter.empty()) {
        filter["classId"] = classFilter;
    }

    auto matches = vectorDBCopy->search("individuals", embedding, topK, filter);

    std::vector<Individual> results;
    for (const auto& match : matches) {
        auto it = individualsCopy.find(match.id);
        if (it != individualsCopy.end()) {
            results.push_back(it->second);
        }
    }

    return results;
}

std::vector<VectorDatabase::SearchResult> HybridStorage::vectorSearch(
    const std::vector<float>& embedding,
    int topK
) const {
    // Copy pointer under read lock, then release before network call
    VectorDatabasePtr vectorDBCopy;
    {
        std::shared_lock lock(mutex_);
        vectorDBCopy = vectorDB_;
    }
    if (!vectorDBCopy) return {};
    return vectorDBCopy->search("individuals", embedding, topK);
}

// ============================================================================
// 清空和统计
// ============================================================================

void HybridStorage::clear() {
    if (isReadOnly_) return;
    std::unique_lock lock(mutex_);
    tripleStore_.clear();
    individuals_.clear();
    classes_.clear();
    relations_.clear();
    subClassOfIndex_.clear();

    if (graphDB_ && graphDB_->isConnected()) {
        // Delete all nodes and relationships in Neo4j
        graphDB_->query(std::string("MATCH (n) DETACH DELETE n"));
    }

    if (vectorDB_) {
        // Drop and recreate the individuals collection
        vectorDB_->dropCollection("individuals");
    }
}

size_t HybridStorage::tripleCount() const {
    std::shared_lock lock(mutex_);
    return tripleStore_.count();
}

size_t HybridStorage::individualCount() const {
    std::shared_lock lock(mutex_);
    return individuals_.size();
}

std::vector<Individual> HybridStorage::getAllIndividuals() const {
    std::shared_lock lock(mutex_);
    std::vector<Individual> result;
    for (const auto& [id, ind] : individuals_) {
        result.push_back(ind);
    }
    return result;
}

std::vector<Class> HybridStorage::getAllClasses() const {
    std::shared_lock lock(mutex_);
    std::vector<Class> result;
    for (const auto& [id, cls] : classes_) {
        result.push_back(cls);
    }
    return result;
}

std::vector<Relation> HybridStorage::getAllRelations() const {
    std::shared_lock lock(mutex_);
    std::vector<Relation> result;
    for (const auto& [id, rel] : relations_) {
        result.push_back(rel);
    }
    return result;
}

bool HybridStorage::updateClass(const Class& cls) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return updateClassImpl_(cls);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::removeClass(const String& id) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return removeClassImpl_(id);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::updateRelation(const Relation& rel) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return updateRelationImpl_(rel);
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool HybridStorage::removeRelation(const String& id) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    try {
        return removeRelationImpl_(id);
    } catch (const std::runtime_error&) {
        return false;
    }
}

size_t HybridStorage::classCount() const {
    std::shared_lock lock(mutex_);
    return classes_.size();
}

size_t HybridStorage::relationCount() const {
    std::shared_lock lock(mutex_);
    return relations_.size();
}

std::vector<std::vector<String>> HybridStorage::findPath(
    const String& from, const String& to, const String& predicate, int maxDepth) const {
    std::shared_lock lock(mutex_);
    return tripleStore_.findPath(from, to, predicate, maxDepth);
}

// ============================================================================
// 批量操作
// ============================================================================

HybridStorage::BatchResult HybridStorage::batchAddTriples(const std::vector<Triple>& triples) {
    BatchResult result;
    if (isReadOnly_) {
        result.failed = static_cast<int>(triples.size());
        for (const auto& t : triples) {
            result.errors.push_back("Read-only mode");
        }
        return result;
    }
    std::unique_lock lock(mutex_);
    for (const auto& t : triples) {
        try {
            if (addTripleImpl_(t)) {
                result.succeeded++;
            } else {
                result.failed++;
                result.errors.push_back("Failed: (" + t.subject + ", " + t.predicate + ", " + t.object + ")");
            }
        } catch (const std::runtime_error&) {
            result.failed++;
            result.errors.push_back("GraphDB write failed: (" + t.subject + ", " + t.predicate + ", " + t.object + ")");
        }
    }
    return result;
}

HybridStorage::BatchResult HybridStorage::batchAddClasses(const std::vector<Class>& classes) {
    BatchResult result;
    if (isReadOnly_) {
        result.failed = static_cast<int>(classes.size());
        for (const auto& cls : classes) {
            result.errors.push_back("Read-only mode");
        }
        return result;
    }
    std::unique_lock lock(mutex_);
    for (const auto& cls : classes) {
        try {
            if (addClassImpl_(cls)) {
                result.succeeded++;
            } else {
                result.failed++;
                result.errors.push_back("Failed: class " + cls.id);
            }
        } catch (const std::runtime_error&) {
            result.failed++;
            result.errors.push_back("GraphDB write failed: class " + cls.id);
        }
    }
    return result;
}

HybridStorage::BatchResult HybridStorage::batchAddIndividuals(const std::vector<Individual>& individuals) {
    BatchResult result;
    if (isReadOnly_) {
        result.failed = static_cast<int>(individuals.size());
        for (const auto& ind : individuals) {
            result.errors.push_back("Read-only mode");
        }
        return result;
    }
    std::unique_lock lock(mutex_);
    for (const auto& ind : individuals) {
        try {
            if (addIndividualImpl_(ind)) {
                result.succeeded++;
            } else {
                result.failed++;
                result.errors.push_back("Failed: individual " + ind.id);
            }
        } catch (const std::runtime_error&) {
            result.failed++;
            result.errors.push_back("GraphDB write failed: individual " + ind.id);
        }
    }
    return result;
}

HybridStorage::BatchResult HybridStorage::batchRemoveTriples(const std::vector<Triple>& triples) {
    BatchResult result;
    if (isReadOnly_) {
        result.failed = static_cast<int>(triples.size());
        for (const auto& t : triples) {
            result.errors.push_back("Read-only mode");
        }
        return result;
    }
    std::unique_lock lock(mutex_);
    for (const auto& t : triples) {
        try {
            if (removeTripleImpl_(t)) {
                result.succeeded++;
            } else {
                result.failed++;
                result.errors.push_back("Not found: (" + t.subject + ", " + t.predicate + ", " + t.object + ")");
            }
        } catch (const std::runtime_error&) {
            result.failed++;
            result.errors.push_back("GraphDB write failed: (" + t.subject + ", " + t.predicate + ", " + t.object + ")");
        }
    }
    return result;
}

std::vector<std::pair<String, String>> HybridStorage::computeTransitiveClosure(const String& predicate, int maxDepth) {
    if (isReadOnly_) return {};
    std::unique_lock lock(mutex_);
    std::vector<std::pair<String, String>> closure;
    // Start with all direct (subject, object) pairs for this predicate
    auto direct = tripleStore_.findByPredicate(predicate);
    std::unordered_map<String, std::unordered_set<String>> reach;
    for (const auto& t : direct) {
        reach[t.subject].insert(t.object);
        closure.push_back({t.subject, t.object});
    }
    // Iteratively compute transitive closure
    for (int depth = 1; depth < maxDepth; ++depth) {
        bool added = false;
        for (auto& [from, targets] : reach) {
            std::unordered_set<String> newTargets;
            for (const auto& mid : targets) {
                auto it = reach.find(mid);
                if (it != reach.end()) {
                    for (const auto& finalTarget : it->second) {
                        if (targets.find(finalTarget) == targets.end()) {
                            newTargets.insert(finalTarget);
                        }
                    }
                }
            }
            for (const auto& nt : newTargets) {
                targets.insert(nt);
                closure.push_back({from, nt});
                added = true;
            }
        }
        if (!added) break;
    }
    return closure;
}

bool HybridStorage::restoreAsOf(int64_t timestamp, WalManager* wal, SnapshotManager* snapshotMgr) {
    if (isReadOnly_) return false;
    std::unique_lock lock(mutex_);
    if (!snapshotMgr) return false;

    // Find the latest snapshot before the target timestamp
    String bestSnapshotId;
    int64_t bestSnapshotTs = 0;

    for (const auto& snapId : snapshotMgr->listSnapshots()) {
        // Snapshot IDs are "snap_<epochMs>"
        if (snapId.substr(0, 5) == "snap_") {
            int64_t snapTs = 0;
            try { snapTs = std::stoll(snapId.substr(5)); } catch (const std::exception&) { continue; }
            if (snapTs <= timestamp && snapTs > bestSnapshotTs) {
                bestSnapshotTs = snapTs;
                bestSnapshotId = snapId;
            }
        }
    }

    // If no snapshot found before timestamp, can't restore
    if (bestSnapshotId.empty()) return false;

    // Restore the snapshot (passing nullptr for ragStorage since we only restore graph data)
    if (!snapshotMgr->restoreSnapshot(bestSnapshotId, std::shared_ptr<HybridStorage>(this, [](auto*){}), nullptr)) {
        return false;
    }

    // Replay WAL entries from after the snapshot timestamp up to the target timestamp
    if (wal) {
        wal->replay([this, bestSnapshotTs, timestamp](const WalEntry& entry) {
            // Skip entries before or at the snapshot time
            if (entry.timestamp <= bestSnapshotTs) return;
            // Skip entries after the target time
            if (entry.timestamp > timestamp) return;
            // Skip checkpoints and transaction markers
            if (entry.type == WalEntryType::Checkpoint ||
                entry.type == WalEntryType::BeginTxn ||
                entry.type == WalEntryType::CommitTxn ||
                entry.type == WalEntryType::RollbackTxn) return;

            // Replay the operation (use Impl_ versions — already under lock)
            switch (entry.type) {
                case WalEntryType::AddTriple: {
                    Triple t;
                    t.subject = entry.data.value("subject", "");
                    t.predicate = entry.data.value("predicate", "");
                    t.object = entry.data.value("object", "");
                    t.isLiteral = entry.data.value("isLiteral", false);
                    t.confidence = entry.data.value("confidence", 1.0f);
                    t.weight = entry.data.value("weight", 1.0f);
                    t.source = entry.data.value("source", "");
                    t.provenance = entry.data.value("provenance", "");
                    t.validFrom = entry.data.value("validFrom", "");
                    t.validTo = entry.data.value("validTo", "");
                    addTripleImpl_(t);
                    break;
                }
                case WalEntryType::RemoveTriple: {
                    Triple t;
                    t.subject = entry.data.value("subject", "");
                    t.predicate = entry.data.value("predicate", "");
                    t.object = entry.data.value("object", "");
                    removeTripleImpl_(t);
                    break;
                }
                case WalEntryType::AddClass: {
                    Class cls;
                    cls.id = entry.data.value("id", "");
                    cls.name = entry.data.value("name", "");
                    cls.description = entry.data.value("description", "");
                    if (entry.data.contains("superClasses")) {
                        for (const auto& sc : entry.data["superClasses"]) cls.superClasses.push_back(sc.get<String>());
                    }
                    cls.validFrom = entry.data.value("validFrom", "");
                    cls.validTo = entry.data.value("validTo", "");
                    addClassImpl_(cls);
                    break;
                }
                case WalEntryType::RemoveClass: {
                    removeClassImpl_(entry.data.value("id", ""));
                    break;
                }
                case WalEntryType::AddRelation: {
                    Relation rel;
                    rel.id = entry.data.value("id", "");
                    rel.name = entry.data.value("name", "");
                    rel.domain = entry.data.value("domain", "");
                    rel.range = entry.data.value("range", "");
                    rel.isTransitive = entry.data.value("isTransitive", false);
                    rel.isSymmetric = entry.data.value("isSymmetric", false);
                    addRelationImpl_(rel);
                    break;
                }
                case WalEntryType::RemoveRelation: {
                    removeRelationImpl_(entry.data.value("id", ""));
                    break;
                }
                case WalEntryType::AddIndividual: {
                    Individual ind;
                    ind.id = entry.data.value("id", "");
                    ind.name = entry.data.value("name", "");
                    ind.classId = entry.data.value("classId", "");
                    if (entry.data.contains("properties")) ind.properties = entry.data["properties"];
                    if (entry.data.contains("relations")) ind.relations = entry.data["relations"].get<std::unordered_map<String, std::vector<String>>>();
                    ind.importance = entry.data.value("importance", 1.0f);
                    ind.validFrom = entry.data.value("validFrom", "");
                    ind.validTo = entry.data.value("validTo", "");
                    addIndividualImpl_(ind);
                    break;
                }
                case WalEntryType::RemoveIndividual: {
                    removeIndividualImpl_(entry.data.value("id", ""));
                    break;
                }
                default:
                    break;
            }
        });
    }

    return true;
}

// ============================================================================
// Private unlocked implementations (called under lock by public methods)
// ============================================================================

bool HybridStorage::addTripleImpl_(const Triple& triple) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        bool ok = graphDB_->createTriple(triple);
        if (!ok) {
            consecutiveWriteFailures_++;
            if (consecutiveWriteFailures_ >= MAX_WRITE_FAILURES) {
                isReadOnly_ = true;
                spdlog::error("GraphDB write failures exceeded threshold, entering read-only mode");
                startReconnectionLoop();
            }
            throw std::runtime_error("GraphDB write failed");
        }
        consecutiveWriteFailures_ = 0;
    }

    // Update memory
    bool result = tripleStore_.add(triple);
    if (result && triple.predicate == "subClassOf") {
        subClassOfIndex_[triple.object].push_back(triple.subject);
    }
    return result;
}

bool HybridStorage::removeTripleImpl_(const Triple& triple) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        graphDB_->deleteTriple(triple);
    }

    bool result = tripleStore_.remove(triple);
    if (result && triple.predicate == "subClassOf") {
        auto& children = subClassOfIndex_[triple.object];
        children.erase(std::remove(children.begin(), children.end(), triple.subject), children.end());
        if (children.empty()) subClassOfIndex_.erase(triple.object);
    }
    return result;
}

bool HybridStorage::storeTripleImpl_(const Triple& triple) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        bool ok = graphDB_->createTriple(triple);
        if (!ok) {
            consecutiveWriteFailures_++;
            if (consecutiveWriteFailures_ >= MAX_WRITE_FAILURES) {
                isReadOnly_ = true;
                spdlog::error("GraphDB write failures exceeded threshold, entering read-only mode");
                startReconnectionLoop();
            }
            throw std::runtime_error("GraphDB write failed");
        }
        consecutiveWriteFailures_ = 0;
    }

    // Update memory
    bool result = tripleStore_.add(triple);
    if (result && triple.predicate == "subClassOf") {
        subClassOfIndex_[triple.object].push_back(triple.subject);
    }
    return result;
}

bool HybridStorage::addClassImpl_(const Class& cls) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        Json props;
        props["name"] = cls.name;
        props["description"] = cls.description;
        props["superClasses"] = cls.superClasses;
        props["metadata"] = cls.metadata;
        bool ok = graphDB_->createNode(cls.id, "Class", props);
        if (!ok) {
            consecutiveWriteFailures_++;
            if (consecutiveWriteFailures_ >= MAX_WRITE_FAILURES) {
                isReadOnly_ = true;
                spdlog::error("GraphDB write failures exceeded threshold, entering read-only mode");
                startReconnectionLoop();
            }
            throw std::runtime_error("GraphDB write failed");
        }
        consecutiveWriteFailures_ = 0;

        // Also create subClassOf edges for each super class
        for (const auto& superId : cls.superClasses) {
            graphDB_->createRelation(cls.id, "subClassOf", superId, {});
        }
    }

    // Update memory
    classes_[cls.id] = cls;
    return true;
}

bool HybridStorage::updateClassImpl_(const Class& cls) {
    auto it = classes_.find(cls.id);
    if (it == classes_.end()) return false;

    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        Json props;
        props["name"] = cls.name;
        props["description"] = cls.description;
        props["superClasses"] = cls.superClasses;
        props["metadata"] = cls.metadata;
        graphDB_->updateNode(cls.id, props);
    }

    // Update memory
    it->second = cls;
    return true;
}

bool HybridStorage::removeClassImpl_(const String& id) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        graphDB_->deleteClass(id);
    }

    return classes_.erase(id) > 0;
}

bool HybridStorage::addRelationImpl_(const Relation& rel) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        Json props;
        props["name"] = rel.name;
        props["description"] = rel.description;
        props["domain"] = rel.domain;
        props["range"] = rel.range;
        props["metadata"] = rel.metadata;
        bool ok = graphDB_->createNode(rel.id, "Relation", props);
        if (!ok) {
            consecutiveWriteFailures_++;
            if (consecutiveWriteFailures_ >= MAX_WRITE_FAILURES) {
                isReadOnly_ = true;
                spdlog::error("GraphDB write failures exceeded threshold, entering read-only mode");
                startReconnectionLoop();
            }
            throw std::runtime_error("GraphDB write failed");
        }
        consecutiveWriteFailures_ = 0;
    }

    // Update memory
    relations_[rel.id] = rel;
    return true;
}

bool HybridStorage::updateRelationImpl_(const Relation& rel) {
    auto it = relations_.find(rel.id);
    if (it == relations_.end()) return false;

    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        Json props;
        props["name"] = rel.name;
        props["description"] = rel.description;
        props["domain"] = rel.domain;
        props["range"] = rel.range;
        props["metadata"] = rel.metadata;
        graphDB_->updateNode(rel.id, props);
    }

    // Update memory
    it->second = rel;
    return true;
}

bool HybridStorage::removeRelationImpl_(const String& id) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        graphDB_->deleteRelation(id);
    }

    return relations_.erase(id) > 0;
}

bool HybridStorage::addIndividualImpl_(const Individual& ind) {
    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        Json props;
        props["name"] = ind.name;
        props["classId"] = ind.classId;
        props["properties"] = ind.properties;
        props["importance"] = ind.importance;
        props["metadata"] = ind.metadata;
        bool ok = graphDB_->createNode(ind.id, "Individual", props);
        if (!ok) {
            consecutiveWriteFailures_++;
            if (consecutiveWriteFailures_ >= MAX_WRITE_FAILURES) {
                isReadOnly_ = true;
                spdlog::error("GraphDB write failures exceeded threshold, entering read-only mode");
                startReconnectionLoop();
            }
            throw std::runtime_error("GraphDB write failed");
        }
        consecutiveWriteFailures_ = 0;
    }

    // Update memory
    individuals_[ind.id] = ind;
    return true;
}

bool HybridStorage::updateIndividualImpl_(const Individual& ind) {
    auto it = individuals_.find(ind.id);
    if (it == individuals_.end()) {
        return false;
    }

    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        Json props;
        props["name"] = ind.name;
        props["classId"] = ind.classId;
        props["properties"] = ind.properties;
        props["importance"] = ind.importance;
        props["metadata"] = ind.metadata;
        graphDB_->updateNode(ind.id, props);
    }

    // Update memory
    it->second = ind;
    return true;
}

bool HybridStorage::removeIndividualImpl_(const String& id) {
    auto it = individuals_.find(id);
    if (it == individuals_.end()) {
        return false;
    }

    // Authority source: write to graphDB first
    if (graphDB_ && graphDB_->isConnected()) {
        if (isReadOnly_) throw std::runtime_error("Read-only mode");
        graphDB_->deleteIndividual(id);
    }

    // Update memory
    individuals_.erase(it);

    // Remove from vector database
    if (vectorDB_) {
        vectorDB_->remove("individuals", id);
    }

    return true;
}

std::optional<Individual> HybridStorage::getIndividualImpl_(const String& id) const {
    auto it = individuals_.find(id);
    if (it != individuals_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Class> HybridStorage::getClassImpl_(const String& id) const {
    auto it = classes_.find(id);
    if (it != classes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Relation> HybridStorage::getRelationImpl_(const String& id) const {
    auto it = relations_.find(id);
    if (it != relations_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ============================================================================
// SubClassOf index accessors
// ============================================================================

std::vector<String> HybridStorage::getDirectSubClasses(const String& classId) const {
    std::shared_lock lock(mutex_);
    auto it = subClassOfIndex_.find(classId);
    if (it != subClassOfIndex_.end()) return it->second;
    return {};
}

std::vector<String> HybridStorage::getAllSubClasses(const String& classId) const {
    std::shared_lock lock(mutex_);
    std::vector<String> result;
    std::unordered_set<String> visited;

    std::function<void(const String&)> collect = [&](const String& cid) {
        auto it = subClassOfIndex_.find(cid);
        if (it != subClassOfIndex_.end()) {
            for (const auto& sub : it->second) {
                if (visited.insert(sub).second) {
                    result.push_back(sub);
                    collect(sub);
                }
            }
        }
    };
    collect(classId);
    return result;
}

// ============================================================================
// Authority source: read-only mode and reconnection
// ============================================================================

bool HybridStorage::isReadOnly() const {
    std::shared_lock lock(mutex_);
    return isReadOnly_;
}

void HybridStorage::setReadOnly(bool readOnly) {
    std::unique_lock lock(mutex_);
    isReadOnly_ = readOnly;
    if (!readOnly) consecutiveWriteFailures_ = 0;
}

std::vector<String> HybridStorage::getSuperClasses(const String& classId) const {
    std::shared_lock lock(mutex_);
    auto it = classes_.find(classId);
    if (it == classes_.end()) return {};
    return it->second.superClasses;
}

bool HybridStorage::loadFromGraphDB() { return false; }
void HybridStorage::startReconnectionLoop() {}
void HybridStorage::stopReconnectionLoop() {}

} // namespace ontology
