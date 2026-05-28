#include <ontology/Inference.hpp>
#include <ontology/Neural.hpp>
#include <ontology/GnnEmbedding.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>

namespace ontology {

// ============================================================================
// NeuralReasoner 实现 - 神经推理 (右脑)
// ============================================================================

NeuralReasoner::NeuralReasoner(StoragePtr storage, int embeddingDimension)
    : storage_(storage), embeddingDimension_(embeddingDimension) {
    embeddingModel_ = std::make_shared<TransEEmbedding>(embeddingDimension);
}

void NeuralReasoner::setEmbeddingModel(EmbeddingModelPtr model) {
    embeddingModel_ = model;
}

void NeuralReasoner::setEmbeddingModelByName(const String& modelName, const Json& config) {
    String lower = modelName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "rgcn") {
        RGCNEmbedding::Config rgcnConfig;
        rgcnConfig.entityDimension = config.value("entityDimension", embeddingDimension_);
        rgcnConfig.hiddenDimension = config.value("hiddenDimension", 512);
        rgcnConfig.numLayers = config.value("numLayers", 2);
        rgcnConfig.numBasis = config.value("numBasis", 100);
        rgcnConfig.dropout = config.value("dropout", 0.1f);
        rgcnConfig.learningRate = config.value("learningRate", 0.01f);
        rgcnConfig.epochs = config.value("epochs", 1000);
        embeddingModel_ = std::make_shared<RGCNEmbedding>(rgcnConfig);
    } else if (lower == "compgcn") {
        CompGCNEmbedding::Config compgcnConfig;
        compgcnConfig.dimension = config.value("dimension", embeddingDimension_);
        compgcnConfig.numLayers = config.value("numLayers", 2);
        compgcnConfig.learningRate = config.value("learningRate", 0.01f);
        compgcnConfig.epochs = config.value("epochs", 1000);
        String op = config.value("compositionOp", "multiply");
        if (op == "subtract") compgcnConfig.compositionOp = CompGCNEmbedding::CompositionOp::Subtract;
        else if (op == "circularcorr") compgcnConfig.compositionOp = CompGCNEmbedding::CompositionOp::CircularCorr;
        else compgcnConfig.compositionOp = CompGCNEmbedding::CompositionOp::Multiply;
        embeddingModel_ = std::make_shared<CompGCNEmbedding>(compgcnConfig);
    } else {
        // Default: TransE
        embeddingModel_ = std::make_shared<TransEEmbedding>(embeddingDimension_);
    }
}

void NeuralReasoner::setTextEmbedder(std::shared_ptr<TextEmbedder> embedder) {
    textEmbedder_ = embedder;
}

void NeuralReasoner::trainEmbeddings(int epochs, float learningRate) {
    // 获取所有三元组用于训练
    std::vector<Triple> triples;

    auto individuals = storage_->getAllIndividuals();
    for (const auto& ind : individuals) {
        auto related = storage_->queryTriples(TripleStore::TriplePattern{ind.id, "", "", false, true, true});
        triples.insert(triples.end(), related.begin(), related.end());
    }

    embeddingModel_->train(triples, epochs, learningRate);
}

std::vector<float> NeuralReasoner::getEmbedding(const String& entityId) {
    return embeddingModel_->getEntityEmbedding(entityId);
}

void NeuralReasoner::setEmbedding(const String& entityId, const std::vector<float>& embedding) {
    embeddingModel_->setEntityEmbedding(entityId, embedding);
}

// ============================================================================
// 相似搜索
// ============================================================================

std::vector<std::pair<String, float>> NeuralReasoner::findSimilar(
    const String& entityId,
    int topK,
    const String& filterClass
) {
    auto queryEmbedding = getEmbedding(entityId);
    if (queryEmbedding.empty()) {
        return {};
    }

    return findSimilar(queryEmbedding, topK, filterClass);
}

