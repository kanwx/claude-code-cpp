#pragma once

#include "Core.hpp"
#include "Storage.hpp"
#include "TextEmbedding.hpp"
#include "DempsterShafer.hpp"
#include "Explainability.hpp"
#include <memory>
#include <optional>
#include <unordered_set>
#include <functional>
#include <random>
#include <sstream>
#include <iomanip>

namespace ontology {

// ============================================================================
// 推理结果类型
// ============================================================================

/// 冲突/不一致
struct Conflict {
    String description;
    std::vector<String> entities;
    enum class Severity {
        Error,
        Warning,
        Info
    } severity = Severity::Error;
};

/// 推理结果
struct InferenceResult {
    String query;
    std::vector<Triple> facts;
    String explanation;
    float confidence = 1.0f;
};

// ============================================================================
// 符号推理引擎 (左脑)
// ============================================================================

class SymbolicReasoner {
public:
    explicit SymbolicReasoner(StoragePtr storage);

    /// 添加/删除公理
    void addAxiom(const Axiom& axiom);
    void removeAxiom(const String& axiomId);

    /// 类型推理
    std::vector<String> getTypes(const String& individualId);
    std::vector<String> getSuperClasses(const String& classId);
    std::vector<String> getSubClasses(const String& classId);
    bool isInstanceOf(const String& individualId, const String& classId);
    bool isSubClassOf(const String& subClass, const String& superClass);

    /// 关系推理
    std::vector<Triple> getRelated(const String& subject, const String& relation);
    std::vector<String> getRelatedObjects(const String& subject, const String& relation);
    std::vector<String> getRelatedSubjects(const String& object, const String& relation);

    /// 传递/对称/逆推理
    std::vector<String> inferTransitive(const String& subject, const String& relation);
    std::vector<String> inferSymmetric(const String& subject, const String& relation);
    std::vector<String> inferInverse(const String& subject, const String& relation);

    /// 规则推理
    std::vector<Triple> applyRules(const String& subject, int maxDepth = 3);

    /// 一致性检查
    std::vector<Conflict> checkConsistency();

    /// 通用推理接口
    InferenceResult infer(const String& query);

    /// 设置推理深度
    void setMaxDepth(int depth) { maxDepth_ = depth; }

    /// 设置可解释性引擎
    void setExplainabilityEngine(ExplainabilityEngine* engine) { explainabilityEngine_ = engine; }

private:
    StoragePtr storage_;
    std::unordered_map<String, Axiom> axioms_;
    int maxDepth_ = 3;
    ExplainabilityEngine* explainabilityEngine_ = nullptr;

    // 内部方法
    bool checkPremise(const String& subject, const String& premise);
    std::vector<Triple> generateConclusion(const String& subject, const String& conclusion);
    bool areDisjoint(const String& class1, const String& class2);
    bool isFunctional(const String& relationId);
    std::vector<Triple> inferInstances(const String& query);
    std::vector<String> inferCapabilities(const String& query);
    String generateExplanation(const InferenceResult& result);
};

// ============================================================================
// 嵌入模型抽象接口
// 支持 TransE, RotatE, DistMult, ComplEx, RGCN, CompGCN 等多种模型
// ============================================================================

class EmbeddingModel {
public:
    virtual ~EmbeddingModel() = default;

    virtual void train(const std::vector<Triple>& triples, int epochs, float learningRate) = 0;
    virtual std::vector<float> getEntityEmbedding(const String& entityId) const = 0;
    virtual std::vector<float> getRelationEmbedding(const String& relationId) const = 0;
    virtual void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) = 0;
    virtual void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) = 0;

    virtual std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) = 0;
    virtual std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) = 0;
    virtual std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) = 0;

    virtual String modelName() const = 0;
    virtual int dimension() const = 0;
    virtual Json getStats() const = 0;
};

using EmbeddingModelPtr = std::shared_ptr<EmbeddingModel>;

