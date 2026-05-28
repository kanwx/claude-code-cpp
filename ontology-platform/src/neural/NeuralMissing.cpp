#include <ontology/Neural.hpp>
#include <ontology/Storage.hpp>
#include <ontology/Inference.hpp>
#include <random>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

namespace ontology {

// ============================================================================
// OntologyEmbedding 实现
// ============================================================================

OntologyEmbedding::OntologyEmbedding(const Config& config) : config_(config) {}

void OntologyEmbedding::train(const Ontology& ontology) {
    // Collect triples from ontology individuals
    std::vector<Triple> triples;
    for (const auto& [id, ind] : ontology.individuals) {
        for (const auto& [pred, objects] : ind.properties) {
            for (const auto& obj : objects) {
                Triple t;
                t.subject = id;
                t.predicate = pred;
                t.object = obj;
                t.confidence = 1.0f;
                triples.push_back(t);
            }
        }
        if (!ind.classId.empty()) {
            Triple t;
            t.subject = id;
            t.predicate = "rdf:type";
            t.object = ind.classId;
            t.confidence = 1.0f;
            triples.push_back(t);
        }
    }

    // Initialize random embeddings for all entities
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    auto randomEmbedding = [&]() {
        std::vector<float> emb(config_.dimension);
        for (int i = 0; i < config_.dimension; ++i) emb[i] = dist(gen);
        float norm = 0.0f;
        for (float v : emb) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0) for (float& v : emb) v /= norm;
        return emb;
    };

    for (const auto& [id, cls] : ontology.classes) {
        if (entityEmbeddings_.find(id) == entityEmbeddings_.end()) {
            entityEmbeddings_[id] = randomEmbedding();
        }
    }
    for (const auto& [id, ind] : ontology.individuals) {
        if (entityEmbeddings_.find(id) == entityEmbeddings_.end()) {
            entityEmbeddings_[id] = randomEmbedding();
        }
    }

    // TransE training loop with proper gradient computation
    if (!triples.empty()) {
        std::uniform_int_distribution<size_t> indexDist(0, triples.size() - 1);
        std::vector<String> entityList;
        for (const auto& [id, _] : entityEmbeddings_) entityList.push_back(id);
        std::uniform_int_distribution<int> entityDist(0, entityList.size() - 1);

        for (int epoch = 0; epoch < config_.epochs; ++epoch) {
            const auto& pos = triples[indexDist(gen)];
            if (entityEmbeddings_.find(pos.subject) == entityEmbeddings_.end() ||
                entityEmbeddings_.find(pos.object) == entityEmbeddings_.end()) continue;

            auto& h = entityEmbeddings_[pos.subject];
            auto& t = entityEmbeddings_[pos.object];

            if (relationEmbeddings_.find(pos.predicate) == relationEmbeddings_.end()) {
                relationEmbeddings_[pos.predicate] = randomEmbedding();
            }
            auto& r = relationEmbeddings_[pos.predicate];

            float posScore = transEScore(h, r, t);

            // Generate negative sample by corrupting the tail
            String negId = entityList[entityDist(gen)];
            if (negId == pos.object) continue;
            auto& negT = entityEmbeddings_[negId];
            float negScore = transEScore(h, r, negT);

            float loss = marginRankingLoss(posScore, negScore);
            if (loss > 0) {
                float lr = config_.learningRate;
                for (int i = 0; i < config_.dimension; ++i) {
                    float diff_pos = h[i] + r[i] - t[i];
                    float diff_neg = h[i] + r[i] - negT[i];

                    // Gradient for positive triple: ∂d_pos/∂h = (h+r-t)/d_pos
                    // Gradient for negative triple: ∂d_neg/∂h = (h+r-negT)/d_neg
                    // Loss = margin + d_pos - d_neg, so ∂L/∂h = (h+r-t)/d_pos - (h+r-negT)/d_neg
                    float gradH = (posScore > 1e-10f ? diff_pos / posScore : diff_pos)
                                - (negScore > 1e-10f ? diff_neg / negScore : diff_neg);
                    float gradR = gradH;
                    float gradTPos = -(posScore > 1e-10f ? diff_pos / posScore : diff_pos);
                    float gradTNeg = (negScore > 1e-10f ? diff_neg / negScore : diff_neg);

                    h[i] -= lr * gradH;
                    r[i] -= lr * gradR;
                    t[i] -= lr * gradTPos;
                    negT[i] -= lr * gradTNeg;
                }
            }
        }
    }
}