std::vector<std::pair<String, float>> NeuralReasoner::findSimilar(
    const std::vector<float>& queryEmbedding,
    int topK,
    const String& filterClass
) {
    std::vector<std::pair<String, float>> results;
    auto startTime = std::chrono::steady_clock::now();

    // 获取候选实体
    std::vector<Individual> candidates;
    if (filterClass.empty()) {
        candidates = storage_->getAllIndividuals();
    } else {
        candidates = storage_->getIndividualsByClass(filterClass);
    }

    // 计算相似度
    for (const auto& cand : candidates) {
        auto candEmbedding = getEmbedding(cand.id);
        if (!candEmbedding.empty()) {
            float sim = cosineSimilarity(queryEmbedding, candEmbedding);
            results.push_back({cand.id, sim});
        }
    }

    // 排序并返回 topK
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)results.size() > topK) {
        results.resize(topK);
    }

    // Record trace step
    if (explainabilityEngine_ && explainabilityEngine_->currentTrace() && !results.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        InferenceStep step;
        step.source = "neural";
        step.type = "similarity_search";
        step.embeddingModel = embeddingModel_->modelName();
        step.embeddingScore = results.empty() ? 0.0f : results[0].second;
        step.outputConfidence = results.empty() ? 0.0f : results[0].second;
        step.durationMs = elapsed;
        step.detail = "Similarity search: topK=" + std::to_string(topK) +
            " filterClass=" + (filterClass.empty() ? "none" : filterClass) +
            " results=" + std::to_string(results.size());
        std::vector<Triple> outputTriples;
        for (const auto& [id, score] : results) {
            Triple t; t.subject = "query"; t.predicate = "similarTo"; t.object = id; t.confidence = score;
            outputTriples.push_back(t);
        }
        explainabilityEngine_->currentTrace()->addStep(step, outputTriples);
    }

    return results;
}

std::vector<std::pair<String, float>> NeuralReasoner::findSimilarByText(
    const String& queryText,
    int topK,
    const String& filterClass
) {
    if (!textEmbedder_) return {};

    auto queryEmb = textEmbedder_->embed(queryText);
    if (queryEmb.empty()) return {};

    return findSimilar(queryEmb, topK, filterClass);
}

// ============================================================================
// 链接预测
// ============================================================================

std::vector<std::pair<String, float>> NeuralReasoner::predictLinks(
    const String& subject,
    const String& relation,
    int topK
) {
    auto startTime = std::chrono::steady_clock::now();
    auto results = embeddingModel_->predictTail(subject, relation, topK);

    // 转换为带名称的结果
    std::vector<std::pair<String, float>> namedResults;
    for (const auto& [id, score] : results) {
        auto ind = storage_->getIndividual(id);
        if (ind) {
            namedResults.push_back({ind->name, score});
        }
    }

    // Record trace step
    if (explainabilityEngine_ && explainabilityEngine_->currentTrace() && !results.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        InferenceStep step;
        step.source = "neural";
        step.type = "link_prediction";
        step.embeddingModel = embeddingModel_->modelName();
        step.outputConfidence = results.empty() ? 0.0f : results[0].second;
        step.durationMs = elapsed;
        step.detail = "Link prediction: " + subject + " --[" + relation + "]--> ? (" +
            std::to_string(results.size()) + " results)";
        step.inputFactIds.push_back(subject + ":" + relation);
        std::vector<Triple> outputTriples;
        for (const auto& [id, score] : results) {
            Triple t; t.subject = subject; t.predicate = relation; t.object = id; t.confidence = score;
            outputTriples.push_back(t);
        }
        explainabilityEngine_->currentTrace()->addStep(step, outputTriples);
    }

    return namedResults;
}

std::vector<std::pair<String, float>> NeuralReasoner::predictHead(
    const String& object,
    const String& relation,
    int topK
) {
    return embeddingModel_->predictHead(object, relation, topK);
}

std::vector<std::pair<String, float>> NeuralReasoner::predictRelation(
    const String& subject,
    const String& object,
    int topK
) {
    return embeddingModel_->predictRelation(subject, object, topK);
}

// ============================================================================
// 知询推理
// ============================================================================

InferenceResult NeuralReasoner::infer(const String& query) {
    InferenceResult result;
    result.query = query;

    // 提取查询中的实体
    auto entities = extractEntities(query);
    auto relations = extractRelations(query);

    if (!entities.empty()) {
        String mainEntity = entities[0];

        if (!relations.empty()) {
            String mainRelation = relations[0];

            // 链接预测
            auto predictions = predictLinks(mainEntity, mainRelation, 10);

            for (const auto& [target, score] : predictions) {
                Triple t;
                t.subject = mainEntity;
                t.predicate = mainRelation;
                t.object = target;
                t.confidence = score;
                t.source = "neural:link_prediction";
                result.facts.push_back(t);
            }
        } else {
            // 相似实体
            auto similar = findSimilar(mainEntity, 10);

            for (const auto& [id, score] : similar) {
                Triple t;
                t.subject = mainEntity;
                t.predicate = "similarTo";
                t.object = id;
                t.confidence = score;
                t.source = "neural:similarity";
                result.facts.push_back(t);
            }
        }
    }

    result.explanation = generateExplanation(result);
    return result;
}

std::vector<String> NeuralReasoner::extractEntities(const String& query) {
    std::vector<String> entities;

    auto individuals = storage_->getAllIndividuals();
    for (const auto& ind : individuals) {
        if (query.find(ind.name) != String::npos) {
            entities.push_back(ind.id);
        }
    }

    return entities;
}

