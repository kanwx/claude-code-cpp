#pragma once

#include "GraphDatabase.hpp"
#include <curl/curl.h>
#include <chrono>

namespace ontology {

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

} // namespace ontology
