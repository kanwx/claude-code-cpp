#pragma once

#include "Forward.hpp"
#include "TripleStore.hpp"
#include "VectorDatabase.hpp"
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace ontology {

// ============================================================================
// 混合存储 (图 + 向量)
// ============================================================================

class HybridStorage {
public:
    HybridStorage(
        GraphDatabasePtr graphDB,
        VectorDatabasePtr vectorDB
    );

    /// 初始化
    bool initialize(const String& collectionName, int embeddingDimension);

    /// 存储本体到混合存储
    bool storeOntology(const Ontology& ontology);

    /// 存储实例 (图节点 + 向量嵌入)
    bool storeIndividual(const Individual& ind, const std::vector<float>& embedding);

    /// 存储三元组
    bool storeTriple(const Triple& triple);
    bool addTriple(const Triple& triple);
    bool removeTriple(const Triple& triple);

    /// 三元组查询
    std::optional<Triple> findTriple(const String& subject, const String& predicate, const String& object) const;
    std::vector<Triple> findBySubject(const String& subject) const;
    std::vector<Triple> findByPredicate(const String& predicate) const;
    std::vector<Triple> findByObject(const String& object) const;
    std::vector<Triple> findBySP(const String& subject, const String& predicate) const;
    std::vector<Triple> findByPO(const String& predicate, const String& object) const;
    std::vector<Triple> getAllTriples() const;
    std::vector<Triple> queryTriples(const TripleStore::TriplePattern& pattern) const;
    std::vector<std::vector<String>> findPath(const String& from, const String& to, const String& predicate = "", int maxDepth = 5) const;

    /// 个体操作
    std::optional<Individual> getIndividual(const String& id) const;
    std::vector<Individual> getIndividualsByClass(const String& classId) const;
    bool addIndividual(const Individual& ind);
    bool updateIndividual(const Individual& ind);
    bool removeIndividual(const String& id);

    /// 类和关系操作
    bool addClass(const Class& cls);
    bool updateClass(const Class& cls);
    bool removeClass(const String& id);
    std::optional<Class> getClass(const String& id) const;

    bool addRelation(const Relation& rel);
    bool updateRelation(const Relation& rel);
    bool removeRelation(const String& id);
    std::optional<Relation> getRelation(const String& id) const;

    /// 统计
    size_t classCount() const;
    size_t relationCount() const;

    /// 混合查询 (符号 + 向量)
    struct HybridResult {
        std::vector<Individual> individuals;    // 符号匹配
        std::vector<VectorDatabase::SearchResult> vectorMatches; // 向量相似
        std::vector<std::pair<Individual, float>> combined; // 混合排序
    };

    HybridResult hybridQuery(
        const Query& query,
        const std::vector<float>& queryEmbedding,
        float symbolWeight = 0.5f,
        float vectorWeight = 0.5f
    ) const;

    /// 语义搜索
    std::vector<Individual> semanticSearch(
        const std::vector<float>& embedding,
        const String& classFilter = "",
        int topK = 10
    ) const;

    /// 向量搜索
    std::vector<VectorDatabase::SearchResult> vectorSearch(
        const std::vector<float>& embedding,
        int topK = 10
    ) const;

    /// 获取图数据库
    GraphDatabase* graphDB() { return graphDB_.get(); }
    const GraphDatabase* graphDB() const { return graphDB_.get(); }

    /// 获取向量数据库
    VectorDatabase* vectorDB() { return vectorDB_.get(); }
    const VectorDatabase* vectorDB() const { return vectorDB_.get(); }

    /// 获取三元组存储
    TripleStore* getTripleStore() { return &tripleStore_; }
    const TripleStore* getTripleStore() const { return &tripleStore_; }

    /// 清空存储
    void clear();

    /// 统计
    size_t tripleCount() const;
    size_t individualCount() const;

    /// 获取所有个体
    std::vector<Individual> getAllIndividuals() const;

    /// 获取所有类
    std::vector<Class> getAllClasses() const;

    /// 获取所有关系
    std::vector<Relation> getAllRelations() const;

    /// 批量操作
    struct BatchResult {
        int succeeded = 0;
        int failed = 0;
        std::vector<String> errors;
    };

    BatchResult batchAddTriples(const std::vector<Triple>& triples);
    BatchResult batchAddClasses(const std::vector<Class>& classes);
    BatchResult batchAddIndividuals(const std::vector<Individual>& individuals);
    BatchResult batchRemoveTriples(const std::vector<Triple>& triples);

    /// 传递闭包
    std::vector<std::pair<String, String>> computeTransitiveClosure(const String& predicate, int maxDepth = 10);

    /// 版本回滚: 恢复到指定时间戳的状态
    /// 1. 找到 timestamp 之前的最新快照并恢复
    /// 2. 重放 WAL 条目直到 timestamp
    bool restoreAsOf(int64_t timestamp, class WalManager* wal, class SnapshotManager* snapshotMgr);

private:
    GraphDatabasePtr graphDB_;
    VectorDatabasePtr vectorDB_;
    TripleStore tripleStore_;
    std::unordered_map<String, Individual> individuals_;
    std::unordered_map<String, Class> classes_;
    std::unordered_map<String, Relation> relations_;
    mutable std::shared_mutex mutex_;

    // Private unlocked implementations (called under lock by public methods or other _impl)
    bool addTripleImpl_(const Triple& triple);
    bool removeTripleImpl_(const Triple& triple);
    bool addClassImpl_(const Class& cls);
    bool updateClassImpl_(const Class& cls);
    bool removeClassImpl_(const String& id);
    bool addRelationImpl_(const Relation& rel);
    bool updateRelationImpl_(const Relation& rel);
    bool removeRelationImpl_(const String& id);
    bool addIndividualImpl_(const Individual& ind);
    bool updateIndividualImpl_(const Individual& ind);
    bool removeIndividualImpl_(const String& id);
    bool storeTripleImpl_(const Triple& triple);
    std::optional<Individual> getIndividualImpl_(const String& id) const;
    std::optional<Class> getClassImpl_(const String& id) const;
    std::optional<Relation> getRelationImpl_(const String& id) const;
};

} // namespace ontology
