#include <ontology/Reranker.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <unordered_set>

namespace ontology {

// ============================================================================
// HttpRerankBackend
// ============================================================================

HttpRerankBackend::HttpRerankBackend(const Config& config)
    : config_(config)
{
}

std::vector<std::pair<int, float>> HttpRerankBackend::rerank(
    const String& query,
    const std::vector<String>& documents,
    int topK
) {
    try {
        String host = config_.endpoint;
        int port = 8001;

        if (host.substr(0, 8) == "https://") host = host.substr(8);
        else if (host.substr(0, 7) == "http://") host = host.substr(7);

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        Json docArray = Json::array();
        int docLimit = std::min(static_cast<int>(documents.size()), config_.maxDocuments);
        for (int i = 0; i < docLimit; i++) {
            docArray.push_back(documents[i]);
        }

        Json body;
        body["model"] = config_.model;
        body["query"] = query;
        body["documents"] = docArray;
        body["top_n"] = topK > 0 ? topK : static_cast<int>(documents.size());

        httplib::Client client(host, port);
        client.set_read_timeout(config_.timeoutMs / 1000);

        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };
        if (!config_.apiKey.empty()) {
            headers.emplace("Authorization", "Bearer " + config_.apiKey);
        }

        auto res = client.Post(config_.apiPath, headers, body.dump(), "application/json");
        if (!res || res->status != 200) return {};

        Json resp = Json::parse(res->body);
        if (!resp.contains("results")) return {};

        std::vector<std::pair<int, float>> results;
        for (const auto& item : resp["results"]) {
            int idx = item.value("index", 0);
            float score = item.value("relevance_score", 0.0f);
            results.push_back({idx, score});
        }

        // Sort by relevance score descending
        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (topK > 0 && static_cast<int>(results.size()) > topK) {
            results.resize(topK);
        }

        return results;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return {};
    }
}

bool HttpRerankBackend::isAvailable() const {
    try {
        String host = config_.endpoint;
        int port = 8001;

        if (host.substr(0, 8) == "https://") host = host.substr(8);
        else if (host.substr(0, 7) == "http://") host = host.substr(7);

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        httplib::Client client(host, port);
        client.set_read_timeout(5);
        client.set_connection_timeout(5);

        auto res = client.Get("/health");
        if (res && res->status == 200) return true;

        res = client.Get("/");
        return res && (res->status == 200 || res->status == 404);
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return false;
    }
}

// ============================================================================
// RerankerEngine
// ============================================================================

RerankerEngine::RerankerEngine(
    std::shared_ptr<TextEmbedder> embedder,
    StoragePtr hybridStorage,
    LlmCollaboration* llm
)
    : embedder_(embedder)
    , hybridStorage_(hybridStorage)
    , llm_(llm)
{
}

std::vector<RerankerEngine::RerankedResult>
RerankerEngine::rerank(
    const String& query,
    const std::vector<HybridRetrievalEngine::RetrievalResult>& candidates,
    int topK
) {
    int k = topK > 0 ? topK : config_.mmrTopK;

    if (candidates.empty()) return {};

    // Limit candidates for reranking
    std::vector<HybridRetrievalEngine::RetrievalResult> rerankCandidates;
    int maxC = static_cast<int>(candidates.size());
    int limit = std::min(maxC, config_.maxRerankCandidates);
    rerankCandidates.assign(candidates.begin(), candidates.begin() + limit);

    // If external rerank backend is available, use it
    if (rerankBackend_ && rerankBackend_->isAvailable()) {
        std::vector<String> docs;
        docs.reserve(rerankCandidates.size());
        for (const auto& c : rerankCandidates) {
            docs.push_back(c.text);
        }

        auto rerankResults = rerankBackend_->rerank(query, docs, k);
        if (!rerankResults.empty()) {
            std::vector<RerankedResult> results;
            for (size_t i = 0; i < rerankResults.size(); i++) {
                int origIdx = rerankResults[i].first;
                float score = rerankResults[i].second;
                if (origIdx < 0 || origIdx >= static_cast<int>(rerankCandidates.size())) continue;

                const auto& c = rerankCandidates[origIdx];
                RerankedResult r;
                r.id = c.id;
                r.text = c.text;
                r.documentId = c.documentId;
                r.knowledgeBaseId = c.knowledgeBaseId;
                r.originalScore = c.fusedScore;
                r.rerankScore = score;
                r.finalScore = score;
                r.originalRank = origIdx + 1;
                r.rerankedRank = static_cast<int>(i) + 1;
                r.matchedEntities = c.matchedEntities;
                results.push_back(r);
            }
            return results;
        }
    }

    // Fallback: internal semantic cross-encoder reranking
    auto results = semanticCrossRerank(query, rerankCandidates);

    // Apply MMR for diversity
    if (config_.enableMMR) {
        results = applyMMR(results, k);
    } else {
        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.finalScore > b.finalScore; });
        if (static_cast<int>(results.size()) > k) {
            results.resize(k);
        }
    }

    // Assign final ranks
    for (size_t i = 0; i < results.size(); i++) {
        results[i].rerankedRank = static_cast<int>(i) + 1;
    }

    return results;
}

