#pragma once

#include "VectorDatabase.hpp"
#include <curl/curl.h>
#include <optional>

namespace ontology {

// ============================================================================
// Qdrant 客户端
// ============================================================================

class QdrantClient : public VectorDatabase {
public:
    struct Config {
        String host = "localhost";
        int port = 6333;
        String apiKey;
        bool useHttps = false;
    };

    // 向量记录结构
    struct VectorRecord {
        String id;
        std::vector<float> vector;
        Json metadata;
    };

    explicit QdrantClient(const Config& config);
    ~QdrantClient();

    bool connect();
    void disconnect();

    bool createCollection(const String& name, int dimension, const String& metric = "cosine") override;
    bool dropCollection(const String& name) override;
    bool hasCollection(const String& name) override;

    bool insert(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata = {}) override;
    bool update(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata = {}) override;
    bool remove(const String& collection, const String& id) override;

    std::vector<SearchResult> search(
        const String& collection,
        const std::vector<float>& query,
        int topK = 10,
        const Json& filter = {}
    ) override;

    bool batchInsert(const String& collection, const std::vector<std::pair<String, std::vector<float>>>& vectors) override;

    bool isConnected() const override;

private:
    Config config_;
    bool connected_ = false;

    String baseUrl() const;
    bool httpGet(const String& url, String& response);
    bool httpPost(const String& url, const String& body, String& response);
    bool httpPut(const String& url, const String& body, String& response);
    bool httpDelete(const String& url, String& response);
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // Helper methods
    bool insertBatch(const String& collection, const std::vector<VectorRecord>& records);
    std::vector<std::vector<SearchResult>> searchBatch(
        const String& collection,
        const std::vector<std::vector<float>>& queries,
        int topK = 10
    );
    std::optional<VectorRecord> get(const String& collection, const String& id);
    size_t count(const String& collection);
    bool createIndex(const String& collection, const String& field, const String& indexType = "keyword");
};

} // namespace ontology