std::vector<float> OntologyEmbedding::getEntityEmbedding(const String& entityId) const {
    auto it = entityEmbeddings_.find(entityId);
    return it != entityEmbeddings_.end() ? it->second : std::vector<float>();
}

std::vector<float> OntologyEmbedding::getRelationEmbedding(const String& relationId) const {
    auto it = relationEmbeddings_.find(relationId);
    return it != relationEmbeddings_.end() ? it->second : std::vector<float>();
}

float OntologyEmbedding::scoreTriple(const Triple& triple) const {
    auto h = getEntityEmbedding(triple.subject);
    auto r = getRelationEmbedding(triple.predicate);
    auto t = getEntityEmbedding(triple.object);
    if (h.empty() || r.empty() || t.empty()) return 0.0f;
    return -transEScore(h, r, t);
}

std::vector<std::pair<String, float>> OntologyEmbedding::findSimilarEntities(
    const String& entityId, int topK) const {
    auto target = getEntityEmbedding(entityId);
    if (target.empty()) return {};

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, emb] : entityEmbeddings_) {
        if (id == entityId) continue;
        float sim = cosineSimilarity(target, emb);
        scores.push_back({id, sim});
    }

    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> OntologyEmbedding::predictTail(
    const String& head, const String& relation, int topK) const {
    auto h = getEntityEmbedding(head);
    auto r = getRelationEmbedding(relation);
    if (h.empty() || r.empty()) return {};

    std::vector<float> hr(config_.dimension);
    for (int i = 0; i < config_.dimension; ++i) hr[i] = h[i] + r[i];

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, t] : entityEmbeddings_) {
        if (id == head) continue;
        float dist = l2Distance(hr, t);
        scores.push_back({id, 1.0f / (1.0f + dist)});
    }

    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

bool OntologyEmbedding::save(const String& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    int32_t dim = config_.dimension;
    file.write(reinterpret_cast<const char*>(&dim), sizeof(dim));

    int32_t numEntities = static_cast<int32_t>(entityEmbeddings_.size());
    file.write(reinterpret_cast<const char*>(&numEntities), sizeof(numEntities));
    for (const auto& [id, emb] : entityEmbeddings_) {
        int32_t len = static_cast<int32_t>(id.size());
        file.write(reinterpret_cast<const char*>(&len), sizeof(len));
        file.write(id.data(), len);
        file.write(reinterpret_cast<const char*>(emb.data()), dim * sizeof(float));
    }

    int32_t numRelations = static_cast<int32_t>(relationEmbeddings_.size());
    file.write(reinterpret_cast<const char*>(&numRelations), sizeof(numRelations));
    for (const auto& [id, emb] : relationEmbeddings_) {
        int32_t len = static_cast<int32_t>(id.size());
        file.write(reinterpret_cast<const char*>(&len), sizeof(len));
        file.write(id.data(), len);
        file.write(reinterpret_cast<const char*>(emb.data()), dim * sizeof(float));
    }

    return true;
}

bool OntologyEmbedding::load(const String& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    int32_t dim;
    file.read(reinterpret_cast<char*>(&dim), sizeof(dim));
    config_.dimension = dim;

    int32_t numEntities;
    file.read(reinterpret_cast<char*>(&numEntities), sizeof(numEntities));
    entityEmbeddings_.clear();
    for (int i = 0; i < numEntities; ++i) {
        int32_t len;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        String id(len, '\0');
        file.read(&id[0], len);
        std::vector<float> emb(dim);
        file.read(reinterpret_cast<char*>(emb.data()), dim * sizeof(float));
        entityEmbeddings_[id] = emb;
    }

    int32_t numRelations;
    file.read(reinterpret_cast<char*>(&numRelations), sizeof(numRelations));
    relationEmbeddings_.clear();
    for (int i = 0; i < numRelations; ++i) {
        int32_t len;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        String id(len, '\0');
        file.read(&id[0], len);
        std::vector<float> emb(dim);
        file.read(reinterpret_cast<char*>(emb.data()), dim * sizeof(float));
        relationEmbeddings_[id] = emb;
    }

    return true;
}

