#pragma once

#include "Core.hpp"
#include "Inference.hpp"
#include <vector>
#include <cmath>

namespace ontology {

// Forward declarations
class HybridStorage;
class SymbolicReasoner;
class NeuralReasoner;

// ============================================================================
// 本体嵌入 - 将本体元素映射到向量空间
// Inherits from EmbeddingModel for polymorphic usage
// ============================================================================

class OntologyEmbedding : public EmbeddingModel {
public:
    struct Config {
        int dimension = 768;
        String method = "transE";  // transE, transH, transR, transD, rotatE, distMult, complEx
        float margin = 1.0f;
        float learningRate = 0.01f;
        int epochs = 1000;
        int negativeSamples = 5;
    };

    explicit OntologyEmbedding(const Config& config);

    // EmbeddingModel interface
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;

    std::vector<std::pair<String, float>> predictTail(const String& head, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;

    String modelName() const override { return config_.method; }
    int dimension() const override { return config_.dimension; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(
        const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    /// Convenience: train from an Ontology object (extracts triples internally)
    void train(const Ontology& ontology);

private:
    Config config_;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    std::unordered_map<String, std::vector<float>> relationEmbeddings_;

    // 距离函数
    float l2Distance(const std::vector<float>& a, const std::vector<float>& b) const;
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;

    // TransE: h + r ≈ t
    float transEScore(const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t) const;

    // 损失函数
    float marginRankingLoss(float positive, float negative) const;
};

// ============================================================================
// TransR 嵌入 - 关系特定的投影空间
// ============================================================================

class TransREmbedding : public EmbeddingModel {
public:
    explicit TransREmbedding(int entityDim, int relationDim);
    ~TransREmbedding() = default;

    // EmbeddingModel interface
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;

    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;

    String modelName() const override { return "TransR"; }
    int dimension() const override { return entityDim_; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    // TransR-specific methods
    std::vector<std::vector<float>> getProjectionMatrix(const String& relationId) const;

    // Legacy convenience
    void setEmbedding(const String& entityId, const std::vector<float>& embedding) { setEntityEmbedding(entityId, embedding); }

private:
    int entityDim_;
    int relationDim_;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    std::unordered_map<String, std::vector<float>> relationEmbeddings_;
    std::unordered_map<String, std::vector<std::vector<float>>> projectionMatrices_; // Mr for each relation

    // 投影: e_proj = Mr * e
    std::vector<float> project(const std::vector<float>& entity, const std::vector<std::vector<float>>& matrix) const;

    float score(const std::vector<float>& h, const std::vector<float>& r,
                const std::vector<float>& t, const std::vector<std::vector<float>>& Mr) const;

    bool initialized_ = false;
};

// ============================================================================
// DistMult 嵌入 - 双线性形式
// ============================================================================

class DistMultEmbedding : public EmbeddingModel {
public:
    explicit DistMultEmbedding(int dimension);
    ~DistMultEmbedding() = default;

    // EmbeddingModel interface
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;

    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;

    String modelName() const override { return "DistMult"; }
    int dimension() const override { return dimension_; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    // Legacy convenience
    void setEmbedding(const String& entityId, const std::vector<float>& embedding) { setEntityEmbedding(entityId, embedding); }

private:
    int dimension_;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    std::unordered_map<String, std::vector<float>> relationEmbeddings_;

    // DistMult score: f(h,r,t) = h^T * diag(r) * t = sum(h_i * r_i * t_i)
    float score(const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t) const;

    bool initialized_ = false;
};

// ============================================================================
// ComplEx 嵌入 - 复数值嵌入
// ============================================================================

class ComplExEmbedding : public EmbeddingModel {
public:
    explicit ComplExEmbedding(int dimension);
    ~ComplExEmbedding() = default;

    // EmbeddingModel interface
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;      // 返回实部
    std::vector<float> getRelationEmbedding(const String& relationId) const override;  // 返回实部
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;

    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;

    String modelName() const override { return "ComplEx"; }
    int dimension() const override { return dimension_; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    // ComplEx-specific methods (non-virtual)
    std::vector<float> getEntityEmbeddingIm(const String& entityId) const;
    std::vector<float> getRelationEmbeddingIm(const String& relationId) const;
    void setEmbedding(const String& entityId, const std::vector<float>& real, const std::vector<float>& imag);

private:
    int dimension_;
    // 实部和虚部分别存储
    std::unordered_map<String, std::vector<float>> entityReal_;
    std::unordered_map<String, std::vector<float>> entityImag_;
    std::unordered_map<String, std::vector<float>> relationReal_;
    std::unordered_map<String, std::vector<float>> relationImag_;

    // ComplEx score: Re(<h, r, conj(t)>)
    // = Re(h_r * r_r * t_r + h_r * r_i * t_i + h_i * r_r * t_i - h_i * r_i * t_r)
    float score(
        const std::vector<float>& hRe, const std::vector<float>& hIm,
        const std::vector<float>& rRe, const std::vector<float>& rIm,
        const std::vector<float>& tRe, const std::vector<float>& tIm
    ) const;

    bool initialized_ = false;
};

// ============================================================================
// RotatE 嵌入 - 旋转关系建模
// ============================================================================

class RotatEEmbedding : public EmbeddingModel {
public:
    explicit RotatEEmbedding(int dimension);
    ~RotatEEmbedding() = default;

    // EmbeddingModel interface
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;

    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;

    String modelName() const override { return "RotatE"; }
    int dimension() const override { return dimension_; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    // RotatE-specific methods (non-virtual)
    std::vector<float> getRelationPhase(const String& relationId) const;  // 关系建模为相位
    void setRelationPhase(const String& relationId, const std::vector<float>& phase);

    // Legacy convenience
    void setEmbedding(const String& entityId, const std::vector<float>& embedding) { setEntityEmbedding(entityId, embedding); }

private:
    int dimension_;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    std::unordered_map<String, std::vector<float>> relationPhases_;  // Θ_r

    // RotatE: t = h ⊙ r, where r = e^{iΘ_r}
    // 即每个关系元素是单位复数 e^{iθ}
    // h ⊙ r 表示逐元素复数乘法
    float score(
        const std::vector<float>& h,
        const std::vector<float>& theta_r,
        const std::vector<float>& t
    ) const;

    // 计算旋转后的嵌入
    std::vector<float> rotate(const std::vector<float>& h, const std::vector<float>& theta) const;

    bool initialized_ = false;
};

// ============================================================================
// 图神经网络 - 处理本体图结构
// ============================================================================

class GraphNeuralNetwork {
public:
    struct Config {
        int inputDimension = 768;
        int hiddenDimension = 512;
        int outputDimension = 256;
        int numLayers = 3;
        float dropout = 0.1f;
        String aggregation = "mean";  // mean, max, attention
    };

    explicit GraphNeuralNetwork(const Config& config);

    /// 前向传播
    std::vector<float> forward(
        const std::vector<float>& nodeFeatures,
        const std::vector<std::vector<float>>& neighborFeatures,
        const std::vector<float>& edgeWeights
    );

    /// 图卷积层
    std::vector<float> graphConv(
        const std::vector<float>& input,
        const std::vector<std::vector<float>>& neighbors,
        const std::vector<float>& weights
    );

    /// 注意力聚合
    std::vector<float> attentionAggregate(
        const std::vector<float>& query,
        const std::vector<std::vector<float>>& keys,
        const std::vector<std::vector<float>>& values
    );

private:
    Config config_;
    std::vector<std::vector<std::vector<float>>> weights_;  // 层权重 [layer][row][col]

    // 激活函数
    std::vector<float> relu(const std::vector<float>& x) const;
    std::vector<float> softmax(const std::vector<float>& x) const;
};

// ============================================================================
// 知识图谱神经网络 (KGNN)
// ============================================================================

class KnowledgeGraphNN {
public:
    struct Config {
        int embeddingDimension = 768;
        int gnnHiddenDimension = 512;
        int numGNNLayers = 3;
        int numAttentionHeads = 8;
        float dropout = 0.1f;
    };

    explicit KnowledgeGraphNN(const Config& config);

    /// 编码实体 (结合嵌入 + 图结构)
    std::vector<float> encodeEntity(
        const Individual& entity,
        const Ontology& ontology,
        const EmbeddingModel& embedding
    );

    /// 编码关系
    std::vector<float> encodeRelation(
        const Relation& relation,
        const EmbeddingModel& embedding
    );

    /// 编码三元组
    std::vector<float> encodeTriple(
        const Triple& triple,
        const Ontology& ontology,
        const EmbeddingModel& embedding
    );

    /// 知识推理
    std::vector<Triple> reason(
        const Ontology& ontology,
        const String& query,
        int topK = 10
    );

    /// 问题回答
    String answerQuestion(
        const String& question,
        const Ontology& ontology,
        const EmbeddingModel& embedding
    );

private:
    Config config_;
    GraphNeuralNetwork gnn_;
    EmbeddingModelPtr embedding_;

    // 注意力机制
    std::vector<float> multiHeadAttention(
        const std::vector<float>& query,
        const std::vector<float>& key,
        const std::vector<float>& value,
        int numHeads
    );
};

// ============================================================================
// 左右脑协同系统
// ============================================================================

class DualBrainSystem {
public:
    struct Config {
        // 左脑 (符号) 配置
        bool enableSymbolic = true;
        int maxInferenceDepth = 3;

        // 右脑 (神经) 配置
        bool enableNeural = true;
        int embeddingDimension = 768;

        // 协同配置
        float symbolicWeight = 0.5f;
        float neuralWeight = 0.5f;
        float integrationThreshold = 0.7f;
    };

    explicit DualBrainSystem(const Config& config);

    /// 设置本体
    void setOntology(std::shared_ptr<Ontology> ontology);

    /// 设置存储
    void setStorage(std::shared_ptr<HybridStorage> storage);

    /// 处理查询 (左右脑协同)
    struct Result {
        // 左脑结果
        std::vector<Triple> symbolicFacts;
        String symbolicExplanation;

        // 右脑结果
        std::vector<std::pair<Individual, float>> neuralMatches;
        std::vector<float> queryEmbedding;

        // 融合结果
        std::vector<Individual> combinedResults;
        String finalAnswer;
        float confidence;
    };

    Result process(const String& query);

    /// 符号推理 (左脑)
    std::vector<Triple> leftBrainReason(const String& query);

    /// 神经推理 (右脑)
    std::vector<std::pair<Individual, float>> rightBrainReason(const String& query);

    /// 结果融合
    std::vector<Individual> integrateResults(
        const std::vector<Triple>& symbolic,
        const std::vector<std::pair<Individual, float>>& neural
    );

    /// 冲突解决
    std::vector<Individual> resolveConflicts(
        const std::vector<Individual>& results1,
        const std::vector<Individual>& results2
    );

private:
    Config config_;
    std::shared_ptr<Ontology> ontology_;
    std::shared_ptr<HybridStorage> storage_;

    std::unique_ptr<SymbolicReasoner> symbolicReasoner_;
    std::unique_ptr<NeuralReasoner> neuralReasoner_;
    EmbeddingModelPtr embedding_;
};

} // namespace ontology