// ============================================================================
// TransE 嵌入模型
// ============================================================================

class TransEEmbedding : public EmbeddingModel {
public:
    explicit TransEEmbedding(int dimension);

    // EmbeddingModel interface
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;
    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) override;
    String modelName() const override { return "TransE"; }
    int dimension() const override { return dimension_; }
    Json getStats() const override;

    // Legacy compatibility
    std::vector<float> getEmbedding(const String& entityId) { return getEntityEmbedding(entityId); }
    void setEmbedding(const String& entityId, const std::vector<float>& embedding) { setEntityEmbedding(entityId, embedding); }

private:
    int dimension_;
    bool initialized_ = false;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    std::unordered_map<String, std::vector<float>> relationEmbeddings_;

    float distance(const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t);
    float l2Distance(const std::vector<float>& a, const std::vector<float>& b);
};

using EmbeddingPtr = std::shared_ptr<TransEEmbedding>;

// ============================================================================
// 神经推理引擎 (右脑)
// ============================================================================

class NeuralReasoner {
public:
    explicit NeuralReasoner(StoragePtr storage, int embeddingDimension = 768);

    void setEmbeddingModel(EmbeddingModelPtr model);
    void setTextEmbedder(std::shared_ptr<TextEmbedder> embedder);

    /// 按名称选择嵌入模型 ("transE", "rgcn", "compgcn")
    void setEmbeddingModelByName(const String& modelName, const Json& config = {});

    /// 训练嵌入
    void trainEmbeddings(int epochs = 100, float learningRate = 0.01f);

    /// 获取/设置嵌入
    std::vector<float> getEmbedding(const String& entityId);
    void setEmbedding(const String& entityId, const std::vector<float>& embedding);

    /// 相似搜索 (by entity ID)
    std::vector<std::pair<String, float>> findSimilar(
        const String& entityId,
        int topK = 10,
        const String& filterClass = ""
    );
    /// 相似搜索 (by embedding vector)
    std::vector<std::pair<String, float>> findSimilar(
        const std::vector<float>& queryEmbedding,
        int topK = 10,
        const String& filterClass = ""
    );
    /// 相似搜索 (by text query, uses TextEmbedder to embed then search)
    std::vector<std::pair<String, float>> findSimilarByText(
        const String& queryText,
        int topK = 10,
        const String& filterClass = ""
    );

    /// 链接预测
    std::vector<std::pair<String, float>> predictLinks(
        const String& subject,
        const String& relation,
        int topK = 10
    );
    std::vector<std::pair<String, float>> predictHead(
        const String& object,
        const String& relation,
        int topK = 10
    );

    /// 关系预测
    std::vector<std::pair<String, float>> predictRelation(
        const String& subject,
        const String& object,
        int topK = 10
    );

    /// 推理
    InferenceResult infer(const String& query);

    /// 设置可解释性引擎
    void setExplainabilityEngine(ExplainabilityEngine* engine) { explainabilityEngine_ = engine; }

private:
    StoragePtr storage_;
    int embeddingDimension_;
    EmbeddingModelPtr embeddingModel_;
    std::shared_ptr<TextEmbedder> textEmbedder_;
    ExplainabilityEngine* explainabilityEngine_ = nullptr;

    // 内部方法
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;
    std::vector<String> extractEntities(const String& query);
    std::vector<String> extractRelations(const String& query);
    String generateExplanation(const InferenceResult& result);
};

// ============================================================================
// 混合推理引擎 (左脑 + 右脑)
// ============================================================================

class DlReasoner;

class HybridReasoner {
public:
    struct Config {
        float symbolWeight = 0.5f;
        float neuralWeight = 0.5f;
        int maxInferenceDepth = 3;
        bool enableSymbolic = true;
        bool enableNeural = true;
    };

    /// 矛盾检测
    struct Contradiction {
        String reason;
        float confidence;
    };

