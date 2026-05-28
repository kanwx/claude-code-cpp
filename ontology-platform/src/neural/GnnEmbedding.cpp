#include <ontology/GnnEmbedding.hpp>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace ontology {

// ============================================================================
// RGCNEmbedding 实现
// ============================================================================

RGCNEmbedding::RGCNEmbedding(const Config& config) : config_(config) {}

void RGCNEmbedding::initialize(int numEntities, int numRelations) {
    std::normal_distribution<float> dist(0.0f, 0.1f / std::sqrt(static_cast<float>(config_.entityDimension)));

    for (const auto& id : entityIds_) {
        if (entityEmbeddings_.find(id) == entityEmbeddings_.end()) {
            std::vector<float> emb(config_.entityDimension);
            for (auto& v : emb) v = dist(rng_);
            entityEmbeddings_[id] = std::move(emb);
        }
    }

    // 基分解: 初始化基矩阵 V_b (numBasis x entityDim x hiddenDim)
    basisMatrices_.resize(config_.numBasis);
    for (int b = 0; b < config_.numBasis; b++) {
        basisMatrices_[b].resize(config_.entityDimension);
        for (auto& row : basisMatrices_[b]) {
            row.resize(config_.hiddenDimension);
            for (auto& v : row) v = dist(rng_);
        }
    }

    // 初始化关系系数 a_rb
    for (const auto& relId : relationIds_) {
        if (relationCoeffs_.find(relId) == relationCoeffs_.end()) {
            std::vector<float> coeffs(config_.numBasis);
            std::normal_distribution<float> cdist(0.0f, 0.1f);
            for (auto& c : coeffs) c = cdist(rng_);
            relationCoeffs_[relId] = std::move(coeffs);
        }
    }

    initialized_ = true;
}

void RGCNEmbedding::buildAdjacency(const std::vector<Triple>& triples) {
    adjacencyOut_.clear();
    adjacencyIn_.clear();
    entityIds_.clear();
    relationIds_.clear();

    for (const auto& t : triples) {
        entityIds_.insert(t.subject);
        entityIds_.insert(t.object);
        relationIds_.insert(t.predicate);

        adjacencyOut_[t.subject].push_back({t.object, t.predicate});
        adjacencyIn_[t.object].push_back({t.subject, t.predicate});
    }
}

std::vector<float> RGCNEmbedding::messagePassingLayer(
    const std::vector<float>& entityEmb,
    const std::vector<AdjacencyEntry>& neighbors,
    const std::vector<AdjacencyEntry>& inNeighbors
) const {
    int D = config_.entityDimension;
    int H = config_.hiddenDimension;
    std::vector<float> aggregated(H, 0.0f);

    // 出边消息
    for (const auto& [neighborId, relId] : neighbors) {
        auto nit = entityEmbeddings_.find(neighborId);
        auto rit = relationCoeffs_.find(relId);
        if (nit == entityEmbeddings_.end() || rit == relationCoeffs_.end()) continue;

        // 重建关系权重 W_r = sum_b a_rb * V_b
        std::vector<std::vector<float>> Wr(D, std::vector<float>(H, 0.0f));
        for (int b = 0; b < config_.numBasis; b++) {
            float coeff = rit->second[b];
            for (int i = 0; i < D; i++) {
                for (int j = 0; j < H; j++) {
                    Wr[i][j] += coeff * basisMatrices_[b][i][j];
                }
            }
        }

        auto msg = matVecMul(Wr, nit->second);
        for (int j = 0; j < H && j < static_cast<int>(msg.size()); j++) {
            aggregated[j] += msg[j];
        }
    }

    // 入边消息 (逆关系)
    for (const auto& [neighborId, relId] : inNeighbors) {
        auto nit = entityEmbeddings_.find(neighborId);
        auto rit = relationCoeffs_.find(relId);
        if (nit == entityEmbeddings_.end() || rit == relationCoeffs_.end()) continue;

        std::vector<std::vector<float>> Wr(D, std::vector<float>(H, 0.0f));
        for (int b = 0; b < config_.numBasis; b++) {
            float coeff = rit->second[b];
            for (int i = 0; i < D; i++) {
                for (int j = 0; j < H; j++) {
                    Wr[i][j] += coeff * basisMatrices_[b][i][j];
                }
            }
        }

        auto msg = matVecMul(Wr, nit->second);
        for (int j = 0; j < H && j < static_cast<int>(msg.size()); j++) {
            aggregated[j] += msg[j];
        }
    }

    // 归一化 + 自连接 + ReLU
    int degree = static_cast<int>(neighbors.size()) + static_cast<int>(inNeighbors.size()) + 1;
    if (degree > 0) {
        float invDegree = 1.0f / static_cast<float>(degree);
        for (auto& v : aggregated) v *= invDegree;
    }

    // 加上自连接
    for (int i = 0; i < D && i < H; i++) {
        aggregated[i] += (i < static_cast<int>(entityEmb.size())) ? entityEmb[i] : 0.0f;
    }

    for (auto& v : aggregated) v = relu(v);

    return aggregated;
}

