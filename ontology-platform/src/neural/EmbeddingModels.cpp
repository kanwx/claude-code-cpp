#include <ontology/Neural.hpp>
#include <random>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace ontology {

// ============================================================================
// TransR 嵌入实现
// ============================================================================

TransREmbedding::TransREmbedding(int entityDim, int relationDim)
    : entityDim_(entityDim), relationDim_(relationDim) {}

void TransREmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    // 初始化嵌入
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dist(0.0f, 0.1f);

    for (const auto& t : triples) {
        if (entityEmbeddings_.find(t.subject) == entityEmbeddings_.end()) {
            std::vector<float> emb(entityDim_);
            for (auto& v : emb) v = dist(gen);
            entityEmbeddings_[t.subject] = std::move(emb);
        }
        if (entityEmbeddings_.find(t.object) == entityEmbeddings_.end()) {
            std::vector<float> emb(entityDim_);
            for (auto& v : emb) v = dist(gen);
            entityEmbeddings_[t.object] = std::move(emb);
        }
        if (relationEmbeddings_.find(t.predicate) == relationEmbeddings_.end()) {
            std::vector<float> emb(relationDim_);
            for (auto& v : emb) v = dist(gen);
            relationEmbeddings_[t.predicate] = std::move(emb);

            // 初始化投影矩阵 (relationDim x entityDim)
            std::vector<std::vector<float>> matrix(relationDim_, std::vector<float>(entityDim_));
            for (auto& row : matrix) {
                for (auto& v : row) v = dist(gen);
            }
            projectionMatrices_[t.predicate] = std::move(matrix);
        }
    }

    initialized_ = true;

    // 训练循环
    for (int epoch = 0; epoch < epochs; epoch++) {
        float totalLoss = 0.0f;

        for (const auto& t : triples) {
            auto& h = entityEmbeddings_[t.subject];
            auto& r = relationEmbeddings_[t.predicate];
            auto& t_emb = entityEmbeddings_[t.object];
            auto& Mr = projectionMatrices_[t.predicate];

            // 正样本得分
            float posScore = score(h, r, t_emb, Mr);

            // 负采样 - 替换尾实体
            String negTail = t.object;
            for (const auto& [id, _] : entityEmbeddings_) {
                if (id != t.object) {
                    negTail = id;
                    break;
                }
            }
            float negScore = score(h, r, entityEmbeddings_[negTail], Mr);

            // Margin loss
            float loss = std::max(0.0f, posScore - negScore + 1.0f);
            totalLoss += loss;

            if (loss > 0) {
                // 梯度下降更新
                auto h_proj = project(h, Mr);
                auto t_proj = project(t_emb, Mr);

                for (int i = 0; i < relationDim_; i++) {
                    float grad = learningRate * (h_proj[i] + r[i] - t_proj[i]);
                    r[i] -= grad;
                }
            }
        }

        // 每100轮打印一次损失
        if (epoch % 100 == 0) {
            // printf("TransR Epoch %d, Loss: %.4f\n", epoch, totalLoss / triples.size());
        }
    }
}

std::vector<float> TransREmbedding::project(const std::vector<float>& entity,
                                             const std::vector<std::vector<float>>& matrix) const {
    std::vector<float> result(relationDim_, 0.0f);
    for (int i = 0; i < relationDim_; i++) {
        for (int j = 0; j < entityDim_; j++) {
            result[i] += matrix[i][j] * entity[j];
        }
    }
    return result;
}