float OntologyEmbedding::l2Distance(const std::vector<float>& a, const std::vector<float>& b) const {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

float OntologyEmbedding::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    return denom < 1e-10f ? 0.0f : dot / denom;
}

float OntologyEmbedding::transEScore(const std::vector<float>& h, const std::vector<float>& r, const std::vector<float>& t) const {
    float sum = 0.0f;
    for (size_t i = 0; i < h.size() && i < r.size() && i < t.size(); ++i) {
        float d = h[i] + r[i] - t[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

float OntologyEmbedding::marginRankingLoss(float positive, float negative) const {
    return config_.margin + positive - negative;
}

// -- EmbeddingModel overrides added during unification --

void OntologyEmbedding::train(const std::vector<Triple>& triples, int epochs, float learningRate) {
    if (triples.empty()) return;

    // Initialize random embeddings for all entities mentioned in triples
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    auto randomEmbedding = [&]() {
        std::vector<float> emb(config_.dimension);
        for (int i = 0; i < config_.dimension; ++i) emb[i] = dist(gen);
        float norm = 0.0f;
        for (float v : emb) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0) for (float& v : emb) v /= norm;
        return emb;
    };

    for (const auto& t : triples) {
        if (entityEmbeddings_.find(t.subject) == entityEmbeddings_.end()) {
            entityEmbeddings_[t.subject] = randomEmbedding();
        }
        if (entityEmbeddings_.find(t.object) == entityEmbeddings_.end()) {
            entityEmbeddings_[t.object] = randomEmbedding();
        }
        if (relationEmbeddings_.find(t.predicate) == relationEmbeddings_.end()) {
            relationEmbeddings_[t.predicate] = randomEmbedding();
        }
    }

    // TransE training loop
    std::uniform_int_distribution<size_t> indexDist(0, triples.size() - 1);
    std::vector<String> entityList;
    for (const auto& [id, _] : entityEmbeddings_) entityList.push_back(id);
    std::uniform_int_distribution<int> entityDist(0, entityList.size() - 1);

    for (int epoch = 0; epoch < epochs; ++epoch) {
        const auto& pos = triples[indexDist(gen)];
        if (entityEmbeddings_.find(pos.subject) == entityEmbeddings_.end() ||
            entityEmbeddings_.find(pos.object) == entityEmbeddings_.end()) continue;

        auto& h = entityEmbeddings_[pos.subject];
        auto& t = entityEmbeddings_[pos.object];
        auto& r = relationEmbeddings_[pos.predicate];

        float posScore = transEScore(h, r, t);

        String negId = entityList[entityDist(gen)];
        if (negId == pos.object) continue;
        auto& negT = entityEmbeddings_[negId];
        float negScore = transEScore(h, r, negT);

        float loss = marginRankingLoss(posScore, negScore);
        if (loss > 0) {
            float lr = learningRate;
            for (int i = 0; i < config_.dimension; ++i) {
                float diff_pos = h[i] + r[i] - t[i];
                float diff_neg = h[i] + r[i] - negT[i];

                float gradH = (posScore > 1e-10f ? diff_pos / posScore : diff_pos)
                            - (negScore > 1e-10f ? diff_neg / negScore : diff_neg);
                float gradR = gradH;
                float gradTPos = -(posScore > 1e-10f ? diff_pos / posScore : diff_pos);
                float gradTNeg = (negScore > 1e-10f ? diff_neg / negScore : diff_neg);

                h[i] -= lr * gradH;
                r[i] -= lr * gradR;
                t[i] -= lr * gradTPos;
                negT[i] -= lr * gradTNeg;
            }
        }
    }
}

void OntologyEmbedding::setEntityEmbedding(const String& entityId, const std::vector<float>& embedding) {
    entityEmbeddings_[entityId] = embedding;
}

void OntologyEmbedding::setRelationEmbedding(const String& relationId, const std::vector<float>& embedding) {
    relationEmbeddings_[relationId] = embedding;
}

std::vector<std::pair<String, float>> OntologyEmbedding::predictHead(
    const String& object, const String& relation, int topK) const {
    auto t = getEntityEmbedding(object);
    auto r = getRelationEmbedding(relation);
    if (t.empty() || r.empty()) return {};

    // t - r should approximate h
    std::vector<float> tr(config_.dimension);
    for (int i = 0; i < config_.dimension; ++i) tr[i] = t[i] - r[i];

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, h] : entityEmbeddings_) {
        if (id == object) continue;
        float dist = l2Distance(tr, h);
        scores.push_back({id, 1.0f / (1.0f + dist)});
    }

    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

std::vector<std::pair<String, float>> OntologyEmbedding::predictRelation(
    const String& subject, const String& object, int topK) const {
    auto h = getEntityEmbedding(subject);
    auto t = getEntityEmbedding(object);
    if (h.empty() || t.empty()) return {};

    // t - h should approximate r
    std::vector<float> th(config_.dimension);
    for (int i = 0; i < config_.dimension; ++i) th[i] = t[i] - h[i];

    std::vector<std::pair<String, float>> scores;
    for (const auto& [id, r] : relationEmbeddings_) {
        float dist = l2Distance(th, r);
        scores.push_back({id, 1.0f / (1.0f + dist)});
    }

    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(scores.size()) > topK) scores.resize(topK);
    return scores;
}