std::vector<float> RGCNEmbedding::encodeEntity(const String& entityId, const std::vector<Triple>& graph) const {
    if (!initialized_) return {};

    auto it = entityEmbeddings_.find(entityId);
    if (it == entityEmbeddings_.end()) return {};

    std::vector<float> emb = it->second;

    for (int layer = 0; layer < config_.numLayers; layer++) {
        auto outIt = adjacencyOut_.find(entityId);
        auto inIt = adjacencyIn_.find(entityId);

        std::vector<AdjacencyEntry> outNeighbors = (outIt != adjacencyOut_.end()) ? outIt->second : std::vector<AdjacencyEntry>();
        std::vector<AdjacencyEntry> inNeighbors = (inIt != adjacencyIn_.end()) ? inIt->second : std::vector<AdjacencyEntry>();

        emb = messagePassingLayer(emb, outNeighbors, inNeighbors);
    }

    return emb;
}

float RGCNEmbedding::scoreTriple(
    const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t
) const {
    // DistMult-style bilinear: score = h^T * diag(r) * t = sum(h_i * r_i * t_i)
    float sum = 0.0f;
    int D = std::min({static_cast<int>(h.size()), static_cast<int>(r.size()), static_cast<int>(t.size())});
    for (int i = 0; i < D; i++) {
        sum += h[i] * r[i] * t[i];
    }
    return sum;
}

std::vector<float> RGCNEmbedding::negativeSample(const String& entityId, const std::vector<Triple>& triples) {
    std::uniform_int_distribution<size_t> dist(0, entityIds_.size() - 1);
    auto it = entityIds_.begin();
    std::advance(it, dist(rng_));
    auto eit = entityEmbeddings_.find(*it);
    return (eit != entityEmbeddings_.end()) ? eit->second : std::vector<float>(config_.entityDimension, 0.0f);
}

void RGCNEmbedding::applyGradient(
    const std::vector<float>& h, const std::vector<float>& r,
    const std::vector<float>& t, float gradient, float lr
) {
    // 简化梯度: 对关系系数更新
    // d(score)/d(a_rb) = sum_ij d(W_r[i][j])/d(a_rb) * (h_i * t_j)
    // W_r[i][j] = sum_b a_rb * V_b[i][j], dW/d(a_rb) = V_b[i][j]
    int D = std::min(static_cast<int>(h.size()), config_.entityDimension);
    int H = std::min(static_cast<int>(t.size()), config_.hiddenDimension);

    for (auto& [relId, coeffs] : relationCoeffs_) {
        for (int b = 0; b < config_.numBasis; b++) {
            float gradSum = 0.0f;
            for (int i = 0; i < D; i++) {
                for (int j = 0; j < H; j++) {
                    gradSum += basisMatrices_[b][i][j] * h[i] * t[j] * r[j];
                }
            }
            coeffs[b] -= lr * gradient * gradSum;
        }
    }

    // L2 正则化
    for (auto& [relId, coeffs] : relationCoeffs_) {
        for (auto& c : coeffs) {
            c -= lr * config_.l2Regularization * c;
        }
    }
}

void RGCNEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    trainingTriples_ = triples;
    buildAdjacency(triples);

    if (!initialized_) {
        initialize(static_cast<int>(entityIds_.size()), static_cast<int>(relationIds_.size()));
    }

    int numTriples = static_cast<int>(triples.size());

    for (int epoch = 0; epoch < epochs; epoch++) {
        float totalLoss = 0.0f;

        // 打乱训练数据
        std::vector<int> indices(numTriples);
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng_);

        for (int idx : indices) {
            const auto& triple = triples[idx];

            // GNN 编码头实体和尾实体
            auto hEmb = encodeEntity(triple.subject, triples);
            auto tEmb = encodeEntity(triple.object, triples);

            // 关系嵌入 (基系数 → 向量)
            auto rCoeffIt = relationCoeffs_.find(triple.predicate);
            if (rCoeffIt == relationCoeffs_.end()) continue;

            // DistMult 关系嵌入: 用基系数向量直接作为关系向量
            std::vector<float> rEmb(rCoeffIt->second.begin(), rCoeffIt->second.end());
            rEmb.resize(std::max(static_cast<int>(rEmb.size()), config_.entityDimension), 0.0f);

            // 正样本得分
            float posScore = scoreTriple(hEmb, rEmb, tEmb);

            // 负采样
            for (int neg = 0; neg < config_.negativeSamples; neg++) {
                auto negEmb = negativeSample(triple.object, triples);
                float negScore = scoreTriple(hEmb, rEmb, negEmb);

                // Margin ranking loss
                float loss = std::max(0.0f, -posScore + negScore + config_.margin);
                totalLoss += loss;

                if (loss > 0.0f) {
                    float gradient = -1.0f;  // 对正样本梯度
                    applyGradient(hEmb, rEmb, tEmb, gradient, learningRate);
                }
            }
        }

        // Dropout 模拟: 随机置零部分嵌入
        if (config_.dropout > 0.0f) {
            std::bernoulli_distribution dropDist(config_.dropout);
            for (auto& [id, emb] : entityEmbeddings_) {
                for (auto& v : emb) {
                    if (dropDist(rng_)) v = 0.0f;
                }
            }
        }
    }
}

std::vector<float> RGCNEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    return it != entityEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<float> RGCNEmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationCoeffs_.find(relationId);
    return it != relationCoeffs_.end() ? it->second : std::vector<float>();
}

void RGCNEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
    entityIds_.insert(entityId);
    initialized_ = true;
}

void RGCNEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationCoeffs_[relationId] = embedding;
    relationIds_.insert(relationId);
    initialized_ = true;
}

