#pragma once

#include "GraphDatabase.hpp"
#include <curl/curl.h>
#include <mutex>
#include <condition_variable>
#include <stack>

namespace ontology {

// ============================================================================
// Neo4j 客户端
// ============================================================================

class Neo4jClient : public GraphDatabase {
public:
    struct Config {
        String uri = "bolt://localhost:7687";
        String username = "neo4j";
        String password = "";
        int connectionTimeout = 30;
        int maxConnectionPoolSize = 100;
    };

    explicit Neo4jClient(const Config& config);
    ~Neo4jClient();

    bool connect() override;
    void disconnect() override;

    // GraphDatabase 接口实现
    bool createNode(const String& id, const String& label, const Json& properties) override;
    bool updateNode(const String& id, const Json& properties) override;
    bool deleteNode(const String& id) override;
    std::optional<Json> getNode(const String& id) override;

    bool createRelation(const String& from, const String& type, const String& to, const Json& properties) override;
    bool deleteRelation(const String& from, const String& type, const String& to) override;
    std::vector<Json> getRelations(const String& from, const String& type = "") override;

    std::vector<std::vector<Json>> findPath(const String& from, const String& to, int maxDepth = 4) override;

    std::vector<Json> query(const String& cypher) override;
    std::vector<Json> query(const Json& querySpec) override;

    bool batchCreate(const std::vector<Triple>& triples) override;

    bool isConnected() const override;
    String getStatus() const override;

private:
    Config config_;
    bool connected_ = false;
    int currentTransactionId_ = -1;
    String httpUri_;

    // HTTP 方法
    bool httpGet(const String& url, String& response);
    bool httpPost(const String& url, const String& body, String& response);
    bool httpDelete(const String& url, String& response);

    // Cypher 执行
    bool runCypher(const String& cypher, const Json& params, String& response);
    String executeCypher(const String& cypher, const Json& params = {});

    // 扩展方法
    std::vector<Json> findNodes(const String& label, const Json& conditions);
    std::vector<Json> getOutgoingRelations(const String& nodeId, const String& type = "");
    std::vector<Json> getIncomingRelations(const String& nodeId, const String& type = "");
    size_t nodeCount();
    size_t relationCount();

    // 事务支持
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    std::vector<Json> executeInTransaction(const std::vector<String>& cyphers);
    bool batchCreateWithTransaction(const std::vector<Triple>& triples);

    // CURL 回调
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

// Neo4j 连接池
class Neo4jConnectionPool {
public:
    Neo4jConnectionPool(const Neo4jClient::Config& config, int poolSize = 10);
    std::shared_ptr<Neo4jClient> acquire();
    void release(std::shared_ptr<Neo4jClient> client);
    size_t availableConnections() const;

private:
    Neo4jClient::Config config_;
    int maxPoolSize_;
    std::stack<std::shared_ptr<Neo4jClient>> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace ontology
