#pragma once

#include "Core.hpp"
#include "Inference.hpp"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <cmath>
#include <functional>

namespace ontology {

// ============================================================================
// RGCN (Relational Graph Convolutional Network) 嵌入模型
// 对标: Schlichtkrull et al., 2018 - Modeling Relational Data with Graph Convolutional Networks
// 关键创新: 关系特定权重矩阵 + 基分解降低参数量
// ============================================================================

class RGCNEmbedding : public EmbeddingModel {
public:
    struct Config {
        int entityDimension = 768;
        int hiddenDimension = 512;
        int numLayers = 2;
        int numBasis = 100;          // 基分解: W_r = sum_b a_rb * V_b
        float dropout = 0.1f;
        float learningRate = 0.01f;
        int epochs = 1000;
        int negativeSamples = 5;
        float margin = 1.0f;
        float l2Regularization = 1e-5f;
    };

    explicit RGCNEmbedding(const Config& config);

    // EmbeddingModel 接口
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;
    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;
    String modelName() const override { return "RGCN"; }
    int dimension() const override { return config_.entityDimension; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    // RGCN 特有方法
    /// 执行 GNN 前向传播, 获取实体编码
    std::vector<float> encodeEntity(const String& entityId, const std::vector<Triple>& graph) const;

    /// 获取关系权重矩阵 (基分解重建)
    std::vector<std::vector<float>> getRelationWeights(const String& relationId) const;

private:
    Config config_;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    // 基分解: W_r = sum_b a_rb * V_b
    std::vector<std::vector<std::vector<float>>> basisMatrices_;  // [basisIdx][row][col]
    std::unordered_map<String, std::vector<float>> relationCoeffs_; // relationId -> basis coefficients

    // 邻接表
    struct AdjacencyEntry {
        String neighbor;
        String relation;
    };
    std::unordered_map<String, std::vector<AdjacencyEntry>> adjacencyOut_;
    std::unordered_map<String, std::vector<AdjacencyEntry>> adjacencyIn_;
    std::unordered_set<String> entityIds_;
    std::unordered_set<String> relationIds_;
    std::vector<Triple> trainingTriples_;

    bool initialized_ = false;
    std::mt19937 rng_{42};

    void initialize(int numEntities, int numRelations);
    void buildAdjacency(const std::vector<Triple>& triples);
    std::vector<float> messagePassingLayer(
        const std::vector<float>& entityEmb,
        const std::vector<AdjacencyEntry>& neighbors,
        const std::vector<AdjacencyEntry>& inNeighbors
    ) const;
    float scoreTriple(const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t) const;
    std::vector<float> negativeSample(const String& entityId, const std::vector<Triple>& triples);
    void applyGradient(const std::vector<float>& h, const std::vector<float>& r,
                       const std::vector<float>& t, float gradient, float lr);

    static float relu(float x) { return x > 0.0f ? x : 0.0f; }
    static std::vector<float> matVecMul(const std::vector<std::vector<float>>& matrix, const std::vector<float>& vec);
};

// ============================================================================
// CompGCN (Composition-based Graph Convolutional Network) 嵌入模型
// 对标: Vashishth et al., 2020 - Composition-based Multi-Relational Graph Convolutional Networks
// 关键创新: 组合算子同时更新实体和关系嵌入
// ============================================================================

class CompGCNEmbedding : public EmbeddingModel {
public:
    enum class CompositionOp {
        Subtract,          // e_r = e_h - e_t
        Multiply,          // e_r = e_h * e_t
        CircularCorr       // e_r = e_h ⊛ e_t
    };

    struct Config {
        int dimension = 768;
        int numLayers = 2;
        CompositionOp compositionOp = CompositionOp::Multiply;
        float learningRate = 0.01f;
        int epochs = 1000;
        int negativeSamples = 5;
        float margin = 1.0f;
        float l2Regularization = 1e-5f;
    };

    explicit CompGCNEmbedding(const Config& config);

    // EmbeddingModel 接口
    void train(const std::vector<Triple>& triples, int epochs, float learningRate) override;
    std::vector<float> getEntityEmbedding(const String& entityId) const override;
    std::vector<float> getRelationEmbedding(const String& relationId) const override;
    void setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) override;
    void setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) override;
    std::vector<std::pair<String, float>> predictTail(const String& subject, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictHead(const String& object, const String& relation, int topK) const override;
    std::vector<std::pair<String, float>> predictRelation(const String& subject, const String& object, int topK) const override;
    String modelName() const override { return "CompGCN"; }
    int dimension() const override { return config_.dimension; }
    Json getStats() const override;
    float scoreTriple(const Triple& triple) const override;
    std::vector<std::pair<String, float>> findSimilarEntities(const String& entityId, int topK = 10) const override;
    bool save(const String& path) const override;
    bool load(const String& path) override;

    // CompGCN 特有方法
    /// 组合操作
    std::vector<float> compose(const std::vector<float>& entity, const std::vector<float>& relation) const;

    /// GNN 前向传播
    std::vector<float> forward(const String& entityId, int layer) const;

private:
    Config config_;
    std::unordered_map<String, std::vector<float>> entityEmbeddings_;
    std::unordered_map<String, std::vector<float>> relationEmbeddings_;

    // 权重矩阵 (每层)
    std::vector<std::vector<std::vector<float>>> W_O_;  // outgoing [layer][row][col]
    std::vector<std::vector<std::vector<float>>> W_I_;  // incoming
    std::vector<std::vector<std::vector<float>>> W_R_;  // relation update

    // 邻接表
    struct Edge {
        String source;
        String relation;
        String target;
    };
    std::vector<Edge> edges_;
    std::unordered_map<String, std::vector<size_t>> outEdges_;  // source -> edge indices
    std::unordered_map<String, std::vector<size_t>> inEdges_;   // target -> edge indices
    std::unordered_set<String> entityIds_;
    std::unordered_set<String> relationIds_;
    std::vector<Triple> trainingTriples_;

    bool initialized_ = false;
    std::mt19937 rng_{42};

    void initialize(int numEntities, int numRelations);
    void buildGraph(const std::vector<Triple>& triples);

    // 组合操作实现
    std::vector<float> composeSubtract(const std::vector<float>& e, const std::vector<float>& r) const;
    std::vector<float> composeMultiply(const std::vector<float>& e, const std::vector<float>& r) const;
    std::vector<float> composeCircularCorr(const std::vector<float>& e, const std::vector<float>& r) const;

    // 评分函数 (DistMult-style bilinear)
    float scoreTriple(const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t) const;

    // 训练
    void trainStep(float lr);
    std::vector<float> negativeSample(const String& entityId);

    static float relu(float x) { return x > 0.0f ? x : 0.0f; }
    static std::vector<float> matVecMul(const std::vector<std::vector<float>>& matrix, const std::vector<float>& vec);
};

} // namespace ontology