void RerankerEngine::annotateCitations(
    std::vector<RerankedResult>& results,
    const std::vector<Triple>& graphFacts
) {
    for (auto& result : results) {
        // Clear existing citations
        result.citations.clear();

        // Citation from graph facts
        for (const auto& fact : graphFacts) {
            // Check if fact entities appear in the chunk text
            bool subjectInText = result.text.find(fact.subject) != String::npos;
            bool objectInText = result.text.find(fact.object) != String::npos;

            if (subjectInText || objectInText) {
                RerankedResult::Citation cit;
                cit.source = fact.source.empty() ? "knowledge_graph" : fact.source;
                cit.fact = fact.subject + " " + fact.predicate + " " + fact.object;
                cit.relevance = fact.confidence;

                // Find position
                if (subjectInText) {
                    cit.startPos = static_cast<int>(result.text.find(fact.subject));
                    cit.endPos = cit.startPos + static_cast<int>(fact.subject.length());
                } else if (objectInText) {
                    cit.startPos = static_cast<int>(result.text.find(fact.object));
                    cit.endPos = cit.startPos + static_cast<int>(fact.object.length());
                }

                result.citations.push_back(cit);
            }
        }

        // Citation from document source
        if (!result.documentId.empty()) {
            RerankedResult::Citation cit;
            cit.source = "document:" + result.documentId;
            cit.fact = "Source document";
            cit.relevance = result.rerankScore;
            cit.startPos = 0;
            cit.endPos = static_cast<int>(result.text.length());
            result.citations.push_back(cit);
        }

        // Sort citations by relevance
        std::sort(result.citations.begin(), result.citations.end(),
            [](const auto& a, const auto& b) { return a.relevance > b.relevance; });

        // Limit citations
        if (result.citations.size() > 10) {
            result.citations.resize(10);
        }
    }
}

// ============================================================================
// Semantic Cross-Encoder Reranking
// ============================================================================

