#pragma once

#include "Core.hpp"
#include <curl/curl.h>
#include <memory>
#include <optional>
#include <unordered_set>

namespace ontology {

// 前向声明和类型别名
class HybridStorage;
class GraphDatabase;
class VectorDatabase;
using StoragePtr = std::shared_ptr<HybridStorage>;
using GraphDatabasePtr = std::shared_ptr<GraphDatabase>;
using VectorDatabasePtr = std::shared_ptr<VectorDatabase>;

// ============================================================================
// TripleStore - 内存三元组存储
// ============================================================================

class TripleStore {
public:
    /// 添加三元组
    bool add(const Triple& triple);

    /// 移除三元组
    bool remove(const Triple& triple);

    /// 检查三元组是否存在
    bool contains(const Triple& triple) const;

    /// 按主语查询
    std::vector<Triple> findBySubject(const String& subject) const;

    /// 按谓词查询
    std::vector<Triple> findByPredicate(const String& predicate) const;

    /// 按客体查询
    std::vector<Triple> findByObject(const String& object) const;

    /// 按主语-谓词查询
    std::vector<Triple> findBySP(const String& subject, const String& predicate) const;

    /// 按谓词-客体查询
    std::vector<Triple> findByPO(const String& predicate, const String& object) const;

    /// 按主语-客体查询
    std::vector<Triple> findBySO(const String& subject, const String& object) const;

    /// 精确查找
    std::optional<Triple> find(const String& subject, const String& predicate, const String& object) const;

    /// 模式查询
    struct TriplePattern {
        String subject;
        String predicate;
        String object;
        bool subjectIsVar = false;
        bool predicateIsVar = false;
        bool objectIsVar = false;
    };
    std::vector<Triple> query(const TriplePattern& pattern) const;

    /// 时间点查询: 返回在指定时间戳有效的三元组 (validFrom <= timestamp <= validTo)
    std::vector<Triple> queryAtTime(const TriplePattern& pattern, const String& timestamp) const;

    /// 时间范围查询: 返回在指定时间范围内有效的三元组 (与 [from, to] 有重叠)
    std::vector<Triple> queryTemporalRange(const TriplePattern& pattern, const String& from, const String& to) const;

    /// 路径查询
    std::vector<std::vector<String>> findPath(
        const String& from,
        const String& to,
        const String& predicate = "",
        int maxDepth = 10
    ) const;

    /// 工具方法
    std::vector<String> getObjects(const String& subject, const String& predicate) const;
    std::vector<String> getSubjects(const String& predicate, const String& object) const;
    std::vector<String> getAllSubjects() const;
    std::vector<String> getAllPredicates() const;
    std::vector<String> getAllObjects() const;

    size_t count() const;
    void clear();

    const std::vector<Triple>& all() const { return triples_; }

private:
    std::vector<Triple> triples_;
    std::unordered_map<String, std::vector<size_t>> subjectIndex_;
    std::unordered_map<String, std::vector<size_t>> predicateIndex_;
    std::unordered_map<String, std::vector<size_t>> objectIndex_;

    void rebuildIndexes();
};

// ============================================================================
// 图数据库接口
// ============================================================================

class GraphDatabase {
public:
    virtual ~GraphDatabase() = default;

    // 连接
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // 节点操作
    virtual bool createNode(const String& id, const String& label, const Json& properties) = 0;
    virtual bool updateNode(const String& id, const Json& properties) = 0;
    virtual bool deleteNode(const String& id) = 0;
    virtual std::optional<Json> getNode(const String& id) = 0;

    // 关系操作
    virtual bool createRelation(const String& from, const String& type, const String& to, const Json& properties) = 0;
    virtual bool deleteRelation(const String& from, const String& type, const String& to) = 0;
    virtual std::vector<Json> getRelations(const String& from, const String& type = "") = 0;

    // 路径查询
    virtual std::vector<std::vector<Json>> findPath(const String& from, const String& to, int maxDepth = 4) = 0;

    // 图查询
    virtual std::vector<Json> query(const String& cypher) = 0;
    virtual std::vector<Json> query(const Json& querySpec) = 0;

    // 批量操作
    virtual bool batchCreate(const std::vector<Triple>& triples) = 0;

    // 状态
    virtual String getStatus() const = 0;
};

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

// ============================================================================
// 向量数据库接口
// ============================================================================

class VectorDatabase {
public:
    virtual ~VectorDatabase() = default;

    // 连接
    virtual bool connect() = 0;
    virtual void disconnect() = 0;