Json OntologyEmbedding::getStats() const {
    Json j;
    j["model"] = config_.method;
    j["dimension"] = config_.dimension;
    j["numEntities"] = entityEmbeddings_.size();
    j["numRelations"] = relationEmbeddings_.size();
    return j;
}

// ============================================================================
// GraphNeuralNetwork 实现
// ============================================================================

GraphNeuralNetwork::GraphNeuralNetwork(const Config& config) : config_(config) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);

    int inDim = config.inputDimension;
    int hiddenDim = config.hiddenDimension;
    int outDim = config.outputDimension;

    // Layer 1: input -> hidden
    weights_.push_back({});
    for (int i = 0; i < hiddenDim; ++i) {
        std::vector<float> row(inDim);
        for (int j = 0; j < inDim; ++j) row[j] = dist(gen);
        weights_.back().push_back(row);
    }

    // Layer 2+: hidden -> hidden (or output)
    for (int layer = 1; layer < config.numLayers; ++layer) {
        weights_.push_back({});
        int inSize = hiddenDim;
        int outSize = (layer == config.numLayers - 1) ? outDim : hiddenDim;
        for (int i = 0; i < outSize; ++i) {
            std::vector<float> row(inSize);
            for (int j = 0; j < inSize; ++j) row[j] = dist(gen);
            weights_.back().push_back(row);
        }
    }
}

std::vector<float> GraphNeuralNetwork::forward(
    const std::vector<float>& nodeFeatures,
    const std::vector<std::vector<float>>& neighborFeatures,
    const std::vector<float>& edgeWeights
) {
    // Step 1: Aggregate neighbor features (graph convolution input)
    auto aggregated = graphConv(nodeFeatures, neighborFeatures, edgeWeights);

    // Step 2: Pass through all weight layers with activation
    auto current = aggregated;
    for (size_t l = 0; l < weights_.size(); ++l) {
        // Linear transform: output = W_l * current
        int outSize = static_cast<int>(weights_[l].size());
        int inSize = current.size();
        std::vector<float> output(outSize, 0.0f);
        for (int i = 0; i < outSize; ++i) {
            for (int j = 0; j < inSize && j < static_cast<int>(weights_[l][i].size()); ++j) {
                output[i] += weights_[l][i][j] * current[j];
            }
        }
        // Apply ReLU (except last layer)
        if (l < weights_.size() - 1) {
            current = relu(output);
        } else {
            current = output;
        }
    }
    return current;
}