std::vector<String> NeuralReasoner::extractRelations(const String& query) {
    std::vector<String> relations;

    // 常见关系关键词映射
    std::unordered_map<String, String> relationKeywords = {
        {"管理", "manages"},
        {"审批", "canApprove"},
        {"下属", "hasSubordinate"},
        {"上级", "hasSupervisor"},
        {"属于", "belongsTo"},
        {"包含", "contains"},
        {"相似", "similarTo"}
    };

    for (const auto& [keyword, rel] : relationKeywords) {
        if (query.find(keyword) != String::npos) {
            relations.push_back(rel);
        }
    }

    return relations;
}

String NeuralReasoner::generateExplanation(const InferenceResult& result) {
    std::ostringstream oss;

    oss << "神经推理过程 (右脑):\n";

    for (const auto& fact : result.facts) {
        oss << "  • " << fact.subject << " " << fact.predicate << " " << fact.object;
        oss << " (置信度: " << std::fixed << std::setprecision(2) << fact.confidence << ")";
        oss << " [" << fact.source << "]\n";
    }

    if (result.facts.empty()) {
        oss << "  (无推理结果)\n";
    }

    return oss.str();
}

// ============================================================================
// 辅助函数
// ============================================================================

float NeuralReasoner::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    if (normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

// ============================================================================
// TransEEmbedding 实现
// ============================================================================

TransEEmbedding::TransEEmbedding(int dimension)
    : dimension_(dimension), initialized_(false) {
}

std::vector<float> TransEEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    if (it != entityEmbeddings_.end()) {
        return it->second;
    }
    return {};
}

void TransEEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
}

void TransEEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationEmbeddings_[relationId] = embedding;
}

std::vector<float> TransEEmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationEmbeddings_.find(relationId);
    if (it != relationEmbeddings_.end()) {
        return it->second;
    }
    return {};
}

Json TransEEmbedding::getStats() const {
    Json j;
    j["model"] = "TransE";
    j["dimension"] = dimension_;
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationEmbeddings_.size();
    j["initialized"] = initialized_;
    return j;
}

void TransEEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    // 收集所有实体和关系
    std::unordered_set<String> entities, relations;
    for (const auto& t : triples) {
        entities.insert(t.subject);
        entities.insert(t.object);
        relations.insert(t.predicate);
    }

    // 初始化嵌入
    if (!initialized_) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        auto randomEmbedding = [&]() {
            std::vector<float> emb(dimension_);
            for (int i = 0; i < dimension_; ++i) {
                emb[i] = dist(gen);
            }
            // L2 归一化
            float norm = 0.0f;
            for (float v : emb) norm += v * v;
            norm = std::sqrt(norm);
            for (float& v : emb) v /= norm;
            return emb;
        };

        for (const auto& e : entities) {
            entityEmbeddings_[e] = randomEmbedding();
        }
        for (const auto& r : relations) {
            relationEmbeddings_[r] = randomEmbedding();
        }

        initialized_ = true;
    }

    // 简化的训练过程
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> indexDist(0, triples.size() - 1);
    std::uniform_int_distribution<int> entityDist(0, entities.size() - 1);

    std::vector<String> entityList(entities.begin(), entities.end());

    for (int epoch = 0; epoch < epochs; ++epoch) {
        // 采样正例
        const auto& posTriple = triples[indexDist(gen)];

        // 采样负例 (替换尾实体)
        String negTail = entityList[entityDist(gen)];
        while (negTail == posTriple.object) {
            negTail = entityList[entityDist(gen)];
        }

        // TransE: h + r ≈ t
        auto h = entityEmbeddings_[posTriple.subject];
        auto r = relationEmbeddings_[posTriple.predicate];
        auto tPos = entityEmbeddings_[posTriple.object];
        auto tNeg = entityEmbeddings_[negTail];

        // 计算距离
        auto posDistance = distance(h, r, tPos);
        auto negDistance = distance(h, r, tNeg);

        // Margin-based loss: max(0, margin + posDist - negDist)
        float margin = 1.0f;
        float loss = margin + posDistance - negDistance;

        if (loss > 0) {
            // Proper TransE gradient with L2 normalization
            // ∂(d)/∂h = (h+r-t)/d, ∂(d)/∂r = (h+r-t)/d, ∂(d)/∂t = -(h+r-t)/d
            // Loss = margin + d_pos - d_neg
            // ∂L/∂h = (h+r-t_pos)/d_pos - (h+r-t_neg)/d_neg
            float posDistSafe = posDistance > 1e-10f ? posDistance : 1e-10f;
            float negDistSafe = negDistance > 1e-10f ? negDistance : 1e-10f;

            for (int i = 0; i < dimension_; ++i) {
                float diffPos = h[i] + r[i] - tPos[i];
                float diffNeg = h[i] + r[i] - tNeg[i];

                float gradH = diffPos / posDistSafe - diffNeg / negDistSafe;
                float gradR = diffPos / posDistSafe - diffNeg / negDistSafe;
                float gradTPos = -diffPos / posDistSafe;
                float gradTNeg = diffNeg / negDistSafe;

                entityEmbeddings_[posTriple.subject][i] -= learningRate * gradH;
                relationEmbeddings_[posTriple.predicate][i] -= learningRate * gradR;
                entityEmbeddings_[posTriple.object][i] -= learningRate * gradTPos;
                entityEmbeddings_[negTail][i] -= learningRate * gradTNeg;
            }

            // L2 normalize entity embeddings after update
            for (const auto& eid : {posTriple.subject, posTriple.object, negTail}) {
                float norm = 0.0f;
                for (int i = 0; i < dimension_; ++i) norm += entityEmbeddings_[eid][i] * entityEmbeddings_[eid][i];
                norm = std::sqrt(norm);
                if (norm > 1e-10f) for (int i = 0; i < dimension_; ++i) entityEmbeddings_[eid][i] /= norm;
            }
        }
    }
}

