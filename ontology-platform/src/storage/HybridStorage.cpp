#include <ontology/Storage.hpp>
#include <ontology/Persistence.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
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
    std::unique_lock lock(mutex_);
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
}

bool HybridStorage::storeIndividual(const Individual& ind, const std::vector<float>& embedding) {
    std::unique_lock lock(mutex_);
    if (!addIndividualImpl_(ind)) return false;

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
    std::unique_lock lock(mutex_);
    return storeTripleImpl_(triple);
}

bool HybridStorage::addTriple(const Triple& triple) {
    std::unique_lock lock(mutex_);
    return addTripleImpl_(triple);
}

bool HybridStorage::removeTriple(const Triple& triple) {
    std::unique_lock lock(mutex_);
    return removeTripleImpl_(triple);
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
    std::unique_lock lock(mutex_);
    return addIndividualImpl_(ind);
}

bool HybridStorage::updateIndividual(const Individual& ind) {
    std::unique_lock lock(mutex_);
    return updateIndividualImpl_(ind);
}

bool HybridStorage::removeIndividual(const String& id) {
    std::unique_lock lock(mutex_);
    return removeIndividualImpl_(id);
}

// ============================================================================
// 类和关系操作
// ============================================================================

bool HybridStorage::addClass(const Class& cls) {
    std::unique_lock lock(mutex_);
    return addClassImpl_(cls);
}

bool HybridStorage::addRelation(const Relation& rel) {
    std::unique_lock lock(mutex_);
    return addRelationImpl_(rel);
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
    std::unique_lock lock(mutex_);
    tripleStore_.clear();
    individuals_.clear();
    classes_.clear();
    relations_.clear();

    if (graphDB_) {
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
    std::unique_lock lock(mutex_);
    return updateClassImpl_(cls);
}

bool HybridStorage::removeClass(const String& id) {
    std::unique_lock lock(mutex_);
    return removeClassImpl_(id);
}

bool HybridStorage::updateRelation(const Relation& rel) {
    std::unique_lock lock(mutex_);
    return updateRelationImpl_(rel);
}

bool HybridStorage::removeRelation(const String& id) {
    std::unique_lock lock(mutex_);
    return removeRelationImpl_(id);
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
    std::unique_lock lock(mutex_);
    BatchResult result;
    for (const auto& t : triples) {
        if (addTripleImpl_(t)) {
            result.succeeded++;
        } else {
            result.failed++;
            result.errors.push_back("Failed: (" + t.subject + ", " + t.predicate + ", " + t.object + ")");
        }
    }
    return result;
}

HybridStorage::BatchResult HybridStorage::batchAddClasses(const std::vector<Class>& classes) {
    std::unique_lock lock(mutex_);
    BatchResult result;
    for (const auto& cls : classes) {
        if (addClassImpl_(cls)) {
            result.succeeded++;
        } else {
            result.failed++;
            result.errors.push_back("Failed: class " + cls.id);
        }
    }
    return result;
}

HybridStorage::BatchResult HybridStorage::batchAddIndividuals(const std::vector<Individual>& individuals) {
    std::unique_lock lock(mutex_);
    BatchResult result;
    for (const auto& ind : individuals) {
        if (addIndividualImpl_(ind)) {
            result.succeeded++;
        } else {
            result.failed++;
            result.errors.push_back("Failed: individual " + ind.id);
        }
    }
    return result;
}

HybridStorage::BatchResult HybridStorage::batchRemoveTriples(const std::vector<Triple>& triples) {
    std::unique_lock lock(mutex_);
    BatchResult result;
    for (const auto& t : triples) {
        if (removeTripleImpl_(t)) {
            result.succeeded++;
        } else {
            result.failed++;
            result.errors.push_back("Not found: (" + t.subject + ", " + t.predicate + ", " + t.object + ")");
        }
    }
    return result;
}

std::vector<std::pair<String, String>> HybridStorage::computeTransitiveClosure(const String& predicate, int maxDepth) {
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
    return tripleStore_.add(triple);
}

bool HybridStorage::removeTripleImpl_(const Triple& triple) {
    return tripleStore_.remove(triple);
}

bool HybridStorage::storeTripleImpl_(const Triple& triple) {
    return tripleStore_.add(triple);
}

bool HybridStorage::addClassImpl_(const Class& cls) {
    classes_[cls.id] = cls;

    // 同时在图数据库中创建节点
    if (graphDB_) {
        Json props;
        props["name"] = cls.name;
        props["description"] = cls.description;
        props["superClasses"] = cls.superClasses;
        props["metadata"] = cls.metadata;
        graphDB_->createNode(cls.id, "Class", props);
    }

    return true;
}

bool HybridStorage::updateClassImpl_(const Class& cls) {
    auto it = classes_.find(cls.id);
    if (it == classes_.end()) return false;
    it->second = cls;
    return true;
}

bool HybridStorage::removeClassImpl_(const String& id) {
    return classes_.erase(id) > 0;
}

bool HybridStorage::addRelationImpl_(const Relation& rel) {
    relations_[rel.id] = rel;

    // 同时在图数据库中创建节点
    if (graphDB_) {
        Json props;
        props["name"] = rel.name;
        props["description"] = rel.description;
        props["domain"] = rel.domain;
        props["range"] = rel.range;
        props["metadata"] = rel.metadata;
        graphDB_->createNode(rel.id, "Relation", props);
    }

    return true;
}

bool HybridStorage::updateRelationImpl_(const Relation& rel) {
    auto it = relations_.find(rel.id);
    if (it == relations_.end()) return false;
    it->second = rel;
    return true;
}

bool HybridStorage::removeRelationImpl_(const String& id) {
    return relations_.erase(id) > 0;
}

bool HybridStorage::addIndividualImpl_(const Individual& ind) {
    individuals_[ind.id] = ind;

    // 同时在图数据库中创建节点
    if (graphDB_) {
        Json props;
        props["name"] = ind.name;
        props["classId"] = ind.classId;
        props["properties"] = ind.properties;
        props["importance"] = ind.importance;
        props["metadata"] = ind.metadata;
        graphDB_->createNode(ind.id, "Individual", props);
    }

    return true;
}

bool HybridStorage::updateIndividualImpl_(const Individual& ind) {
    auto it = individuals_.find(ind.id);
    if (it == individuals_.end()) {
        return false;
    }
    it->second = ind;

    // 同时更新图数据库
    if (graphDB_) {
        Json props;
        props["name"] = ind.name;
        props["classId"] = ind.classId;
        props["properties"] = ind.properties;
        props["importance"] = ind.importance;
        props["metadata"] = ind.metadata;
        graphDB_->updateNode(ind.id, props);
    }

    return true;
}

bool HybridStorage::removeIndividualImpl_(const String& id) {
    auto it = individuals_.find(id);
    if (it == individuals_.end()) {
        return false;
    }
    individuals_.erase(it);

    // 同时从图数据库删除
    if (graphDB_) {
        graphDB_->deleteNode(id);
    }

    // 从向量数据库删除
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

} // namespace ontology
