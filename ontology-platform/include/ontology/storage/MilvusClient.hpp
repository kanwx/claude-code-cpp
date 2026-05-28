#pragma once

#include "VectorDatabase.hpp"
#include <curl/curl.h>
#include <optional>

namespace ontology {

// ============================================================================
// Milvus 客户端
// ============================================================================

class MilvusClient : public VectorDatabase {
public:
    struct Config {
        String host = "localhost";
        int port = 19530;
        String username;
        String password;
        int timeout = 30;
    };

    // 向量记录结构
    struct VectorRecord {
        String id;
        std::vector<float> vector;
        Json metadata;
    };

    explicit MilvusClient(const Config& config);
    ~MilvusClient();

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

    // HTTP 方法
    bool httpGet(const String& url, String& response);
    bool httpPost(const String& url, const String& body, String& response);
    bool httpDelete(const String& url, String& response);

    // 扩展方法
    bool insertBatch(const String& collection, const std::vector<VectorRecord>& records);
    std::vector<std::vector<SearchResult>> searchBatch(
        const String& collection,
        const std::vector<std::vector<float>>& queries,
        int topK = 10
    );
    std::optional<VectorRecord> get(const String& collection, const String& id);
    size_t count(const String& collection);
    bool createIndex(const String& collection, const String& field, const String& indexType = "IVF_FLAT");

    // 高级方法
    bool batchInsertOptimized(const String& collection, const std::vector<VectorRecord>& records, int batchSize = 1000);
    bool flush(const String& collection);
    bool loadCollection(const String& collection);
    bool releaseCollection(const String& collection);
    Json getCollectionStats(const String& collection);

    // 内部方法
    String baseUrl() const { return "http://" + config_.host + ":" + std::to_string(config_.port); }
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

} // namespace ontology