std::vector<RerankerEngine::RerankedResult>
RerankerEngine::semanticCrossRerank(
    const String& query,
    const std::vector<HybridRetrievalEngine::RetrievalResult>& candidates
) {
    std::vector<RerankedResult> results;
    results.reserve(candidates.size());

    auto queryEmb = embedder_->embed(query);

    for (size_t i = 0; i < candidates.size(); i++) {
        const auto& c = candidates[i];

        RerankedResult r;
        r.id = c.id;
        r.text = c.text;
        r.documentId = c.documentId;
        r.knowledgeBaseId = c.knowledgeBaseId;
        r.originalScore = c.fusedScore;
        r.originalRank = static_cast<int>(i) + 1;
        r.matchedEntities = c.matchedEntities;

        // === Signal 1: Semantic similarity (cross-encoder approximation) ===
        // True cross-encoder would use a trained model; here we use
        // fine-grained embedding similarity as an approximation
        float semanticScore = 0.0f;
        if (!queryEmb.empty() && !c.text.empty()) {
            auto docEmb = embedder_->embed(c.text);
            semanticScore = cosineSimilarity(queryEmb, docEmb);
        }

        // === Signal 2: Key-term matching ===
        float keytermScore = computeKeytermScore(query, c.text);

        // Collect key term matches for citation
        auto keyTerms = extractKeyTerms(query);
        for (const auto& term : keyTerms) {
            auto positions = findTermPositions(term, c.text);
            if (!positions.empty()) {
                RerankerEngine::RerankedResult::KeyTermMatch m;
                m.term = term;
                m.position = positions[0].second;
                m.importance = static_cast<float>(positions.size()) /
                               std::max(1.0f, static_cast<float>(c.text.length()) / 100.0f);
                r.keyTermMatches.push_back(m);
            }
        }

        // === Signal 3: Entity overlap ===
        float entityScore = computeEntityScore(query, c.text, c.matchedEntities);

        // === Combine signals ===
        r.rerankScore = config_.semanticWeight * semanticScore +
                        config_.keytermWeight * keytermScore +
                        config_.entityWeight * entityScore;

        // Blend with original score
        r.finalScore = (1.0f - config_.crossEncoderWeight) * r.originalScore +
                        config_.crossEncoderWeight * r.rerankScore;

        results.push_back(r);
    }

    // Sort by final score
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.finalScore > b.finalScore; });

    return results;
}

float RerankerEngine::computeKeytermScore(
    const String& query,
    const String& docText
) const {
    auto keyTerms = extractKeyTerms(query);
    if (keyTerms.empty() || docText.empty()) return 0.0f;

    float totalScore = 0.0f;
    int matched = 0;

    for (const auto& term : keyTerms) {
        // Count occurrences
        size_t count = 0;
        size_t pos = 0;
        while ((pos = docText.find(term, pos)) != String::npos) {
            count++;
            pos += term.length();
        }

        if (count > 0) {
            matched++;
            // TF-based scoring with diminishing returns
            float tf = 1.0f + std::log(static_cast<float>(count));
            totalScore += tf;
        }
    }

    // Coverage: what fraction of key terms appear in the document
    float coverage = static_cast<float>(matched) / keyTerms.size();

    // Combine coverage and TF
    return coverage * 0.6f + (totalScore / keyTerms.size()) * 0.4f;
}

float RerankerEngine::computeEntityScore(
    const String& query,
    const String& docText,
    const std::vector<String>& matchedEntities
) const {
    if (matchedEntities.empty()) return 0.0f;

    float score = 0.0f;
    for (const auto& entity : matchedEntities) {
        if (docText.find(entity) != String::npos) {
            score += 1.0f;
        }
    }

    // Normalize
    return matchedEntities.empty() ? 0.0f : score / matchedEntities.size();
}

// ============================================================================
// MMR (Maximal Marginal Relevance)
// ============================================================================

std::vector<RerankerEngine::RerankedResult>
RerankerEngine::applyMMR(
    std::vector<RerankedResult>& candidates,
    int topK
) {
    if (candidates.empty()) return {};

    // Pre-compute embeddings for all candidates
    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(candidates.size());
    for (const auto& c : candidates) {
        embeddings.push_back(embedder_->embed(c.text));
    }

    std::vector<RerankedResult> selected;
    std::unordered_set<int> selectedIndices;

    // First: pick the highest-scoring candidate
    int bestIdx = 0;
    float bestScore = candidates[0].finalScore;
    for (size_t i = 1; i < candidates.size(); i++) {
        if (candidates[i].finalScore > bestScore) {
            bestScore = candidates[i].finalScore;
            bestIdx = static_cast<int>(i);
        }
    }
    selected.push_back(candidates[bestIdx]);
    selectedIndices.insert(bestIdx);

    // Iteratively select candidates balancing relevance and diversity
    while (static_cast<int>(selected.size()) < topK &&
           selected.size() < candidates.size()) {
        float bestMMR = -std::numeric_limits<float>::max();
        int bestI = -1;

        for (size_t i = 0; i < candidates.size(); i++) {
            if (selectedIndices.count(static_cast<int>(i))) continue;

            // Relevance to query
            float relevance = candidates[i].finalScore;

            // Maximum similarity to already-selected results
            float maxSim = 0.0f;
            for (size_t j = 0; j < selected.size(); j++) {
                int selIdx = -1;
                // Find the original index of selected[j]
                for (size_t k = 0; k < candidates.size(); k++) {
                    if (candidates[k].id == selected[j].id) {
                        selIdx = static_cast<int>(k);
                        break;
                    }
                }
                if (selIdx >= 0 && !embeddings[i].empty() && !embeddings[selIdx].empty()) {
                    float sim = cosineSimilarity(embeddings[i], embeddings[selIdx]);
                    maxSim = std::max(maxSim, sim);
                }
            }

            // MMR score
            float mmr = config_.mmrLambda * relevance -
                        (1.0f - config_.mmrLambda) * maxSim;

            if (mmr > bestMMR) {
                bestMMR = mmr;
                bestI = static_cast<int>(i);
            }
        }

        if (bestI < 0) break;

        selected.push_back(candidates[bestI]);
        selectedIndices.insert(bestI);
    }

    return selected;
}

