#include <ontology/EmbeddingService.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <sstream>
#include <httplib.h>

namespace ontology {

// ============================================================================
// HashFingerprintBackend
// ============================================================================

std::vector<String> HashFingerprintBackend::tokenize(const String& text) const {
    std::vector<String> tokens;
    std::string current;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (current.length() >= 2) tokens.push_back(current);
            current.clear();
        }
        if (tokens.size() >= 500) break;
    }
    if (current.length() >= 2) tokens.push_back(current);
    return tokens;
}

uint32_t HashFingerprintBackend::fnv1a(const String& s, uint32_t seed) {
    uint32_t h = seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x01000193;
    }
    return h;
}

void HashFingerprintBackend::hashNgram(
    const String& ngram, std::vector<float>& acc, uint32_t seed
) const {
    uint32_t h = fnv1a(ngram, seed);
    int idx = static_cast<int>(h % static_cast<uint32_t>(dimension_));
    float sign = ((h / static_cast<uint32_t>(dimension_)) % 2 == 0) ? 1.0f : -1.0f;
    acc[idx] += sign;
}

std::vector<float> HashFingerprintBackend::normalize(const std::vector<float>& v) {
    float sum = 0.0f;
    for (float x : v) sum += x * x;
    if (sum < 1e-10f) return v;
    float invNorm = 1.0f / std::sqrt(sum);
    std::vector<float> result(v.size());
    for (size_t i = 0; i < v.size(); i++) result[i] = v[i] * invNorm;
    return result;
}

std::vector<float> HashFingerprintBackend::embed(const String& text) {
    std::vector<float> acc(dimension_, 0.0f);
    auto tokens = tokenize(text);
    if (tokens.empty()) return acc;

    for (const auto& w : tokens) hashNgram(w, acc, 0x811c9dc5);
    for (size_t i = 0; i + 1 < tokens.size(); i++)
        hashNgram(tokens[i] + "_" + tokens[i + 1], acc, 0x1234abcd);
    for (size_t i = 0; i + 2 < tokens.size(); i++)
        hashNgram(tokens[i] + "_" + tokens[i + 1] + "_" + tokens[i + 2], acc, 0x5678ef01);

    return normalize(acc);
}

std::vector<std::vector<float>> HashFingerprintBackend::embedBatch(
    const std::vector<String>& texts
) {
    std::vector<std::vector<float>> results;
    results.reserve(texts.size());
    for (const auto& text : texts) results.push_back(embed(text));
    return results;
}

// ============================================================================
// OpenAIEmbeddingBackend
// ============================================================================

OpenAIEmbeddingBackend::OpenAIEmbeddingBackend(
    const String& apiKey, const String& endpoint,
    const String& model, int dimension, int maxBatchSize,
    const String& apiPath, int timeoutMs
)
    : apiKey_(apiKey)
    , endpoint_(endpoint)
    , model_(model)
    , apiPath_(apiPath.empty() ? "/v1/embeddings" : apiPath)
    , dimension_(dimension)
    , maxBatchSize_(maxBatchSize)
    , timeoutMs_(timeoutMs > 0 ? timeoutMs : 60000)
{
}

std::vector<float> OpenAIEmbeddingBackend::normalize(const std::vector<float>& v) {
    float sum = 0.0f;
    for (float x : v) sum += x * x;
    if (sum < 1e-10f) return v;
    float invNorm = 1.0f / std::sqrt(sum);
    std::vector<float> result(v.size());
    for (size_t i = 0; i < v.size(); i++) result[i] = v[i] * invNorm;
    return result;
}

bool OpenAIEmbeddingBackend::isAvailable() const {
    return !endpoint_.empty();
}

std::vector<float> OpenAIEmbeddingBackend::embed(const String& text) {
    auto results = callApi({text});
    return results.empty() ? std::vector<float>() : results[0];
}

std::vector<std::vector<float>> OpenAIEmbeddingBackend::embedBatch(
    const std::vector<String>& texts
) {
    std::vector<std::vector<float>> allResults;
    allResults.reserve(texts.size());

    for (size_t i = 0; i < texts.size(); i += maxBatchSize_) {
        size_t end = std::min(i + maxBatchSize_, texts.size());
        std::vector<String> batch(texts.begin() + i, texts.begin() + end);
        auto result = callApi(batch);
        if (result.size() == batch.size()) {
            for (auto& emb : result) allResults.push_back(std::move(emb));
        } else {
            // Fallback: embed individually
            for (size_t j = i; j < end; j++) {
                auto single = callApi({texts[j]});
                if (!single.empty()) allResults.push_back(std::move(single[0]));
                else allResults.push_back(std::vector<float>(dimension_, 0.0f));
            }
        }
    }

    return allResults;
}