std::vector<float> GraphNeuralNetwork::graphConv(
    const std::vector<float>& input,
    const std::vector<std::vector<float>>& neighbors,
    const std::vector<float>& weights
) {
    // Determine output dimension from first weight layer
    int dim = config_.inputDimension;
    if (!input.empty()) dim = static_cast<int>(input.size());

    // Aggregate neighbor features (weighted mean)
    std::vector<float> agg(dim, 0.0f);
    if (!neighbors.empty()) {
        for (size_t i = 0; i < neighbors.size() && i < weights.size(); ++i) {
            float w = weights[i];
            for (size_t j = 0; j < neighbors[i].size() && j < agg.size(); ++j) {
                agg[j] += neighbors[i][j] * w;
            }
        }
        float wSum = 0.0f;
        for (size_t i = 0; i < weights.size() && i <= neighbors.size(); ++i) {
            wSum += std::abs(weights[i < weights.size() ? i : 0]);
        }
        if (wSum > 1e-10f) for (float& v : agg) v /= wSum;
        else for (float& v : agg) v /= neighbors.size();
    }

    // Combine self features with aggregated neighbors (skip connection)
    std::vector<float> result(dim, 0.0f);
    for (size_t j = 0; j < agg.size() && j < result.size(); ++j) {
        result[j] = (j < input.size() ? input[j] : 0.0f) + agg[j];
    }

    return result;
}

std::vector<float> GraphNeuralNetwork::attentionAggregate(
    const std::vector<float>& query,
    const std::vector<std::vector<float>>& keys,
    const std::vector<std::vector<float>>& values
) {
    if (keys.empty()) return query;

    std::vector<float> attnScores(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        float dot = 0.0f;
        for (size_t j = 0; j < query.size() && j < keys[i].size(); ++j) {
            dot += query[j] * keys[i][j];
        }
        attnScores[i] = dot;
    }

    auto probs = softmax(attnScores);

    int dim = values[0].size();
    std::vector<float> result(dim, 0.0f);
    for (size_t i = 0; i < values.size(); ++i) {
        for (int j = 0; j < dim; ++j) {
            result[j] += probs[i] * values[i][j];
        }
    }
    return result;
}

std::vector<float> GraphNeuralNetwork::relu(const std::vector<float>& x) const {
    std::vector<float> result(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = std::max(0.0f, x[i]);
    }
    return result;
}

std::vector<float> GraphNeuralNetwork::softmax(const std::vector<float>& x) const {
    float maxVal = *std::max_element(x.begin(), x.end());
    std::vector<float> result(x.size());
    float sum = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = std::exp(x[i] - maxVal);
        sum += result[i];
    }
    if (sum > 0) for (float& v : result) v /= sum;
    return result;
}

// ============================================================================
// KnowledgeGraphNN 实现
// ============================================================================

KnowledgeGraphNN::KnowledgeGraphNN(const Config& config)
    : config_(config)
    , gnn_(GraphNeuralNetwork::Config{config.embeddingDimension, config.gnnHiddenDimension,
                                       config.embeddingDimension, config.numGNNLayers})
    , embedding_(std::make_shared<OntologyEmbedding>(OntologyEmbedding::Config{config.embeddingDimension}))
{
}

std::vector<float> KnowledgeGraphNN::encodeEntity(
    const Individual& entity,
    const Ontology& ontology,
    const EmbeddingModel& emb
) {
    auto entityEmb = emb.getEntityEmbedding(entity.id);
    if (entityEmb.empty()) {
        entityEmb.resize(config_.embeddingDimension, 0.0f);
    }

    // Collect neighbor features
    std::vector<std::vector<float>> neighborFeatures;
    std::vector<float> edgeWeights;

    // For simplicity, use related entities from the ontology
    for (const auto& [relId, targets] : entity.relations) {
        auto relEmb = emb.getRelationEmbedding(relId);
        for (const auto& target : targets) {
            auto targetEmb = emb.getEntityEmbedding(target);
            if (!targetEmb.empty()) {
                neighborFeatures.push_back(targetEmb);
                edgeWeights.push_back(1.0f);
            }
        }
    }

    return gnn_.forward(entityEmb, neighborFeatures, edgeWeights);
}