float TransREmbedding::score(const std::vector<float>& h, const std::vector<float>& r,
                              const std::vector<float>& t, const std::vector<std::vector<float>>& Mr) const {
    auto h_proj = project(h, Mr);
    auto t_proj = project(t, Mr);

    float sum = 0.0f;
    for (int i = 0; i < relationDim_; i++) {
        float diff = h_proj[i] + r[i] - t_proj[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

std::vector<float> TransREmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    return it != entityEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<float> TransREmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationEmbeddings_.find(relationId);
    return it != relationEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<std::vector<float>> TransREmbedding::getProjectionMatrix(const String& relationId) const {
    auto it = projectionMatrices_.find(relationId);
    return it != projectionMatrices_.end() ? it->second : std::vector<std::vector<float>>();
}

std::vector<std::pair<String, float>> TransREmbedding::predictTail(const String& subject,
                                                                    const String& relation,
                                                                    int topK) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto rIt = relationEmbeddings_.find(relation);
    auto mIt = projectionMatrices_.find(relation);
    if (hIt == entityEmbeddings_.end() || rIt == relationEmbeddings_.end() || mIt == projectionMatrices_.end()) {
        return {};
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, t_emb] : entityEmbeddings_) {
        float s = score(hIt->second, rIt->second, t_emb, mIt->second);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if ((int)scores.size() > topK) {
        scores.resize(topK);
    }

    return scores;
}

std::vector<std::pair<String, float>> TransREmbedding::predictHead(const String& object,
                                                                    const String& relation,
                                                                    int topK) const {
    if (!initialized_) return {};

    auto tIt = entityEmbeddings_.find(object);
    auto rIt = relationEmbeddings_.find(relation);
    auto mIt = projectionMatrices_.find(relation);
    if (tIt == entityEmbeddings_.end() || rIt == relationEmbeddings_.end() || mIt == projectionMatrices_.end()) {
        return {};
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, h_emb] : entityEmbeddings_) {
        float s = score(h_emb, rIt->second, tIt->second, mIt->second);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if ((int)scores.size() > topK) {
        scores.resize(topK);
    }

    return scores;
}

std::vector<std::pair<String, float>> TransREmbedding::predictRelation(const String& subject,
                                                                        const String& object,
                                                                        int topK) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto tIt = entityEmbeddings_.find(object);
    if (hIt == entityEmbeddings_.end() || tIt == entityEmbeddings_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [relId, r_emb] : relationEmbeddings_) {
        auto mIt = projectionMatrices_.find(relId);
        if (mIt == projectionMatrices_.end()) continue;
        float s = score(hIt->second, r_emb, tIt->second, mIt->second);
        scores.push_back({relId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

void TransREmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
    initialized_ = true;
}

void TransREmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationEmbeddings_[relationId] = embedding;
    initialized_ = true;
}

Json TransREmbedding::getStats() const {
    Json j;
    j["model"] = "TransR";
    j["entityDim"] = entityDim_;
    j["relationDim"] = relationDim_;
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationEmbeddings_.size();
    j["initialized"] = initialized_;
    return j;
}

float TransREmbedding::scoreTriple(const Triple& triple) const {
    auto h = getEntityEmbedding(triple.subject);
    auto r = getRelationEmbedding(triple.predicate);
    auto t = getEntityEmbedding(triple.object);
    auto mIt = projectionMatrices_.find(triple.predicate);
    if (h.empty() || r.empty() || t.empty() || mIt == projectionMatrices_.end()) return 0.0f;
    return -score(h, r, t, mIt->second);  // negate distance so higher = better
}

std::vector<std::pair<String, float>> TransREmbedding::findSimilarEntities(
    const String& entityId, int topK) const {
    auto target = getEntityEmbedding(entityId);
    if (target.empty()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityEmbeddings_) {
        if (id == entityId) continue;
        // Cosine similarity
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

bool TransREmbedding::save(const String& path) const {
    // Stub: persistent storage not yet implemented for TransR
    (void)path;
    return false;
}

bool TransREmbedding::load(const String& path) {
    // Stub: persistent storage not yet implemented for TransR
    (void)path;
    return false;
}

// ============================================================================
// DistMult 嵌入实现
// ============================================================================

DistMultEmbedding::DistMultEmbedding(int dimension) : dimension_(dimension) {}

void DistMultEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    // 初始化
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dist(0.0f, 0.1f);

    for (const auto& t : triples) {
        if (entityEmbeddings_.find(t.subject) == entityEmbeddings_.end()) {
            std::vector<float> emb(dimension_);
            for (auto& v : emb) v = dist(gen);
            entityEmbeddings_[t.subject] = std::move(emb);
        }
        if (entityEmbeddings_.find(t.object) == entityEmbeddings_.end()) {
            std::vector<float> emb(dimension_);
            for (auto& v : emb) v = dist(gen);
            entityEmbeddings_[t.object] = std::move(emb);
        }
        if (relationEmbeddings_.find(t.predicate) == relationEmbeddings_.end()) {
            std::vector<float> emb(dimension_);
            for (auto& v : emb) v = dist(gen);
            relationEmbeddings_[t.predicate] = std::move(emb);
        }
    }

    initialized_ = true;

    // 训练
    for (int epoch = 0; epoch < epochs; epoch++) {
        float totalLoss = 0.0f;

        for (const auto& t : triples) {
            auto& h = entityEmbeddings_[t.subject];
            auto& r = relationEmbeddings_[t.predicate];
            auto& t_emb = entityEmbeddings_[t.object];

            float posScore = score(h, r, t_emb);

            // 负采样
            String negEntity = t.object;
            for (const auto& [id, _] : entityEmbeddings_) {
                if (id != t.object && id != t.subject) {
                    negEntity = id;
                    break;
                }
            }
            float negScore = score(h, r, entityEmbeddings_[negEntity]);

            float loss = std::max(0.0f, -posScore + negScore + 1.0f);
            totalLoss += loss;

            if (loss > 0) {
                // 更新
                for (int i = 0; i < dimension_; i++) {
                    h[i] += learningRate * r[i] * t_emb[i];
                    r[i] += learningRate * h[i] * t_emb[i];
                    t_emb[i] += learningRate * h[i] * r[i];
                }
            }
        }
    }
}

float DistMultEmbedding::score(const std::vector<float>& h, const std::vector<float>& r,
                                const std::vector<float>& t) const {
    float sum = 0.0f;
    for (int i = 0; i < dimension_; i++) {
        sum += h[i] * r[i] * t[i];
    }
    return sum;
}

std::vector<float> DistMultEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    return it != entityEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<float> DistMultEmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationEmbeddings_.find(relationId);
    return it != relationEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<std::pair<String, float>> DistMultEmbedding::predictTail(const String& subject,
                                                                      const String& relation,
                                                                      int topK) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto rIt = relationEmbeddings_.find(relation);
    if (hIt == entityEmbeddings_.end() || rIt == relationEmbeddings_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, t_emb] : entityEmbeddings_) {
        float s = score(hIt->second, rIt->second, t_emb);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> DistMultEmbedding::predictHead(const String& object,
                                                                      const String& relation,
                                                                      int topK) const {
    return predictTail(object, relation, topK); // DistMult is symmetric
}

std::vector<std::pair<String, float>> DistMultEmbedding::predictRelation(const String& subject,
                                                                          const String& object,
                                                                          int topK) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto tIt = entityEmbeddings_.find(object);
    if (hIt == entityEmbeddings_.end() || tIt == entityEmbeddings_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [relId, r_emb] : relationEmbeddings_) {
        float s = score(hIt->second, r_emb, tIt->second);
        scores.push_back({relId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

void DistMultEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
    initialized_ = true;
}

void DistMultEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationEmbeddings_[relationId] = embedding;
    initialized_ = true;
}

Json DistMultEmbedding::getStats() const {
    Json j;
    j["model"] = "DistMult";
    j["dimension"] = dimension_;
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationEmbeddings_.size();
    j["initialized"] = initialized_;
    return j;
}

float DistMultEmbedding::scoreTriple(const Triple& triple) const {
    auto h = getEntityEmbedding(triple.subject);
    auto r = getRelationEmbedding(triple.predicate);
    auto t = getEntityEmbedding(triple.object);
    if (h.empty() || r.empty() || t.empty()) return 0.0f;
    return score(h, r, t);
}

std::vector<std::pair<String, float>> DistMultEmbedding::findSimilarEntities(
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

bool DistMultEmbedding::save(const String& path) const {
    (void)path;
    return false;
}

bool DistMultEmbedding::load(const String& path) {
    (void)path;
    return false;
}

// ============================================================================
// ComplEx 嵌入实现
// ============================================================================

ComplExEmbedding::ComplExEmbedding(int dimension) : dimension_(dimension) {}

void ComplExEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dist(0.0f, 0.1f);

    auto initEntity = [&](const String& id) {
        if (entityReal_.find(id) == entityReal_.end()) {
            std::vector<float> real(dimension_), imag(dimension_);
            for (int i = 0; i < dimension_; i++) {
                real[i] = dist(gen);
                imag[i] = dist(gen);
            }
            entityReal_[id] = std::move(real);
            entityImag_[id] = std::move(imag);
        }
    };

    auto initRelation = [&](const String& id) {
        if (relationReal_.find(id) == relationReal_.end()) {
            std::vector<float> real(dimension_), imag(dimension_);
            for (int i = 0; i < dimension_; i++) {
                real[i] = dist(gen);
                imag[i] = dist(gen);
            }
            relationReal_[id] = std::move(real);
            relationImag_[id] = std::move(imag);
        }
    };

    for (const auto& t : triples) {
        initEntity(t.subject);
        initEntity(t.object);
        initRelation(t.predicate);
    }

    initialized_ = true;

    for (int epoch = 0; epoch < epochs; epoch++) {
        float totalLoss = 0.0f;

        for (const auto& t : triples) {
            auto& hRe = entityReal_[t.subject];
            auto& hIm = entityImag_[t.subject];
            auto& rRe = relationReal_[t.predicate];
            auto& rIm = relationImag_[t.predicate];
            auto& tRe = entityReal_[t.object];
            auto& tIm = entityImag_[t.object];

            float posScore = score(hRe, hIm, rRe, rIm, tRe, tIm);
            totalLoss += -posScore; // 最大化正样本得分
        }
    }
}

float ComplExEmbedding::score(const std::vector<float>& hRe, const std::vector<float>& hIm,
                               const std::vector<float>& rRe, const std::vector<float>& rIm,
                               const std::vector<float>& tRe, const std::vector<float>& tIm) const {
    // Re(<h, r, conj(t)>) = Re(h*r*conj(t))
    float sum = 0.0f;
    for (int i = 0; i < dimension_; i++) {
        // hr = (hRe + i*hIm) * (rRe + i*rIm)
        float hrRe = hRe[i] * rRe[i] - hIm[i] * rIm[i];
        float hrIm = hRe[i] * rIm[i] + hIm[i] * rRe[i];
        // hr * conj(t) = (hrRe + i*hrIm) * (tRe - i*tIm)
        sum += hrRe * tRe[i] + hrIm * tIm[i];
    }
    return sum;
}

std::vector<float> ComplExEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityReal_.find(entityId);
    return it != entityReal_.end() ? it->second : std::vector<float>();
}

std::vector<float> ComplExEmbedding::getEntityEmbeddingIm(const String& entityId) const {
    auto it = entityImag_.find(entityId);
    return it != entityImag_.end() ? it->second : std::vector<float>();
}

std::vector<float> ComplExEmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationReal_.find(relationId);
    return it != relationReal_.end() ? it->second : std::vector<float>();
}

std::vector<float> ComplExEmbedding::getRelationEmbeddingIm(const String& relationId) const {
    auto it = relationImag_.find(relationId);
    return it != relationImag_.end() ? it->second : std::vector<float>();
}

std::vector<std::pair<String, float>> ComplExEmbedding::predictTail(const String& subject,
                                                                     const String& relation,
                                                                     int topK) const {
    if (!initialized_) return {};

    auto hReIt = entityReal_.find(subject);
    auto hImIt = entityImag_.find(subject);
    auto rReIt = relationReal_.find(relation);
    auto rImIt = relationImag_.find(relation);
    if (hReIt == entityReal_.end() || rReIt == relationReal_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, tRe] : entityReal_) {
        auto tImIt = entityImag_.find(entityId);
        if (tImIt == entityImag_.end()) continue;
        float s = score(hReIt->second, hImIt->second, rReIt->second, rImIt->second, tRe, tImIt->second);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> ComplExEmbedding::predictHead(const String& object,
                                                                     const String& relation,
                                                                     int topK) const {
    return predictTail(object, relation, topK);
}

std::vector<std::pair<String, float>> ComplExEmbedding::predictRelation(const String& subject,
                                                                         const String& object,
                                                                         int topK) const {
    if (!initialized_) return {};

    auto hReIt = entityReal_.find(subject);
    auto hImIt = entityImag_.find(subject);
    auto tReIt = entityReal_.find(object);
    auto tImIt = entityImag_.find(object);
    if (hReIt == entityReal_.end() || tReIt == entityReal_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [relId, rRe] : relationReal_) {
        auto rImIt = relationImag_.find(relId);
        if (rImIt == relationImag_.end()) continue;
        float s = score(hReIt->second, hImIt->second, rRe, rImIt->second, tReIt->second, tImIt->second);
        scores.push_back({relId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

void ComplExEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityReal_[entityId] = embedding;
    // If imaginary part doesn't exist, initialize to zero
    if (entityImag_.find(entityId) == entityImag_.end()) {
        entityImag_[entityId] = std::vector<float>(dimension_, 0.0f);
    }
    initialized_ = true;
}

void ComplExEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationReal_[relationId] = embedding;
    if (relationImag_.find(relationId) == relationImag_.end()) {
        relationImag_[relationId] = std::vector<float>(dimension_, 0.0f);
    }
    initialized_ = true;
}

void ComplExEmbedding::setEmbedding(const String& entityId, const std::vector<float>& real,
                                     const std::vector<float>& imag) {
    entityReal_[entityId] = real;
    entityImag_[entityId] = imag;
    initialized_ = true;
}

Json ComplExEmbedding::getStats() const {
    Json j;
    j["model"] = "ComplEx";
    j["dimension"] = dimension_;
    j["numEntities"] = entityReal_.size();
    j["numRelations"] = relationReal_.size();
    j["initialized"] = initialized_;
    return j;
}

float ComplExEmbedding::scoreTriple(const Triple& triple) const {
    auto hRe = getEntityEmbedding(triple.subject);
    auto hIm = getEntityEmbeddingIm(triple.subject);
    auto rRe = getRelationEmbedding(triple.predicate);
    auto rIm = getRelationEmbeddingIm(triple.predicate);
    auto tRe = getEntityEmbedding(triple.object);
    auto tIm = getEntityEmbeddingIm(triple.object);
    if (hRe.empty() || rRe.empty() || tRe.empty()) return 0.0f;
    if (hIm.empty()) hIm.resize(dimension_, 0.0f);
    if (rIm.empty()) rIm.resize(dimension_, 0.0f);
    if (tIm.empty()) tIm.resize(dimension_, 0.0f);
    return score(hRe, hIm, rRe, rIm, tRe, tIm);
}

std::vector<std::pair<String, float>> ComplExEmbedding::findSimilarEntities(
    const String& entityId, int topK) const {
    auto target = getEntityEmbedding(entityId);
    if (target.empty()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityReal_) {
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

bool ComplExEmbedding::save(const String& path) const {
    (void)path;
    return false;
}

bool ComplExEmbedding::load(const String& path) {
    (void)path;
    return false;
}

// ============================================================================
// RotatE 嵌入实现
// ============================================================================

RotatEEmbedding::RotatEEmbedding(int dimension) : dimension_(dimension) {}

void RotatEEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dist(0.0f, 0.1f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * static_cast<float>(M_PI));

    for (const auto& t : triples) {
        if (entityEmbeddings_.find(t.subject) == entityEmbeddings_.end()) {
            std::vector<float> emb(dimension_);
            for (auto& v : emb) v = dist(gen);
            entityEmbeddings_[t.subject] = std::move(emb);
        }
        if (entityEmbeddings_.find(t.object) == entityEmbeddings_.end()) {
            std::vector<float> emb(dimension_);
            for (auto& v : emb) v = dist(gen);
            entityEmbeddings_[t.object] = std::move(emb);
        }
        if (relationPhases_.find(t.predicate) == relationPhases_.end()) {
            std::vector<float> phase(dimension_);
            for (auto& v : phase) v = phaseDist(gen);
            relationPhases_[t.predicate] = std::move(phase);
        }
    }

    initialized_ = true;

    for (int epoch = 0; epoch < epochs; epoch++) {
        float totalLoss = 0.0f;

        for (const auto& t : triples) {
            auto& h = entityEmbeddings_[t.subject];
            auto& theta = relationPhases_[t.predicate];
            auto& t_emb = entityEmbeddings_[t.object];

            float s = score(h, theta, t_emb);
            totalLoss += s;
        }
    }
}

float RotatEEmbedding::score(const std::vector<float>& h, const std::vector<float>& theta_r,
                              const std::vector<float>& t) const {
    // ||t - h ⊙ r||, where r = e^{i*theta_r}
    // h ⊙ r: 每个维度 h_i * e^{i*theta_i} = h_i * (cos(theta_i) + i*sin(theta_i))
    float sum = 0.0f;
    for (int i = 0; i < dimension_; i++) {
        // 假设h是模长，计算旋转后的期望值
        float rotated = h[i] * std::cos(theta_r[i]);
        float diff = t[i] - rotated;
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

std::vector<float> RotatEEmbedding::rotate(const std::vector<float>& h, const std::vector<float>& theta) const {
    std::vector<float> result(dimension_);
    for (int i = 0; i < dimension_; i++) {
        result[i] = h[i] * std::cos(theta[i]);
    }
    return result;
}

std::vector<float> RotatEEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    return it != entityEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<float> RotatEEmbedding::getRelationEmbedding(const String& relationId) const {
    // For RotatE, the relation embedding is represented by the phase vector
    auto it = relationPhases_.find(relationId);
    return it != relationPhases_.end() ? it->second : std::vector<float>();
}

std::vector<float> RotatEEmbedding::getRelationPhase(const String& relationId) const {
    auto it = relationPhases_.find(relationId);
    return it != relationPhases_.end() ? it->second : std::vector<float>();
}

std::vector<std::pair<String, float>> RotatEEmbedding::predictTail(const String& subject,
                                                                     const String& relation,
                                                                     int topK) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto rIt = relationPhases_.find(relation);
    if (hIt == entityEmbeddings_.end() || rIt == relationPhases_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, t_emb] : entityEmbeddings_) {
        float s = score(hIt->second, rIt->second, t_emb);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> RotatEEmbedding::predictHead(const String& object,
                                                                     const String& relation,
                                                                     int topK) const {
    if (!initialized_) return {};

    auto tIt = entityEmbeddings_.find(object);
    auto rIt = relationPhases_.find(relation);
    if (tIt == entityEmbeddings_.end() || rIt == relationPhases_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [entityId, h_emb] : entityEmbeddings_) {
        float s = score(h_emb, rIt->second, tIt->second);
        scores.push_back({entityId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> RotatEEmbedding::predictRelation(const String& subject,
                                                                         const String& object,
                                                                         int topK) const {
    if (!initialized_) return {};

    auto hIt = entityEmbeddings_.find(subject);
    auto tIt = entityEmbeddings_.find(object);
    if (hIt == entityEmbeddings_.end() || tIt == entityEmbeddings_.end()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [relId, phase] : relationPhases_) {
        float s = score(hIt->second, phase, tIt->second);
        scores.push_back({relId, s});
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    if ((int)scores.size() > topK) scores.resize(topK);
    return scores;
}

void RotatEEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
    initialized_ = true;
}

void RotatEEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    // For RotatE, treat the embedding as the phase vector
    relationPhases_[relationId] = embedding;
    initialized_ = true;
}

void RotatEEmbedding::setRelationPhase(const String& relationId, const std::vector<float>& phase) {
    relationPhases_[relationId] = phase;
    initialized_ = true;
}

Json RotatEEmbedding::getStats() const {
    Json j;
    j["model"] = "RotatE";
    j["dimension"] = dimension_;
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationPhases_.size();
    j["initialized"] = initialized_;
    return j;
}

float RotatEEmbedding::scoreTriple(const Triple& triple) const {
    auto h = getEntityEmbedding(triple.subject);
    auto theta = getRelationPhase(triple.predicate);
    auto t = getEntityEmbedding(triple.object);
    if (h.empty() || theta.empty() || t.empty()) return 0.0f;
    return -score(h, theta, t);  // negate distance so higher = better
}

std::vector<std::pair<String, float>> RotatEEmbedding::findSimilarEntities(
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

bool RotatEEmbedding::save(const String& path) const {
    (void)path;
    return false;
}

bool RotatEEmbedding::load(const String& path) {
    (void)path;
    return false;
}

} // namespace ontology