std::vector<std::vector<float>> OpenAIEmbeddingBackend::callApi(const std::vector<String>& texts) {
    try {
        String host = endpoint_;
        int port = 443;

        if (host.substr(0, 8) == "https://") host = host.substr(8);
        else if (host.substr(0, 7) == "http://") { host = host.substr(7); port = 80; }

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        Json inputArray = Json::array();
        for (const auto& t : texts) inputArray.push_back(t);

        Json body;
        body["model"] = model_;
        body["input"] = inputArray;

        httplib::Client client(host, port);
        client.set_read_timeout(timeoutMs_ / 1000);

        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };
        if (!apiKey_.empty()) {
            headers.emplace("Authorization", "Bearer " + apiKey_);
        }

        auto res = client.Post(apiPath_, headers, body.dump(), "application/json");
        if (!res || res->status != 200) return {};

        Json resp = Json::parse(res->body);
        if (!resp.contains("data")) return {};

        std::vector<std::pair<int, std::vector<float>>> indexed;
        for (const auto& item : resp["data"]) {
            int idx = item.value("index", 0);
            std::vector<float> emb;
            for (const auto& v : item["embedding"]) emb.push_back(v.get<float>());
            if (static_cast<int>(emb.size()) > dimension_) emb.resize(dimension_);
            else if (static_cast<int>(emb.size()) < dimension_) emb.resize(dimension_, 0.0f);
            indexed.push_back({idx, normalize(emb)});
        }

        std::sort(indexed.begin(), indexed.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        if (texts.size() == 1) {
            return indexed.empty() ? std::vector<std::vector<float>>() : std::vector<std::vector<float>>{indexed[0].second};
        }

        // For batch, return all embeddings in order
        std::vector<std::vector<float>> results;
        results.reserve(indexed.size());
        for (auto& [idx, emb] : indexed) {
            results.push_back(std::move(emb));
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

// ============================================================================
// OllamaEmbeddingBackend
// ============================================================================

OllamaEmbeddingBackend::OllamaEmbeddingBackend(
    const String& endpoint, const String& model, int dimension
)
    : endpoint_(endpoint)
    , model_(model)
    , dimension_(dimension)
{
}

std::vector<float> OllamaEmbeddingBackend::normalize(const std::vector<float>& v) {
    float sum = 0.0f;
    for (float x : v) sum += x * x;
    if (sum < 1e-10f) return v;
    float invNorm = 1.0f / std::sqrt(sum);
    std::vector<float> result(v.size());
    for (size_t i = 0; i < v.size(); i++) result[i] = v[i] * invNorm;
    return result;
}

bool OllamaEmbeddingBackend::isAvailable() const {
    // Check if Ollama is running
    try {
        String host = endpoint_;
        int port = 11434;
        if (host.substr(0, 7) == "http://") host = host.substr(7);
        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }
        httplib::Client client(host, port);
        client.set_read_timeout(5);
        auto res = client.Get("/");
        return res && res->status == 200;
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return false;
    }
}

std::vector<float> OllamaEmbeddingBackend::embed(const String& text) {
    return callApi(text);
}

std::vector<std::vector<float>> OllamaEmbeddingBackend::embedBatch(
    const std::vector<String>& texts
) {
    // Ollama doesn't support true batch embedding, so we call individually
    std::vector<std::vector<float>> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        auto emb = callApi(text);
        if (!emb.empty()) results.push_back(emb);
        else results.push_back(std::vector<float>(dimension_, 0.0f));
    }
    return results;
}

std::vector<float> OllamaEmbeddingBackend::callApi(const String& text) {
    try {
        String host = endpoint_;
        int port = 11434;
        if (host.substr(0, 7) == "http://") host = host.substr(7);
        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        Json body;
        body["model"] = model_;
        body["prompt"] = text;

        httplib::Client client(host, port);
        client.set_read_timeout(30);

        auto res = client.Post("/api/embeddings", body.dump(), "application/json");
        if (!res || res->status != 200) return {};

        Json resp = Json::parse(res->body);
        if (!resp.contains("embedding")) return {};

        std::vector<float> emb;
        for (const auto& v : resp["embedding"]) emb.push_back(v.get<float>());

        if (static_cast<int>(emb.size()) > dimension_) emb.resize(dimension_);
        else if (static_cast<int>(emb.size()) < dimension_) emb.resize(dimension_, 0.0f);

        return normalize(emb);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return {};
    }
}

// ============================================================================
// EmbeddingService (Facade)
// ============================================================================

EmbeddingService::EmbeddingService(const EmbeddingServiceConfig& config)
    : config_(config)
{
    initializePrimaryBackend();
    fallbackBackend_ = createHashBackend(config.dimension);
}

void EmbeddingService::initializePrimaryBackend() {
    switch (config_.backend) {
        case EmbeddingBackend::HashFingerprint:
            primaryBackend_ = createHashBackend(config_.dimension);
            break;
        case EmbeddingBackend::OpenAI:
            primaryBackend_ = createOpenAIBackend(
                config_.apiKey, config_.endpoint, config_.model,
                config_.dimension, config_.maxBatchSize);
            break;
        case EmbeddingBackend::Ollama:
            primaryBackend_ = createOllamaBackend(
                config_.ollamaEndpoint, config_.ollamaModel, config_.dimension);
            break;
        default:
            primaryBackend_ = createHashBackend(config_.dimension);
            break;
    }
}

std::shared_ptr<EmbeddingBackendInterface>
EmbeddingService::createHashBackend(int dim) {
    return std::make_shared<HashFingerprintBackend>(dim);
}

std::shared_ptr<EmbeddingBackendInterface>
EmbeddingService::createOpenAIBackend(
    const String& apiKey, const String& endpoint,
    const String& model, int dim, int maxBatch,
    const String& apiPath, int timeoutMs
) {
    return std::make_shared<OpenAIEmbeddingBackend>(apiKey, endpoint, model, dim, maxBatch, apiPath, timeoutMs);
}

std::shared_ptr<EmbeddingBackendInterface>
EmbeddingService::createOllamaBackend(
    const String& endpoint, const String& model, int dim
) {
    return std::make_shared<OllamaEmbeddingBackend>(endpoint, model, dim);
}

EmbeddingResult EmbeddingService::embed(const String& text, const String& backendName) {
    EmbeddingResult result;
    auto start = std::chrono::high_resolution_clock::now();

    // Determine which backend to use
    std::shared_ptr<EmbeddingBackendInterface> backend;
    if (!backendName.empty() && customBackends_.count(backendName)) {
        backend = customBackends_[backendName];
    } else if (!backendName.empty() && backendName == primaryBackend_->name()) {
        backend = primaryBackend_;
    } else if (backendName.empty()) {
        backend = primaryBackend_;
    }

    // Check cache first
    String bName = backend ? backend->name() : "none";
    if (config_.enableCache && backend) {
        String key = computeCacheKey(text, bName);
        auto cached = cacheGet(key);
        if (cached) {
            result.embedding = *cached;
            result.fromCache = true;
            result.backendName = bName;
            auto end = std::chrono::high_resolution_clock::now();
            result.latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
            return result;
        }
    }

    // Try selected backend
    if (backend && backend->isAvailable()) {
        result.embedding = backend->embed(text);
        result.backendName = bName;
    }

    // If named backend failed, try primary as fallback
    if (result.embedding.empty() && !backendName.empty() && primaryBackend_ && primaryBackend_->isAvailable()) {
        result.embedding = primaryBackend_->embed(text);
        result.backendName = primaryBackend_->name();
    }

    // Fallback to hash
    if (result.embedding.empty() && fallbackBackend_) {
        result.embedding = fallbackBackend_->embed(text);
        result.backendName = fallbackBackend_->name();
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.latencyMs = std::chrono::duration<double, std::milli>(end - start).count();

    // Cache result
    if (config_.enableCache && !result.embedding.empty()) {
        String key = computeCacheKey(text, result.backendName);
        cacheSet(key, result.embedding);
    }

    return result;
}

std::vector<EmbeddingResult> EmbeddingService::embedBatch(
    const std::vector<String>& texts, const String& backendName
) {
    std::vector<EmbeddingResult> results;
    results.reserve(texts.size());

    // Determine which backend to use
    std::shared_ptr<EmbeddingBackendInterface> backend;
    if (!backendName.empty() && customBackends_.count(backendName)) {
        backend = customBackends_[backendName];
    } else if (backendName.empty()) {
        backend = primaryBackend_;
    }

    bool useBackend = backend && backend->isAvailable();

    if (useBackend) {
        String bName = backend->name();
        // Check which are cached
        std::vector<int> uncachedIndices;
        std::vector<String> uncachedTexts;

        for (size_t i = 0; i < texts.size(); i++) {
            if (config_.enableCache) {
                String key = computeCacheKey(texts[i], bName);
                auto cached = cacheGet(key);
                if (cached) {
                    EmbeddingResult r;
                    r.embedding = *cached;
                    r.fromCache = true;
                    r.backendName = bName;
                    results.push_back(r);
                    continue;
                }
            }
            uncachedIndices.push_back(static_cast<int>(i));
            uncachedTexts.push_back(texts[i]);
            results.push_back(EmbeddingResult{});  // Placeholder
        }

        if (!uncachedTexts.empty()) {
            auto batch = backend->embedBatch(uncachedTexts);
            if (batch.size() == uncachedTexts.size()) {
                for (size_t j = 0; j < uncachedTexts.size(); j++) {
                    auto& r = results[uncachedIndices[j]];
                    r.embedding = batch[j];
                    r.backendName = bName;
                    if (config_.enableCache && !r.embedding.empty()) {
                        String key = computeCacheKey(uncachedTexts[j], r.backendName);
                        cacheSet(key, r.embedding);
                    }
                }
            } else {
                // Batch failed, embed individually with fallback
                for (size_t j = 0; j < uncachedTexts.size(); j++) {
                    auto& r = results[uncachedIndices[j]];
                    if (fallbackBackend_) {
                        r.embedding = fallbackBackend_->embed(uncachedTexts[j]);
                        r.backendName = fallbackBackend_->name();
                    }
                }
            }
        }
    } else {
        // Use fallback for all
        for (const auto& text : texts) {
            EmbeddingResult r;
            if (fallbackBackend_) {
                r.embedding = fallbackBackend_->embed(text);
                r.backendName = fallbackBackend_->name();
            }
            results.push_back(r);
        }
    }

    return results;
}

std::vector<float> EmbeddingService::embedVector(const String& text) {
    return embed(text).embedding;
}

std::vector<std::vector<float>> EmbeddingService::embedBatchVectors(
    const std::vector<String>& texts
) {
    auto results = embedBatch(texts);
    std::vector<std::vector<float>> vectors;
    vectors.reserve(results.size());
    for (auto& r : results) vectors.push_back(std::move(r.embedding));
    return vectors;
}

void EmbeddingService::setBackend(EmbeddingBackend backend) {
    config_.backend = backend;
    initializePrimaryBackend();
}

void EmbeddingService::setConfig(const EmbeddingServiceConfig& config) {
    config_ = config;
    initializePrimaryBackend();
}

void EmbeddingService::registerBackend(
    const String& name, std::shared_ptr<EmbeddingBackendInterface> backend
) {
    customBackends_[name] = backend;
}

bool EmbeddingService::hasBackend(const String& name) const {
    return customBackends_.count(name) > 0;
}

std::shared_ptr<EmbeddingBackendInterface> EmbeddingService::getCustomBackend(const String& name) const {
    auto it = customBackends_.find(name);
    return it != customBackends_.end() ? it->second : nullptr;
}

String EmbeddingService::currentBackendName() const {
    return primaryBackend_ ? primaryBackend_->name() : "none";
}

void EmbeddingService::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    cacheHits_ = 0;
    cacheMisses_ = 0;
}

float EmbeddingService::cacheHitRate() const {
    size_t total = cacheHits_ + cacheMisses_;
    return total > 0 ? static_cast<float>(cacheHits_) / total : 0.0f;
}

bool EmbeddingService::isAvailable() const {
    return (primaryBackend_ && primaryBackend_->isAvailable()) ||
           (fallbackBackend_ && fallbackBackend_->isAvailable());
}

Json EmbeddingService::getStats() const {
    Json j;
    j["backend"] = currentBackendName();
    j["dimension"] = config_.dimension;
    j["cacheSize"] = static_cast<int>(cache_.size());
    j["cacheHitRate"] = cacheHitRate();
    return j;
}

// ============================================================================
// Cache Operations
// ============================================================================

std::optional<std::vector<float>> EmbeddingService::cacheGet(const String& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        cacheMisses_++;
        return std::nullopt;
    }
    if (currentTimeMs() > it->second.expiryMs) {
        cache_.erase(it);
        cacheMisses_++;
        return std::nullopt;
    }
    cacheHits_++;
    return it->second.embedding;
}

void EmbeddingService::cacheSet(
    const String& key, const std::vector<float>& embedding
) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    if (cache_.size() >= static_cast<size_t>(config_.cacheMaxSize)) {
        // Evict oldest entries (simple approach)
        auto it = cache_.begin();
        int toRemove = static_cast<int>(cache_.size()) - config_.cacheMaxSize / 2;
        while (it != cache_.end() && toRemove > 0) {
            it = cache_.erase(it);
            toRemove--;
        }
    }

    cache_[key] = {embedding, currentTimeMs() + config_.cacheTtlSeconds * 1000LL};
}

String EmbeddingService::computeCacheKey(
    const String& text, const String& backendName
) {
    return backendName + ":" + std::to_string(std::hash<String>{}(text));
}

int64_t EmbeddingService::currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace ontology