std::vector<std::pair<String, float>> RGCNEmbedding::predictTail(
    const String& subject, const String& relation, int topK
) const {
    if (!initialized_) return {};

    auto hEmb = encodeEntity(subject, trainingTriples_);
    if (hEmb.empty()) return {};

    auto rIt = relationCoeffs_.find(relation);
    if (rIt == relationCoeffs_.end()) return {};

    std::vector<float> rEmb(rIt->second.begin(), rIt->second.end());
    rEmb.resize(std::max(static_cast<int>(rEmb.size()), config_.entityDimension), 0.0f);

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, tEmb] : entityEmbeddings_) {
        auto tEncoded = encodeEntity(entityId, trainingTriples_);
        float s = scoreTriple(hEmb, rEmb, tEncoded.empty() ? tEmb : tEncoded);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> RGCNEmbedding::predictHead(
    const String& object, const String& relation, int topK
) const {
    if (!initialized_) return {};

    auto tEmb = encodeEntity(object, trainingTriples_);
    if (tEmb.empty()) return {};

    auto rIt = relationCoeffs_.find(relation);
    if (rIt == relationCoeffs_.end()) return {};

    std::vector<float> rEmb(rIt->second.begin(), rIt->second.end());
    rEmb.resize(std::max(static_cast<int>(rEmb.size()), config_.entityDimension), 0.0f);

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, hEmb] : entityEmbeddings_) {
        auto hEncoded = encodeEntity(entityId, trainingTriples_);
        float s = scoreTriple(hEncoded.empty() ? hEmb : hEncoded, rEmb, tEmb);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> RGCNEmbedding::predictRelation(
    const String& subject, const String& object, int topK
) const {
    if (!initialized_) return {};

    auto hEmb = encodeEntity(subject, trainingTriples_);
    auto tEmb = encodeEntity(object, trainingTriples_);
    if (hEmb.empty() || tEmb.empty()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [relId, coeffs] : relationCoeffs_) {
        std::vector<float> rEmb(coeffs.begin(), coeffs.end());
        rEmb.resize(std::max(static_cast<int>(rEmb.size()), config_.entityDimension), 0.0f);
        float s = scoreTriple(hEmb, rEmb, tEmb);
        scores.push_back({relId, s});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::vector<float>> RGCNEmbedding::getRelationWeights(const String& relationId) const {
    auto rit = relationCoeffs_.find(relationId);
    if (rit == relationCoeffs_.end()) return {};

    int D = config_.entityDimension;
    int H = config_.hiddenDimension;
    std::vector<std::vector<float>> Wr(D, std::vector<float>(H, 0.0f));

    for (int b = 0; b < config_.numBasis; b++) {
        float coeff = rit->second[b];
        for (int i = 0; i < D; i++) {
            for (int j = 0; j < H; j++) {
                Wr[i][j] += coeff * basisMatrices_[b][i][j];
            }
        }
    }

    return Wr;
}

Json RGCNEmbedding::getStats() const {
    Json j;
    j["model"] = "RGCN";
    j["entityDimension"] = config_.entityDimension;
    j["hiddenDimension"] = config_.hiddenDimension;
    j["numLayers"] = config_.numLayers;
    j["numBasis"] = config_.numBasis;
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationCoeffs_.size();
    j["initialized"] = initialized_;
    return j;
}

float RGCNEmbedding::scoreTriple(const Triple& triple) const {
    auto hEmb = encodeEntity(triple.subject, trainingTriples_);
    auto tEmb = encodeEntity(triple.object, trainingTriples_);
    auto rIt = relationCoeffs_.find(triple.predicate);
    if (hEmb.empty() || tEmb.empty() || rIt == relationCoeffs_.end()) return 0.0f;
    std::vector<float> rEmb(rIt->second.begin(), rIt->second.end());
    rEmb.resize(std::max(static_cast<int>(rEmb.size()), config_.entityDimension), 0.0f);
    return scoreTriple(hEmb, rEmb, tEmb);
}

std::vector<std::pair<String, float>> RGCNEmbedding::findSimilarEntities(
    const String& entityId, int topK) const {
    auto target = getEntityEmbedding(entityId);
    if (target.empty()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityEmbeddings_) {
        if (id == entityId) continue;
        float dot = 0.0f, na = 0.0f, nb = 0.0f;
        for (size_t i = 0; i < target.size() && i < emb.size(); ++i) {
            dot += target[i] * emb[i];
            na += target[i] * target[i];
            nb += emb[i] * emb[i];
        }
        float denom = std::sqrt(na) * std::sqrt(nb);
        float sim = denom < 1e-10f ? 0.0f : dot / denom;
        scores.push_back({id, sim});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

bool RGCNEmbedding::save(const String& path) const {
    (void)path;
    return false;
}

bool RGCNEmbedding::load(const String& path) {
    (void)path;
    return false;
}

std::vector<float> RGCNEmbedding::matVecMul(
    const std::vector<std::vector<float>>& matrix, const std::vector<float>& vec
) {
    if (matrix.empty() || vec.empty()) return {};
    int rows = static_cast<int>(matrix.size());
    int cols = static_cast<int>(matrix[0].size());
    std::vector<float> result(rows, 0.0f);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols && j < static_cast<int>(vec.size()); j++) {
            result[i] += matrix[i][j] * vec[j];
        }
    }
    return result;
}

// ============================================================================
// CompGCNEmbedding 实现
// ============================================================================

CompGCNEmbedding::CompGCNEmbedding(const Config& config) : config_(config) {}

void CompGCNEmbedding::initialize(int numEntities, int numRelations) {
    int D = config_.dimension;
    std::normal_distribution<float> dist(0.0f, 1.0f / std::sqrt(static_cast<float>(D)));

    for (const auto& id : entityIds_) {
        if (entityEmbeddings_.find(id) == entityEmbeddings_.end()) {
            std::vector<float> emb(D);
            for (auto& v : emb) v = dist(rng_);
            entityEmbeddings_[id] = std::move(emb);
        }
    }

    for (const auto& id : relationIds_) {
        if (relationEmbeddings_.find(id) == relationEmbeddings_.end()) {
            std::vector<float> emb(D);
            for (auto& v : emb) v = dist(rng_);
            relationEmbeddings_[id] = std::move(emb);
        }
    }

    // 初始化权重矩阵 (每层)
    W_O_.resize(config_.numLayers);
    W_I_.resize(config_.numLayers);
    W_R_.resize(config_.numLayers);

    for (int l = 0; l < config_.numLayers; l++) {
        W_O_[l].resize(D, std::vector<float>(D));
        W_I_[l].resize(D, std::vector<float>(D));
        W_R_[l].resize(D, std::vector<float>(D));

        std::normal_distribution<float> wdist(0.0f, 1.0f / std::sqrt(static_cast<float>(D)));
        for (int i = 0; i < D; i++) {
            for (int j = 0; j < D; j++) {
                W_O_[l][i][j] = wdist(rng_);
                W_I_[l][i][j] = wdist(rng_);
                W_R_[l][i][j] = wdist(rng_);
            }
        }
    }

    initialized_ = true;
}

void CompGCNEmbedding::buildGraph(const std::vector<Triple>& triples) {
    edges_.clear();
    outEdges_.clear();
    inEdges_.clear();
    entityIds_.clear();
    relationIds_.clear();

    for (const auto& t : triples) {
        entityIds_.insert(t.subject);
        entityIds_.insert(t.object);
        relationIds_.insert(t.predicate);

        size_t edgeIdx = edges_.size();
        edges_.push_back({t.subject, t.predicate, t.object});
        outEdges_[t.subject].push_back(edgeIdx);
        inEdges_[t.object].push_back(edgeIdx);
    }
}

// 组合操作实现
std::vector<float> CompGCNEmbedding::composeSubtract(
    const std::vector<float>& e, const std::vector<float>& r
) const {
    int D = std::min(static_cast<int>(e.size()), static_cast<int>(r.size()));
    std::vector<float> result(D);
    for (int i = 0; i < D; i++) result[i] = e[i] - r[i];
    return result;
}

std::vector<float> CompGCNEmbedding::composeMultiply(
    const std::vector<float>& e, const std::vector<float>& r
) const {
    int D = std::min(static_cast<int>(e.size()), static_cast<int>(r.size()));
    std::vector<float> result(D);
    for (int i = 0; i < D; i++) result[i] = e[i] * r[i];
    return result;
}

std::vector<float> CompGCNEmbedding::composeCircularCorr(
    const std::vector<float>& e, const std::vector<float>& r
) const {
    // 圆形相关: (e ⊛ r)_k = sum_j e_j * r_{(j+k) mod D}
    int D = std::min(static_cast<int>(e.size()), static_cast<int>(r.size()));
    std::vector<float> result(D, 0.0f);
    for (int k = 0; k < D; k++) {
        for (int j = 0; j < D; j++) {
            result[k] += e[j] * r[(j + k) % D];
        }
    }
    return result;
}

std::vector<float> CompGCNEmbedding::compose(
    const std::vector<float>& entity, const std::vector<float>& relation
) const {
    switch (config_.compositionOp) {
        case CompositionOp::Subtract: return composeSubtract(entity, relation);
        case CompositionOp::Multiply: return composeMultiply(entity, relation);
        case CompositionOp::CircularCorr: return composeCircularCorr(entity, relation);
    }
    return composeMultiply(entity, relation);
}

std::vector<float> CompGCNEmbedding::forward(const String& entityId, int layer) const {
    if (layer < 0 || layer >= config_.numLayers) return {};

    int D = config_.dimension;
    std::vector<float> aggregated(D, 0.0f);

    // 出边: e_v' += W_O * φ(e_u, e_r) / (2|N(v)|)
    auto outIt = outEdges_.find(entityId);
    if (outIt != outEdges_.end()) {
        for (size_t edgeIdx : outIt->second) {
            const auto& edge = edges_[edgeIdx];
            auto srcIt = entityEmbeddings_.find(edge.source);
            auto relIt = relationEmbeddings_.find(edge.relation);
            if (srcIt == entityEmbeddings_.end() || relIt == relationEmbeddings_.end()) continue;

            auto composed = compose(srcIt->second, relIt->second);
            auto msg = matVecMul(W_O_[layer], composed);

            for (int i = 0; i < D && i < static_cast<int>(msg.size()); i++) {
                aggregated[i] += msg[i];
            }
        }
    }

    // 入边: e_v' += W_I * φ(e_u, e_r_inv) / (2|N(v)|)
    auto inIt = inEdges_.find(entityId);
    if (inIt != inEdges_.end()) {
        for (size_t edgeIdx : inIt->second) {
            const auto& edge = edges_[edgeIdx];
            auto tgtIt = entityEmbeddings_.find(edge.target);
            auto relIt = relationEmbeddings_.find(edge.relation);
            if (tgtIt == entityEmbeddings_.end() || relIt == relationEmbeddings_.end()) continue;

            // 逆关系: 取负
            std::vector<float> invRel(D);
            for (int i = 0; i < D && i < static_cast<int>(relIt->second.size()); i++) {
                invRel[i] = -relIt->second[i];
            }

            auto composed = compose(tgtIt->second, invRel);
            auto msg = matVecMul(W_I_[layer], composed);

            for (int i = 0; i < D && i < static_cast<int>(msg.size()); i++) {
                aggregated[i] += msg[i];
            }
        }
    }

    // 归一化
    int degree = 0;
    if (outIt != outEdges_.end()) degree += static_cast<int>(outIt->second.size());
    if (inIt != inEdges_.end()) degree += static_cast<int>(inIt->second.size());
    if (degree > 0) {
        float invDegree = 1.0f / static_cast<float>(degree);
        for (auto& v : aggregated) v *= invDegree;
    }

    // 激活
    for (auto& v : aggregated) v = relu(v);

    return aggregated;
}

float CompGCNEmbedding::scoreTriple(
    const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t
) const {
    // DistMult-style bilinear
    float sum = 0.0f;
    int D = std::min({static_cast<int>(h.size()), static_cast<int>(r.size()), static_cast<int>(t.size())});
    for (int i = 0; i < D; i++) {
        sum += h[i] * r[i] * t[i];
    }
    return sum;
}

std::vector<float> CompGCNEmbedding::negativeSample(const String& entityId) {
    std::uniform_int_distribution<size_t> dist(0, entityIds_.size() - 1);
    auto it = entityIds_.begin();
    std::advance(it, dist(rng_));
    auto eit = entityEmbeddings_.find(*it);
    return (eit != entityEmbeddings_.end()) ? eit->second : std::vector<float>(config_.dimension, 0.0f);
}

void CompGCNEmbedding::trainStep(float lr) {
    if (trainingTriples_.empty()) return;

    // 随机采样一个正样本
    std::uniform_int_distribution<size_t> dist(0, trainingTriples_.size() - 1);
    const auto& triple = trainingTriples_[dist(rng_)];

    // GNN 前向传播获取编码
    auto hEmb = entityEmbeddings_.find(triple.subject);
    auto rEmb = relationEmbeddings_.find(triple.predicate);
    auto tEmb = entityEmbeddings_.find(triple.object);

    if (hEmb == entityEmbeddings_.end() || rEmb == relationEmbeddings_.end() ||
        tEmb == entityEmbeddings_.end()) return;

    // 多层 GNN 编码
    std::vector<float> hEncoded = hEmb->second;
    std::vector<float> tEncoded = tEmb->second;
    std::vector<float> rEncoded = rEmb->second;

    for (int layer = 0; layer < config_.numLayers; layer++) {
        hEncoded = forward(triple.subject, layer);
        tEncoded = forward(triple.object, layer);

        // 关系更新: e_r' = W_R * φ(e_u, e_v)
        auto composed = compose(hEmb->second, tEmb->second);
        auto rMsg = matVecMul(W_R_[layer], composed);
        int D = config_.dimension;
        for (int i = 0; i < D && i < static_cast<int>(rMsg.size()); i++) {
            rEncoded[i] = rMsg[i];
        }
        for (auto& v : rEncoded) v = relu(v);
    }

    // 正样本得分
    float posScore = scoreTriple(hEncoded, rEncoded, tEncoded);

    // 负采样
    for (int neg = 0; neg < config_.negativeSamples; neg++) {
        auto negEmb = negativeSample(triple.object);
        float negScore = scoreTriple(hEncoded, rEncoded, negEmb);

        // Margin ranking loss
        float loss = std::max(0.0f, -posScore + negScore + config_.margin);

        if (loss > 0.0f) {
            // 简化梯度更新: 直接更新嵌入
            int D = config_.dimension;
            for (int i = 0; i < D && i < static_cast<int>(hEmb->second.size()); i++) {
                hEmb->second[i] += lr * rEncoded[i] * tEncoded[i];
                tEmb->second[i] += lr * hEncoded[i] * rEncoded[i];
                rEmb->second[i] += lr * hEncoded[i] * tEncoded[i];
            }

            // L2 正则化
            for (int i = 0; i < D && i < static_cast<int>(hEmb->second.size()); i++) {
                hEmb->second[i] -= lr * config_.l2Regularization * hEmb->second[i];
                tEmb->second[i] -= lr * config_.l2Regularization * tEmb->second[i];
                rEmb->second[i] -= lr * config_.l2Regularization * rEmb->second[i];
            }
        }
    }
}

void CompGCNEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    trainingTriples_ = triples;
    buildGraph(triples);

    if (!initialized_) {
        initialize(static_cast<int>(entityIds_.size()), static_cast<int>(relationIds_.size()));
    }

    for (int epoch = 0; epoch < epochs; epoch++) {
        for (int step = 0; step < static_cast<int>(triples.size()); step++) {
            trainStep(learningRate);
        }
    }
}

std::vector<float> CompGCNEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    return it != entityEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<float> CompGCNEmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationEmbeddings_.find(relationId);
    return it != relationEmbeddings_.end() ? it->second : std::vector<float>();
}

void CompGCNEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
    entityIds_.insert(entityId);
    initialized_ = true;
}

void CompGCNEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationEmbeddings_[relationId] = embedding;
    relationIds_.insert(relationId);
    initialized_ = true;
}

std::vector<std::pair<String, float>> CompGCNEmbedding::predictTail(
    const String& subject, const String& relation, int topK
) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto rIt = relationEmbeddings_.find(relation);
    if (hIt == entityEmbeddings_.end() || rIt == relationEmbeddings_.end()) return {};

    // GNN 编码
    std::vector<float> hEmb = hIt->second;
    std::vector<float> rEmb = rIt->second;
    for (int layer = 0; layer < config_.numLayers; layer++) {
        auto encoded = forward(subject, layer);
        if (!encoded.empty()) hEmb = encoded;
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, tEmb] : entityEmbeddings_) {
        auto tEncoded = tEmb;
        for (int layer = 0; layer < config_.numLayers; layer++) {
            auto encoded = forward(entityId, layer);
            if (!encoded.empty()) tEncoded = encoded;
        }
        float s = scoreTriple(hEmb, rEmb, tEncoded);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> CompGCNEmbedding::predictHead(
    const String& object, const String& relation, int topK
) const {
    if (!initialized_) return {};

    auto tIt = entityEmbeddings_.find(object);
    auto rIt = relationEmbeddings_.find(relation);
    if (tIt == entityEmbeddings_.end() || rIt == relationEmbeddings_.end()) return {};

    std::vector<float> tEmb = tIt->second;
    std::vector<float> rEmb = rIt->second;
    for (int layer = 0; layer < config_.numLayers; layer++) {
        auto encoded = forward(object, layer);
        if (!encoded.empty()) tEmb = encoded;
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, hEmb] : entityEmbeddings_) {
        auto hEncoded = hEmb;
        for (int layer = 0; layer < config_.numLayers; layer++) {
            auto encoded = forward(entityId, layer);
            if (!encoded.empty()) hEncoded = encoded;
        }
        float s = scoreTriple(hEncoded, rEmb, tEmb);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> CompGCNEmbedding::predictRelation(
    const String& subject, const String& object, int topK
) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto tIt = entityEmbeddings_.find(object);
    if (hIt == entityEmbeddings_.end() || tIt == entityEmbeddings_.end()) return {};

    std::vector<float> hEmb = hIt->second;
    std::vector<float> tEmb = tIt->second;
    for (int layer = 0; layer < config_.numLayers; layer++) {
        auto hEnc = forward(subject, layer);
        auto tEnc = forward(object, layer);
        if (!hEnc.empty()) hEmb = hEnc;
        if (!tEnc.empty()) tEmb = tEnc;
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [relId, rEmb] : relationEmbeddings_) {
        float s = scoreTriple(hEmb, rEmb, tEmb);
        scores.push_back({relId, s});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

Json CompGCNEmbedding::getStats() const {
    Json j;
    j["model"] = "CompGCN";
    j["dimension"] = config_.dimension;
    j["numLayers"] = config_.numLayers;
    j["compositionOp"] = static_cast<int>(config_.compositionOp);
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationEmbeddings_.size();
    j["initialized"] = initialized_;
    return j;
}

float CompGCNEmbedding::scoreTriple(const Triple& triple) const {
    auto hEmb = entityEmbeddings_.find(triple.subject);
    auto rEmb = relationEmbeddings_.find(triple.predicate);
    auto tEmb = entityEmbeddings_.find(triple.object);
    if (hEmb == entityEmbeddings_.end() || rEmb == relationEmbeddings_.end() ||
        tEmb == entityEmbeddings_.end()) return 0.0f;
    return scoreTriple(hEmb->second, rEmb->second, tEmb->second);
}

std::vector<std::pair<String, float>> CompGCNEmbedding::findSimilarEntities(
    const String& entityId, int topK) const {
    auto target = getEntityEmbedding(entityId);
    if (target.empty()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityEmbeddings_) {
        if (id == entityId) continue;
        float dot = 0.0f, na = 0.0f, nb = 0.0f;
        for (size_t i = 0; i < target.size() && i < emb.size(); ++i) {
            dot += target[i] * emb[i];
            na += target[i] * target[i];
            nb += emb[i] * emb[i];
        }
        float denom = std::sqrt(na) * std::sqrt(nb);
        float sim = denom < 1e-10f ? 0.0f : dot / denom;
        scores.push_back({id, sim});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

bool CompGCNEmbedding::save(const String& path) const {
    (void)path;
    return false;
}

bool CompGCNEmbedding::load(const String& path) {
    (void)path;
    return false;
}

std::vector<float> CompGCNEmbedding::matVecMul(
    const std::vector<std::vector<float>>& matrix, const std::vector<float>& vec
) {
    if (matrix.empty() || vec.empty()) return {};
    int rows = static_cast<int>(matrix.size());
    int cols = static_cast<int>(matrix[0].size());
    std::vector<float> result(rows, 0.0f);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols && j < static_cast<int>(vec.size()); j++) {
            result[i] += matrix[i][j] * vec[j];
        }
    }
    return result;
}

} // namespace ontology