    /// 神经预测
    struct NeuralPrediction {
        String subject;
        String relation;
        String object;
        float score;
    };

    HybridReasoner(
        std::shared_ptr<SymbolicReasoner> symbolic,
        std::shared_ptr<NeuralReasoner> neural
    );

    HybridReasoner(
        std::shared_ptr<SymbolicReasoner> symbolic,
        std::shared_ptr<NeuralReasoner> neural,
        const Config& config
    );

    void setStorage(StoragePtr storage);

    /// 混合推理结果
    struct HybridResult {
        std::vector<Triple> symbolicFacts;
        std::vector<std::pair<String, float>> neuralPredictions;
        std::vector<Triple> combined;
        std::vector<DsFusionResult> dsFusionDetails;  // Dempster-Shafer 融合细节
        String explanation;
        String traceId;  // 可解释性追踪ID
    };

    /// 执行混合推理
    HybridResult infer(const String& individualId, const String& context = "");

    /// 回答问题
    String answer(const String& question, const Ontology& ontology);

    /// 相似实体搜索
    std::vector<std::pair<Individual, float>> findSimilar(
        const Individual& target,
        int topK = 10
    );

    /// 知识补全
    std::vector<Triple> knowledgeCompletion(const Triple& partial);

    /// 矛盾检测
    std::vector<Contradiction> detectContradictions();

    /// 设置可解释性引擎
    void setExplainabilityEngine(ExplainabilityEngine* engine) { explainabilityEngine_ = engine; }

    /// 设置/获取 DL 推理器 (TBox-aware consistency checking)
    void setDlReasoner(DlReasoner* reasoner) { dlReasoner_ = reasoner; }
    DlReasoner* getDlReasoner() const { return dlReasoner_; }

private:
    std::shared_ptr<SymbolicReasoner> symbolic_;
    std::shared_ptr<NeuralReasoner> neural_;
    StoragePtr hybridStorage_;
    Config config_;
    ExplainabilityEngine* explainabilityEngine_ = nullptr;
    DlReasoner* dlReasoner_ = nullptr;

    std::vector<Triple> mergeResults(
        const std::vector<Triple>& symbolic,
        const std::vector<NeuralPrediction>& neural
    );
};

// ============================================================================
// 规则引擎
// ============================================================================

class RuleEngine {
public:
    struct Rule {
        String id;
        String name;
        String when;
        String then;
        float confidence = 1.0f;
        float priority = 1.0f;
    };

    void addRule(const Rule& rule);
    void removeRule(const String& ruleId);
    std::vector<Triple> execute(const Ontology& ontology, const Triple& trigger);
    std::vector<Triple> executeAll(const Ontology& ontology);
    std::vector<Rule> getRules() const;

private:
    std::unordered_map<String, Rule> rules_;

    bool matchesWhen(const String& whenClause, const Triple& triple) const;
    std::vector<Triple> applyThen(const String& thenClause, const Triple& trigger, float confidence) const;
};

// ============================================================================
// 路径查找器
// ============================================================================

class PathFinder {
public:
    explicit PathFinder(StoragePtr storage);

    std::vector<std::vector<Triple>> findPath(
        const String& from,
        const String& to,
        int maxDepth = 4
    );

    std::vector<Triple> shortestPath(
        const String& from,
        const String& to,
        const std::vector<String>& relationFilter = {}
    );

    std::vector<std::vector<Triple>> allPaths(
        const String& from,
        const String& to,
        int maxDepth = 6
    );

    std::vector<String> commonAncestors(
        const String& entity1,
        const String& entity2
    );

private:
    StoragePtr storage_;
    std::vector<String> bfs(const String& start, int depth, const String& relationFilter = "");
    void dfs(const String& current, const String& target, int maxDepth,
             std::vector<Triple>& currentPath, std::unordered_set<String>& visited,
             std::vector<std::vector<Triple>>& allPaths) const;
};

} // namespace ontology