std::vector<float> KnowledgeGraphNN::encodeRelation(
    const Relation& relation,
    const EmbeddingModel& emb
) {
    return emb.getRelationEmbedding(relation.id);
}

std::vector<float> KnowledgeGraphNN::encodeTriple(
    const Triple& triple,
    const Ontology& ontology,
    const EmbeddingModel& emb
) {
    auto h = emb.getEntityEmbedding(triple.subject);
    auto r = emb.getRelationEmbedding(triple.predicate);
    auto t = emb.getEntityEmbedding(triple.object);

    if (h.empty()) h.resize(config_.embeddingDimension, 0.0f);
    if (r.empty()) r.resize(config_.embeddingDimension, 0.0f);
    if (t.empty()) t.resize(config_.embeddingDimension, 0.0f);

    // Concatenate h, r, t
    std::vector<float> result;
    result.reserve(h.size() + r.size() + t.size());
    result.insert(result.end(), h.begin(), h.end());
    result.insert(result.end(), r.begin(), r.end());
    result.insert(result.end(), t.begin(), t.end());
    return result;
}

std::vector<Triple> KnowledgeGraphNN::reason(
    const Ontology& ontology,
    const String& query,
    int topK
) {
    // Use embedding-based link prediction as reasoning
    std::vector<Triple> results;
    if (!embedding_) return results;

    // Simple approach: find entities mentioned in query, predict related triples
    for (const auto& [id, ind] : ontology.individuals) {
        if (query.find(ind.name) != String::npos || query.find(id) != String::npos) {
            auto predictions = embedding_->predictTail(id, "", topK);
            for (const auto& [obj, score] : predictions) {
                Triple t;
                t.subject = id;
                t.predicate = "relatedTo";
                t.object = obj;
                t.confidence = score;
                t.source = "kgnn:reasoning";
                results.push_back(t);
            }
        }
    }

    if (static_cast<int>(results.size()) > topK) results.resize(topK);
    return results;
}

String KnowledgeGraphNN::answerQuestion(
    const String& question,
    const Ontology& ontology,
    const EmbeddingModel& emb
) {
    auto triples = reason(ontology, question, 5);
    if (triples.empty()) return "No relevant information found.";

    std::string answer = "Based on the knowledge graph:\n";
    for (const auto& t : triples) {
        answer += "- " + t.subject + " → " + t.predicate + " → " + t.object +
                  " (confidence: " + std::to_string(static_cast<int>(t.confidence * 100)) + "%)\n";
    }
    return answer;
}

std::vector<float> KnowledgeGraphNN::multiHeadAttention(
    const std::vector<float>& query,
    const std::vector<float>& key,
    const std::vector<float>& value,
    int numHeads
) {
    int dim = static_cast<int>(query.size());
    int headDim = dim / numHeads;
    if (headDim == 0) headDim = 1;

    std::vector<float> result(dim, 0.0f);

    for (int h = 0; h < numHeads; ++h) {
        int start = h * headDim;
        int end = std::min(start + headDim, dim);

        // Dot product attention for this head
        float score = 0.0f;
        for (int i = start; i < end; ++i) {
            score += query[i] * key[i];
        }
        score /= std::sqrt(static_cast<float>(headDim));

        float weight = 1.0f / (1.0f + std::exp(-score));
        for (int i = start; i < end; ++i) {
            result[i] = weight * value[i];
        }
    }

    return result;
}

// ============================================================================
// DualBrainSystem 实现
// ============================================================================

DualBrainSystem::DualBrainSystem(const Config& config)
    : config_(config)
    , embedding_(std::make_shared<OntologyEmbedding>(OntologyEmbedding::Config{config.embeddingDimension}))
{
    if (config.enableSymbolic) {
        // symbolicReasoner_ needs storage, set later
    }
    if (config.enableNeural) {
        neuralReasoner_ = std::make_unique<NeuralReasoner>(nullptr, config.embeddingDimension);
    }
}

void DualBrainSystem::setOntology(std::shared_ptr<Ontology> ontology) {
    ontology_ = ontology;
}

