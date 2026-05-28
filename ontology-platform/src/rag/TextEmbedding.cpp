#include <ontology/TextEmbedding.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace ontology {

TextEmbedder::TextEmbedder(const Config& config) : config_(config) {}

TextEmbedder::TextEmbedder(std::shared_ptr<EmbeddingService> service)
    : embeddingService_(service)
{
    if (service) {
        config_.dimension = service->dimension();
        config_.method = "embedding_service";
    }
}

std::vector<float> TextEmbedder::embed(const String& text) {
    if (embeddingService_) {
        return embeddingService_->embedVector(text);
    }
    if (config_.method == "openai" && !config_.apiKey.empty()) {
        auto result = callExternalApi(text);
        if (!result.empty()) return result;
    }
    return hashFingerprint(text);
}

std::vector<std::vector<float>> TextEmbedder::embedBatch(const std::vector<String>& texts) {
    if (embeddingService_) {
        return embeddingService_->embedBatchVectors(texts);
    }
    if (config_.method == "openai" && !config_.apiKey.empty() && !texts.empty()) {
        auto results = callExternalApiBatch(texts);
        if (!results.empty() && results.size() == texts.size()) return results;
    }
    std::vector<std::vector<float>> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        results.push_back(hashFingerprint(text));
    }
    return results;
}

bool TextEmbedder::isExternalAvailable() const {
    if (embeddingService_) return embeddingService_->isAvailable();
    return config_.method != "hash_fingerprint" && !config_.apiKey.empty();
}

// ============================================================================
// Hash-based Semantic Fingerprint
// ============================================================================

std::vector<String> TextEmbedder::tokenize(const String& text) const {
    std::vector<String> tokens;
    std::string current;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (current.length() >= 2) {
                tokens.push_back(current);
            }
            current.clear();
        }
        if (tokens.size() >= 500) break;
    }
    if (current.length() >= 2) {
        tokens.push_back(current);
    }
    return tokens;
}

uint32_t TextEmbedder::fnv1a(const String& s, uint32_t seed) {
    uint32_t h = seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x01000193;
    }
    return h;
}

void TextEmbedder::hashNgram(const String& ngram, std::vector<float>& acc, uint32_t seed) const {
    uint32_t h = fnv1a(ngram, seed);
    int dim = config_.dimension;
    int idx = static_cast<int>(h % static_cast<uint32_t>(dim));
    float sign = ((h / static_cast<uint32_t>(dim)) % 2 == 0) ? 1.0f : -1.0f;
    acc[idx] += sign;
}

std::vector<float> TextEmbedder::hashFingerprint(const String& text) const {
    std::vector<float> acc(config_.dimension, 0.0f);
    auto tokens = tokenize(text);
    if (tokens.empty()) return acc;

    // Unigram (weight 1.0)
    for (const auto& w : tokens) {
        hashNgram(w, acc, 0x811c9dc5);
    }

    // Bigram (weight 0.7)
    for (size_t i = 0; i + 1 < tokens.size(); i++) {
        hashNgram(tokens[i] + "_" + tokens[i + 1], acc, 0x1234abcd);
    }

    // Trigram (weight 0.4)
    for (size_t i = 0; i + 2 < tokens.size(); i++) {
        hashNgram(tokens[i] + "_" + tokens[i + 1] + "_" + tokens[i + 2], acc, 0x5678ef01);
    }

    return normalize(acc);
}

std::vector<float> TextEmbedder::normalize(const std::vector<float>& v) {
    float sum = 0.0f;
    for (float x : v) sum += x * x;
    if (sum < 1e-10f) return v;

    float invNorm = 1.0f / std::sqrt(sum);
    std::vector<float> result(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        result[i] = v[i] * invNorm;
    }
    return result;
}

float TextEmbedder::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
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

// ============================================================================
// External API (OpenAI)
// ============================================================================