    // 集合操作
    virtual bool createCollection(const String& name, int dimension, const String& metric = "cosine") = 0;
    virtual bool dropCollection(const String& name) = 0;
    virtual bool hasCollection(const String& name) = 0;

    // 向量操作
    virtual bool insert(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata = {}) = 0;
    virtual bool update(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata = {}) = 0;
    virtual bool remove(const String& collection, const String& id) = 0;

    // 搜索
    struct SearchResult {
        String id;
        float score;
        Json metadata;
        std::vector<float> vector;
    };

    virtual std::vector<SearchResult> search(
        const String& collection,
        const std::vector<float>& query,
        int topK = 10,
        const Json& filter = {}
    ) = 0;

    // 批量操作
    virtual bool batchInsert(const String& collection, const std::vector<std::pair<String, std::vector<float>>>& vectors) = 0;

    // 状态
    virtual bool isConnected() const = 0;
};

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

// Milvus 连接池
class MilvusConnectionPool {
public:
    MilvusConnectionPool(const MilvusClient::Config& config, int poolSize = 10);
    std::shared_ptr<MilvusClient> acquire();
    void release(std::shared_ptr<MilvusClient> client);
    size_t availableConnections() const;

private:
    MilvusClient::Config config_;
    int maxPoolSize_;
    std::stack<std::shared_ptr<MilvusClient>> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

// ============================================================================
// StellarDB 客户端 - 星环图数据库
// 查询语言: Transwarp Extended-OpenCypher (OpenCypher 兼容)
// 认证: TDH Guardian 服务 (用户名/密码 + Token)
// ============================================================================

class StellarDBClient : public GraphDatabase {
public:
    struct Config {
        String host = "localhost";
        int port = 8080;                // StellarDB HTTP API 端口
        String graphName = "ontology";  // 图空间名称
        String username;
        String password;
        String token;                   // TDH 认证 Token (可选, 优先于用户名密码)
        bool useHttps = false;
        int connectionTimeout = 30;
        int maxConnectionPoolSize = 50;
        int maxDepth = 10;              // 路径查询最大深度
    };

    explicit StellarDBClient(const Config& config);
    ~StellarDBClient();

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

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

    String getStatus() const override;

    // StellarDB 扩展方法
    /// 执行 OpenCypher 查询 (Transwarp Extended)
    std::vector<Json> executeCypher(const String& cypher, const Json& params = {});

    /// 获取图统计信息 (顶点数/边数)
    Json getGraphStats();

    /// 创建图空间
    bool createGraphSpace(const String& name, const Json& options = {});

    /// 删除图空间
    bool dropGraphSpace(const String& name);

    /// 列出所有图空间
    std::vector<String> listGraphSpaces();

    /// 获取子图
    std::vector<Json> getSubgraph(const String& nodeId, int depth = 1);

    /// 获取节点所有标签
    std::vector<String> getNodeLabels(const String& nodeId);

    /// 图算法: 最短路径 (StellarDB 内置分布式算法)
    std::vector<std::vector<String>> shortestPath(
        const String& from, const String& to, const String& edgeType = "", int maxDepth = 10);

    /// 图算法: 全路径
    std::vector<std::vector<String>> allPaths(
        const String& from, const String& to, const String& edgeType = "", int maxDepth = 10);

    /// 图算法: PageRank
    std::vector<std::pair<String, double>> pageRank(const String& label = "", int iterations = 20, double damping = 0.85);

    /// 图算法: 共同邻居
    std::vector<String> commonNeighbors(const String& node1, const String& node2);

    /// 图算法: 度中心性
    std::vector<std::pair<String, int>> degreeCentrality(const String& label = "", const String& direction = "both");

    /// 批量节点创建 (StellarDB 优化)
    bool batchCreateNodes(const std::vector<std::tuple<String, String, Json>>& nodes);

    /// 批量边创建 (StellarDB 优化)
    bool batchCreateEdges(const std::vector<std::tuple<String, String, String, Json>>& edges);

    /// 认证刷新 Token
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

    // Cypher 执行
    bool runCypher(const String& cypher, const Json& params, String& response);
    std::vector<Json> parseCypherResult(const String& response);

    // CURL 回调
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

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

// ============================================================================
// 混合存储 (图 + 向量)
// ============================================================================

class HybridStorage {
public:
    HybridStorage(
        GraphDatabasePtr graphDB,
        VectorDatabasePtr vectorDB
    );

    /// 初始化
    bool initialize(const String& collectionName, int embeddingDimension);

    /// 存储本体到混合存储
    bool storeOntology(const Ontology& ontology);

    /// 存储实例 (图节点 + 向量嵌入)
    bool storeIndividual(const Individual& ind, const std::vector<float>& embedding);