std::vector<std::pair<String, float>> TransEEmbedding::predictTail(
    const String& subject,
    const String& relation,
    int topK
) const {
    auto h = entityEmbeddings_.find(subject);
    auto r = relationEmbeddings_.find(relation);

    if (h == entityEmbeddings_.end() || r == relationEmbeddings_.end()) {
        return {};
    }

    // 计算 h + r
    std::vector<float> hr(dimension_);
    for (int i = 0; i < dimension_; ++i) {
        hr[i] = h->second[i] + r->second[i];
    }

    // 与所有实体计算距离
    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityEmbeddings_) {
        if (id == subject) continue;

        float dist = l2Distance(hr, emb);
        float score = 1.0f / (1.0f + dist); // 转换为相似度
        scores.push_back({id, score});
    }

    // 排序
    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scores.size() > topK) {
        scores.resize(topK);
    }

    return scores;
}

std::vector<std::pair<String, float>> TransEEmbedding::predictHead(
    const String& object,
    const String& relation,
    int topK
) const {
    auto t = entityEmbeddings_.find(object);
    auto r = relationEmbeddings_.find(relation);

    if (t == entityEmbeddings_.end() || r == relationEmbeddings_.end()) {
        return {};
    }

    // 计算 t - r
    std::vector<float> tr(dimension_);
    for (int i = 0; i < dimension_; ++i) {
        tr[i] = t->second[i] - r->second[i];
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityEmbeddings_) {
        if (id == object) continue;

        float dist = l2Distance(tr, emb);
        float score = 1.0f / (1.0f + dist);
        scores.push_back({id, score});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scores.size() > topK) {
        scores.resize(topK);
    }

    return scores;
}

std::vector<std::pair<String, float>> TransEEmbedding::predictRelation(
    const String& subject,
    const String& object,
    int topK
) const {
    auto h = entityEmbeddings_.find(subject);
    auto t = entityEmbeddings_.find(object);

    if (h == entityEmbeddings_.end() || t == entityEmbeddings_.end()) {
        return {};
    }

    // 计算 t - h
    std::vector<float> th(dimension_);
    for (int i = 0; i < dimension_; ++i) {
        th[i] = t->second[i] - h->second[i];
    }

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : relationEmbeddings_) {
        float dist = l2Distance(th, emb);
        float score = 1.0f / (1.0f + dist);
        scores.push_back({id, score});
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scores.size() > topK) {
        scores.resize(topK);
    }

    return scores;
}

float TransEEmbedding::distance(
    const std::vector<float>& h,
    const std::vector<float>& r,
    const std::vector<float>& t
) const {
    float sum = 0.0f;
    for (int i = 0; i < dimension_; ++i) {
        float diff = h[i] + r[i] - t[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}


float TransEEmbedding::l2Distance(const std::vector<float>& a, const std::vector<float>& b) const {
    float sum = 0.0f;
    for (int i = 0; i < dimension_; ++i) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

float TransEEmbedding::scoreTriple(const Triple& triple) const {
    auto h = getEntityEmbedding(triple.subject);
    auto r = getRelationEmbedding(triple.predicate);
    auto t = getEntityEmbedding(triple.object);
    if (h.empty() || r.empty() || t.empty()) return 0.0f;
    return -distance(h, r, t);  // negate distance so higher = better
}

std::vector<std::pair<String, float>> TransEEmbedding::findSimilarEntities(
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

bool TransEEmbedding::save(const String& path) const {
    (void)path;
    return false;
}

bool TransEEmbedding::load(const String& path) {
    (void)path;
    return false;
}

} // namespace ontology