void DualBrainSystem::setStorage(std::shared_ptr<HybridStorage> storage) {
    storage_ = storage;
    if (config_.enableSymbolic && storage) {
        symbolicReasoner_ = std::make_unique<SymbolicReasoner>(storage);
    }
    if (config_.enableNeural && storage) {
        neuralReasoner_ = std::make_unique<NeuralReasoner>(storage, config_.embeddingDimension);
    }
}

DualBrainSystem::Result DualBrainSystem::process(const String& query) {
    Result result;
    result.queryEmbedding.resize(config_.embeddingDimension, 0.0f);

    // Left brain: symbolic reasoning
    if (config_.enableSymbolic && symbolicReasoner_) {
        auto infResult = symbolicReasoner_->infer(query);
        result.symbolicFacts = infResult.facts;
        result.symbolicExplanation = infResult.explanation;
    }

    // Right brain: neural reasoning
    if (config_.enableNeural && neuralReasoner_) {
        result.neuralMatches = rightBrainReason(query);
    }

    // Integration
    result.combinedResults = integrateResults(result.symbolicFacts, result.neuralMatches);

    // Generate answer
    if (!result.combinedResults.empty()) {
        std::ostringstream oss;
        oss << "融合推理结果:\n";
        for (const auto& ind : result.combinedResults) {
            oss << "- " << ind.name << " [" << ind.classId << "]\n";
        }
        result.finalAnswer = oss.str();
    } else if (!result.symbolicFacts.empty()) {
        result.finalAnswer = result.symbolicExplanation;
    }

    // Compute confidence
    float symConf = result.symbolicFacts.empty() ? 0.0f : 0.8f;
    float neuConf = result.neuralMatches.empty() ? 0.0f : result.neuralMatches[0].second;
    result.confidence = symConf * config_.symbolicWeight + neuConf * config_.neuralWeight;
    float total = config_.symbolicWeight + config_.neuralWeight;
    if (total > 0) result.confidence /= total;

    return result;
}

std::vector<Triple> DualBrainSystem::leftBrainReason(const String& query) {
    if (!symbolicReasoner_) return {};
    auto result = symbolicReasoner_->infer(query);
    return result.facts;
}

std::vector<std::pair<Individual, float>> DualBrainSystem::rightBrainReason(const String& query) {
    if (!neuralReasoner_ || !storage_) return {};

    std::vector<std::pair<Individual, float>> results;

    // Find entities mentioned in query
    auto individuals = storage_->getAllIndividuals();
    for (const auto& ind : individuals) {
        if (!ind.name.empty() && query.find(ind.name) != String::npos) {
            auto similar = neuralReasoner_->findSimilar(ind.id, 10);
            for (const auto& [id, score] : similar) {
                auto found = storage_->getIndividual(id);
                if (found) results.push_back({*found, score});
            }
        }
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    return results;
}

std::vector<Individual> DualBrainSystem::integrateResults(
    const std::vector<Triple>& symbolic,
    const std::vector<std::pair<Individual, float>>& neural
) {
    std::unordered_set<String> seen;
    std::vector<Individual> combined;

    // Add symbolic results first (higher priority)
    for (const auto& t : symbolic) {
        if (storage_ && seen.find(t.object) == seen.end()) {
            auto ind = storage_->getIndividual(t.object);
            if (ind) {
                combined.push_back(*ind);
                seen.insert(t.object);
            }
        }
    }

    // Add neural results
    for (const auto& [ind, score] : neural) {
        if (seen.find(ind.id) == seen.end() && score >= config_.integrationThreshold) {
            combined.push_back(ind);
            seen.insert(ind.id);
        }
    }

    return combined;
}

std::vector<Individual> DualBrainSystem::resolveConflicts(
    const std::vector<Individual>& results1,
    const std::vector<Individual>& results2
) {
    std::unordered_set<String> seen;
    std::vector<Individual> resolved;

    for (const auto& ind : results1) {
        if (seen.find(ind.id) == seen.end()) {
            resolved.push_back(ind);
            seen.insert(ind.id);
        }
    }
    for (const auto& ind : results2) {
        if (seen.find(ind.id) == seen.end()) {
            resolved.push_back(ind);
            seen.insert(ind.id);
        }
    }

    return resolved;
}

} // namespace ontology