    /// 存储三元组
    bool storeTriple(const Triple& triple);
    bool addTriple(const Triple& triple);
    bool removeTriple(const Triple& triple);

    /// 三元组查询
    std::optional<Triple> findTriple(const String& subject, const String& predicate, const String& object) const;
    std::vector<Triple> findBySubject(const String& subject) const;
    std::vector<Triple> findByPredicate(const String& predicate) const;
    std::vector<Triple> findByObject(const String& object) const;
    std::vector<Triple> findBySP(const String& subject, const String& predicate) const;
    std::vector<Triple> findByPO(const String& predicate, const String& object) const;
    std::vector<Triple> getAllTriples() const;
    std::vector<Triple> queryTriples(const TripleStore::TriplePattern& pattern) const;
    std::vector<std::vector<String>> findPath(const String& from, const String& to, const String& predicate = "", int maxDepth = 5) const;

    /// 个体操作
    std::optional<Individual> getIndividual(const String& id) const;
    std::vector<Individual> getIndividualsByClass(const String& classId) const;
    bool addIndividual(const Individual& ind);
    bool updateIndividual(const Individual& ind);
    bool removeIndividual(const String& id);

    /// 类和关系操作
    bool addClass(const Class& cls);
    bool updateClass(const Class& cls);
    bool removeClass(const String& id);
    std::optional<Class> getClass(const String& id) const;

    bool addRelation(const Relation& rel);
    bool updateRelation(const Relation& rel);
    bool removeRelation(const String& id);
    std::optional<Relation> getRelation(const String& id) const;

    /// 统计
    size_t classCount() const;
    size_t relationCount() const;

    /// 混合查询 (符号 + 向量)
    struct HybridResult {
        std::vector<Individual> individuals;    // 符号匹配
        std::vector<VectorDatabase::SearchResult> vectorMatches; // 向量相似
        std::vector<std::pair<Individual, float>> combined; // 混合排序
    };

    HybridResult hybridQuery(
        const Query& query,
        const std::vector<float>& queryEmbedding,
        float symbolWeight = 0.5f,
        float vectorWeight = 0.5f
    ) const;

    /// 语义搜索
    std::vector<Individual> semanticSearch(
        const std::vector<float>& embedding,
        const String& classFilter = "",
        int topK = 10
    ) const;

    /// 向量搜索
    std::vector<VectorDatabase::SearchResult> vectorSearch(
        const std::vector<float>& embedding,
        int topK = 10
    ) const;

    /// 获取图数据库
    GraphDatabase* graphDB() { return graphDB_.get(); }
    const GraphDatabase* graphDB() const { return graphDB_.get(); }

    /// 获取向量数据库
    VectorDatabase* vectorDB() { return vectorDB_.get(); }
    const VectorDatabase* vectorDB() const { return vectorDB_.get(); }

    /// 获取三元组存储
    TripleStore* getTripleStore() { return &tripleStore_; }
    const TripleStore* getTripleStore() const { return &tripleStore_; }

    /// 清空存储
    void clear();

    /// 统计
    size_t tripleCount() const;
    size_t individualCount() const;

    /// 获取所有个体
    std::vector<Individual> getAllIndividuals() const;

    /// 获取所有类
    std::vector<Class> getAllClasses() const;

    /// 获取所有关系
    std::vector<Relation> getAllRelations() const;

    /// 批量操作
    struct BatchResult {
        int succeeded = 0;
        int failed = 0;
        std::vector<String> errors;
    };

    BatchResult batchAddTriples(const std::vector<Triple>& triples);
    BatchResult batchAddClasses(const std::vector<Class>& classes);
    BatchResult batchAddIndividuals(const std::vector<Individual>& individuals);
    BatchResult batchRemoveTriples(const std::vector<Triple>& triples);

    /// 传递闭包
    std::vector<std::pair<String, String>> computeTransitiveClosure(const String& predicate, int maxDepth = 10);

    /// 版本回滚: 恢复到指定时间戳的状态
    /// 1. 找到 timestamp 之前的最新快照并恢复
    /// 2. 重放 WAL 条目直到 timestamp
    bool restoreAsOf(int64_t timestamp, class WalManager* wal, class SnapshotManager* snapshotMgr);

private:
    GraphDatabasePtr graphDB_;
    VectorDatabasePtr vectorDB_;
    TripleStore tripleStore_;
    std::unordered_map<String, Individual> individuals_;
    std::unordered_map<String, Class> classes_;
    std::unordered_map<String, Relation> relations_;
};

} // namespace ontology