// ============================================================================
// Helpers
// ============================================================================

std::vector<String> RerankerEngine::extractKeyTerms(const String& query) const {
    std::vector<String> terms;
    size_t i = 0;
    String currentWord;

    // Common stop words
    static const std::unordered_set<String> stopWords = {
        "的", "了", "在", "是", "我", "有", "和", "就", "不", "人",
        "都", "一", "一个", "上", "也", "很", "到", "说", "要", "去",
        "你", "会", "着", "没有", "看", "好", "自己", "这", "那",
        "什么", "怎么", "哪个", "哪些", "为什么", "如何", "是否",
        "可以", "能够", "应该", "需要", "请", "问", "想",
        "the", "a", "an", "is", "are", "was", "were", "be", "been",
        "have", "has", "had", "do", "does", "did", "will", "would",
        "can", "could", "may", "might", "shall", "should",
        "it", "in", "on", "at", "to", "for", "of", "with",
        "what", "how", "why", "which", "when", "where", "who",
    };

    while (i < query.size()) {
        unsigned char c = static_cast<unsigned char>(query[i]);

        if (c < 0x80) {
            if (std::isalnum(c) || c == '_') {
                currentWord += static_cast<char>(std::tolower(c));
            } else {
                if (currentWord.length() >= 2 && stopWords.find(currentWord) == stopWords.end()) {
                    terms.push_back(currentWord);
                }
                currentWord.clear();
            }
            i++;
        } else if (c >= 0xE0 && c < 0xF0) {
            if (currentWord.length() >= 2 && stopWords.find(currentWord) == stopWords.end()) {
                terms.push_back(currentWord);
            }
            currentWord.clear();
            if (i + 3 <= query.size()) {
                String ch = query.substr(i, 3);
                if (stopWords.find(ch) == stopWords.end()) {
                    terms.push_back(ch);
                }
                i += 3;
            } else {
                i++;
            }
        } else {
            if (currentWord.length() >= 2 && stopWords.find(currentWord) == stopWords.end()) {
                terms.push_back(currentWord);
            }
            currentWord.clear();
            i += (c >= 0xC0 && c < 0xE0) ? 2 : 1;
        }
    }
    if (currentWord.length() >= 2 && stopWords.find(currentWord) == stopWords.end()) {
        terms.push_back(currentWord);
    }

    // Deduplicate
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    return terms;
}

std::vector<std::pair<String, int>>
RerankerEngine::findTermPositions(const String& term, const String& text) const {
    std::vector<std::pair<String, int>> results;
    size_t pos = 0;
    while ((pos = text.find(term, pos)) != String::npos) {
        results.push_back({term, static_cast<int>(pos)});
        pos += term.length();
    }
    return results;
}

float RerankerEngine::cosineSimilarity(
    const std::vector<float>& a, const std::vector<float>& b
) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    return denom < 1e-10f ? 0.0f : dot / denom;
}

} // namespace ontology
