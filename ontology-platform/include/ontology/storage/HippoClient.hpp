#pragma once

#include "VectorDatabase.hpp"
#include <curl/curl.h>
#include <chrono>

namespace ontology {

// ============================================================================
// Hippo 客户端 - 星环向量数据库
// API: RESTful API, 类SQL语法
// 认证: SASL 认证 + SSL/TLS
// ============================================================================

class HippoClient : public VectorDatabase {
public:
    struct Config {
        String host = "localhost";
        int port = 9200;            // Hippo HTTP API 端口
        String username;
        String password;
        String token;               // 认证 Token (可选)
        bool useHttps = false;
        bool enableSsl = false;     // SSL/TLS 加密传输
        String sslCertPath;         // SSL 证书路径
        int connectionTimeout = 30;
        int searchTimeout = 60;     // 向量搜索超时(秒)
        int maxPoolSize = 10;
    };

    // 向量记录
    struct HippoVectorRecord {
        String id;
        std::vector<float> vector;
        Json metadata;
    };

    explicit HippoClient(const Config& config);
    ~HippoClient();

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    // VectorDatabase 接口实现
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

    // Hippo 扩展方法
    /// 批量插入 (带元数据)
    bool insertBatch(const String& collection, const std::vector<HippoVectorRecord>& records);

    /// 批量搜索
    std::vector<std::vector<SearchResult>> searchBatch(
        const String& collection,
        const std::vector<std::vector<float>>& queries,
        int topK = 10
    );

    /// 获取向量记录
    std::optional<HippoVectorRecord> get(const String& collection, const String& id);

    /// 获取集合统计
    size_t count(const String& collection);

    /// 创建索引
    bool createIndex(const String& collection, const String& field, const String& indexType = "HNSW");

    /// 删除索引
    bool dropIndex(const String& collection, const String& field);

    /// 刷新集合 (持久化)
    bool flush(const String& collection);

    /// 加载集合到内存
    bool loadCollection(const String& collection);

    /// 释放集合内存
    bool releaseCollection(const String& collection);

    /// 获取集合统计信息
    Json getCollectionStats(const String& collection);

    /// 混合查询 (向量 + 标量过滤)
    std::vector<SearchResult> hybridSearch(
        const String& collection,
        const std::vector<float>& query,
        int topK,
        const Json& scalarFilter,
        float vectorWeight = 0.7f,
        float scalarWeight = 0.3f
    );

    /// 类 SQL 查询
    std::vector<SearchResult> sqlQuery(const String& collection, const String& sql);

    /// 认证
    bool refreshToken();

private:
    Config config_;
    bool connected_ = false;
    String authToken_;
    std::chrono::system_clock::time_point tokenExpiry_;

    // HTTP 方法
    String baseUrl() const;
    bool httpGet(const String& url, String& response);
    bool httpPost(const String& url, const String& body, String& response);
    bool httpPut(const String& url, const String& body, String& response);
    bool httpDelete(const String& url, String& response);

    // 认证
    bool authenticate();
    void applyAuthHeaders(struct curl_slist*& headers);

    // CURL 回调
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // 内部辅助
    String metricToHippoType(const String& metric) const;
};

} // namespace ontology