std::vector<float> TextEmbedder::callExternalApi(const String& text) {
    try {
        String host = config_.endpoint;
        int port = 443;
        bool useHttps = true;

        if (host.substr(0, 8) == "https://") {
            host = host.substr(8);
        } else if (host.substr(0, 7) == "http://") {
            host = host.substr(7);
            useHttps = false;
            port = 80;
        }

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        Json body;
        body["model"] = config_.model;
        body["input"] = text;

        httplib::Client client(host, port);
        client.set_read_timeout(30);

        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + config_.apiKey}
        };

        auto res = client.Post("/v1/embeddings", headers, body.dump(), "application/json");
        if (!res || res->status != 200) return {};

        Json resp = Json::parse(res->body);
        if (!resp.contains("data") || resp["data"].empty()) return {};

        auto& emb = resp["data"][0]["embedding"];
        if (!emb.is_array()) return {};

        std::vector<float> result;
        for (const auto& v : emb) {
            result.push_back(v.get<float>());
        }

        // Truncate or pad to configured dimension
        if (static_cast<int>(result.size()) > config_.dimension) {
            result.resize(config_.dimension);
        } else if (static_cast<int>(result.size()) < config_.dimension) {
            result.resize(config_.dimension, 0.0f);
        }

        return normalize(result);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return {};
    }
}

std::vector<std::vector<float>> TextEmbedder::callExternalApiBatch(const std::vector<String>& texts) {
    // Process in batches
    std::vector<std::vector<float>> allResults;
    allResults.reserve(texts.size());

    for (size_t i = 0; i < texts.size(); i += config_.maxBatchSize) {
        size_t end = std::min(i + config_.maxBatchSize, texts.size());

        try {
            String host = config_.endpoint;
            int port = 443;

            if (host.substr(0, 8) == "https://") host = host.substr(8);
            else if (host.substr(0, 7) == "http://") { host = host.substr(7); port = 80; }

            auto colonPos = host.find(':');
            if (colonPos != String::npos) {
                port = std::stoi(host.substr(colonPos + 1));
                host = host.substr(0, colonPos);
            }

            Json inputArray = Json::array();
            for (size_t j = i; j < end; j++) {
                inputArray.push_back(texts[j]);
            }

            Json body;
            body["model"] = config_.model;
            body["input"] = inputArray;

            httplib::Client client(host, port);
            client.set_read_timeout(60);

            httplib::Headers headers = {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + config_.apiKey}
            };

            auto res = client.Post("/v1/embeddings", headers, body.dump(), "application/json");
            if (!res || res->status != 200) {
                // Fallback to hash for this batch
                for (size_t j = i; j < end; j++) {
                    allResults.push_back(hashFingerprint(texts[j]));
                }
                continue;
            }

            Json resp = Json::parse(res->body);
            if (!resp.contains("data")) {
                for (size_t j = i; j < end; j++) {
                    allResults.push_back(hashFingerprint(texts[j]));
                }
                continue;
            }

            // Sort by index (API may return in different order)
            std::vector<std::pair<int, std::vector<float>>> indexed;
            for (const auto& item : resp["data"]) {
                int idx = item.value("index", 0);
                std::vector<float> emb;
                for (const auto& v : item["embedding"]) {
                    emb.push_back(v.get<float>());
                }
                if (static_cast<int>(emb.size()) > config_.dimension) {
                    emb.resize(config_.dimension);
                } else if (static_cast<int>(emb.size()) < config_.dimension) {
                    emb.resize(config_.dimension, 0.0f);
                }
                indexed.push_back({idx, normalize(emb)});
            }

            std::sort(indexed.begin(), indexed.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

            for (auto& [idx, emb] : indexed) {
                allResults.push_back(std::move(emb));
            }
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("RAG JSON error: {}", e.what());
            for (size_t j = i; j < end; j++) {
                allResults.push_back(hashFingerprint(texts[j]));
            }
        } catch (const std::exception& e) {
            spdlog::error("RAG error: {}", e.what());
            for (size_t j = i; j < end; j++) {
                allResults.push_back(hashFingerprint(texts[j]));
            }
        }
    }

    return allResults;
}

} // namespace ontology
