#include <ontology/Storage.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <curl/curl.h>
#include <mutex>
#include <chrono>
#include <algorithm>

namespace ontology {

// ============================================================================
// StellarDBClient 实现 - 星环图数据库
// 基于 Transwarp Extended-OpenCypher (OpenCypher 兼容)
// ============================================================================

StellarDBClient::StellarDBClient(const Config& config)
    : config_(config) {
    curl_global_init(CURL_GLOBAL_ALL);
}

StellarDBClient::~StellarDBClient() {
    disconnect();
    curl_global_cleanup();
}

String StellarDBClient::baseUrl() const {
    return (config_.useHttps ? "https://" : "http://") +
           config_.host + ":" + std::to_string(config_.port);
}

bool StellarDBClient::connect() {
    // 1. 认证获取 Token
    if (!authenticate()) {
        connected_ = false;
        return false;
    }

    // 2. 验证连接: 查询图空间列表
    String response;
    String url = baseUrl() + "/api/v1/graphspaces";

    if (!httpGet(url, response)) {
        // 尝试备用端点 (不同版本的 StellarDB)
        url = baseUrl() + "/graphspaces";
        if (!httpGet(url, response)) {
            // 再尝试 Cypher 查询验证
            if (!runCypher("RETURN 1", {}, response)) {
                connected_ = false;
                return false;
            }
        }
    }

    connected_ = true;
    return true;
}

void StellarDBClient::disconnect() {
    connected_ = false;
    authToken_.clear();
}

bool StellarDBClient::isConnected() const {
    return connected_;
}

// ============================================================================
// 认证
// ============================================================================

bool StellarDBClient::authenticate() {
    // 如果已有有效 Token, 直接使用
    if (!authToken_.empty() &&
        std::chrono::system_clock::now() < tokenExpiry_) {
        return true;
    }

    // 如果配置了 Token, 直接使用
    if (!config_.token.empty()) {
        authToken_ = config_.token;
        // Token 默认 24 小时有效
        tokenExpiry_ = std::chrono::system_clock::now() + std::chrono::hours(24);
        return true;
    }

    // 通过用户名密码获取 Token
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    String response;
    String url = baseUrl() + "/api/v1/auth/login";

    Json body;
    body["username"] = config_.username;
    body["password"] = config_.password;

    String bodyStr = body.dump();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    try {
        Json result = Json::parse(response);
        // StellarDB TDH 认证返回格式
        if (result.contains("data") && result["data"].contains("token")) {
            authToken_ = result["data"]["token"].get<String>();
        } else if (result.contains("token")) {
            authToken_ = result["token"].get<String>();
        } else if (result.contains("access_token")) {
            authToken_ = result["access_token"].get<String>();
        } else {
            // 如果认证端点不可用, 尝试使用 Basic Auth
            authToken_ = "";
            return true;  // 降级为 Basic Auth
        }

        // 解析过期时间 (默认 2 小时)
        int expiresIn = result.value("expires_in", 7200);
        tokenExpiry_ = std::chrono::system_clock::now() +
                        std::chrono::seconds(expiresIn - 300);  // 提前 5 分钟刷新
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("StellarDB JSON error: {}", e.what());
        // 解析失败, 降级为 Basic Auth
        authToken_ = "";
        return true;
    } catch (const std::exception& e) {
        spdlog::error("StellarDB error: {}", e.what());
        authToken_ = "";
        return true;
    }
}

bool StellarDBClient::refreshToken() {
    authToken_.clear();
    return authenticate();
}

void StellarDBClient::applyAuthHeaders(struct curl_slist*& headers) {
    if (!authToken_.empty()) {
        String authHeader = "Authorization: Bearer " + authToken_;
        headers = curl_slist_append(headers, authHeader.c_str());
    } else if (!config_.username.empty()) {
        // Basic Auth 降级
        String credentials = config_.username + ":" + config_.password;
        String encoded = "Authorization: Basic ";
        // Base64 编码
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string ret;
        int i = 0, j = 0;
        unsigned char buf[3];
        unsigned char out[4];
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(credentials.data());
        size_t len = credentials.size();

        while (len--) {
            buf[i++] = *(bytes++);
            if (i == 3) {
                out[0] = (buf[0] & 0xfc) >> 2;
                out[1] = ((buf[0] & 0x03) << 4) + ((buf[1] & 0xf0) >> 4);
                out[2] = ((buf[1] & 0x0f) << 2) + ((buf[2] & 0xc0) >> 6);
                out[3] = buf[2] & 0x3f;
                for (i = 0; i < 4; i++) ret += base64_chars[out[i]];
                i = 0;
            }
        }
        if (i) {
            for (j = i; j < 3; j++) buf[j] = '\0';
            out[0] = (buf[0] & 0xfc) >> 2;
            out[1] = ((buf[0] & 0x03) << 4) + ((buf[1] & 0xf0) >> 4);
            out[2] = ((buf[1] & 0x0f) << 2) + ((buf[2] & 0xc0) >> 6);
            for (j = 0; j < i + 1; j++) ret += base64_chars[out[j]];
            while (i++ < 3) ret += '=';
        }

        encoded += ret;
        headers = curl_slist_append(headers, encoded.c_str());
    }
}

// ============================================================================
// HTTP 方法
// ============================================================================

bool StellarDBClient::httpGet(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool StellarDBClient::httpPost(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool StellarDBClient::httpPut(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool StellarDBClient::httpDelete(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

size_t StellarDBClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    ((String*)userp)->append((char*)contents, totalSize);
    return totalSize;
}

// ============================================================================
// Cypher 执行
// ============================================================================

bool StellarDBClient::runCypher(const String& cypher, const Json& params, String& response) {
    // StellarDB OpenCypher 查询端点
    // 尝试多种端点格式以兼容不同版本
    String url = baseUrl() + "/api/v1/graph/" + config_.graphName + "/cypher";

    Json body;
    body["query"] = cypher;
    if (!params.empty()) {
        body["parameters"] = params;
    }

    if (!httpPost(url, body.dump(), response)) {
        // 备用端点 1
        url = baseUrl() + "/graph/" + config_.graphName + "/cypher";
        if (!httpPost(url, body.dump(), response)) {
            // 备用端点 2 (兼容 TDH 统一接口)
            url = baseUrl() + "/api/v1/cypher";
            Json cypherBody;
            cypherBody["statement"] = cypher;
            cypherBody["graph"] = config_.graphName;
            if (!params.empty()) {
                cypherBody["parameters"] = params;
            }
            if (!httpPost(url, cypherBody.dump(), response)) {
                return false;
            }
        }
    }

    return true;
}

std::vector<Json> StellarDBClient::parseCypherResult(const String& response) {
    std::vector<Json> results;

    try {
        Json result = Json::parse(response);

        // StellarDB 响应格式 1: { "data": { "rows": [...] } }
        if (result.contains("data") && result["data"].contains("rows")) {
            for (const auto& row : result["data"]["rows"]) {
                results.push_back(row);
            }
            return results;
        }

        // 响应格式 2: { "results": [{ "data": [{ "row": [...] }] }] } (Neo4j 兼容)
        if (result.contains("results") && result["results"].is_array()) {
            for (const auto& res : result["results"]) {
                if (res.contains("data") && res["data"].is_array()) {
                    for (const auto& item : res["data"]) {
                        if (item.contains("row")) {
                            results.push_back(item["row"]);
                        } else {
                            results.push_back(item);
                        }
                    }
                }
            }
            return results;
        }

        // 响应格式 3: { "data": [...] }
        if (result.contains("data") && result["data"].is_array()) {
            for (const auto& item : result["data"]) {
                results.push_back(item);
            }
            return results;
        }

        // 响应格式 4: 直接数组
        if (result.is_array()) {
            for (const auto& item : result) {
                results.push_back(item);
            }
            return results;
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("StellarDB JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("StellarDB error: {}", e.what());
    }

    return results;
}

// ============================================================================
// GraphDatabase 接口实现
// ============================================================================

bool StellarDBClient::createNode(const String& id, const String& label, const Json& properties) {
    String cypher = "CREATE (n:`" + label + "` {id: $id";
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        cypher += ", " + it.key() + ": $" + it.key();
    }
    cypher += "}) RETURN n";

    Json params;
    params["id"] = id;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        params[it.key()] = it.value();
    }

    String response;
    return runCypher(cypher, params, response);
}

bool StellarDBClient::updateNode(const String& id, const Json& properties) {
    String cypher = "MATCH (n {id: $id}) SET ";
    bool first = true;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (!first) cypher += ", ";
        cypher += "n." + it.key() + " = $" + it.key();
        first = false;
    }

    Json params;
    params["id"] = id;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        params[it.key()] = it.value();
    }

    String response;
    return runCypher(cypher, params, response);
}

bool StellarDBClient::deleteNode(const String& id) {
    String cypher = "MATCH (n {id: $id}) DETACH DELETE n";
    Json params;
    params["id"] = id;

    String response;
    return runCypher(cypher, params, response);
}

std::optional<Json> StellarDBClient::getNode(const String& id) {
    String cypher = "MATCH (n {id: $id}) RETURN n";
    Json params;
    params["id"] = id;

    String response;
    if (!runCypher(cypher, params, response)) {
        return std::nullopt;
    }

    auto results = parseCypherResult(response);
    if (!results.empty()) {
        // 提取节点属性
        if (results[0].is_array() && results[0].size() > 0) {
            return results[0][0];
        }
        return results[0];
    }

    return std::nullopt;
}

bool StellarDBClient::createRelation(const String& from, const String& type, const String& to, const Json& properties) {
    String cypher = "MATCH (a {id: $from}), (b {id: $to}) CREATE (a)-[r:`" + type + "` {";
    bool first = true;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (!first) cypher += ", ";
        cypher += it.key() + ": $" + it.key();
        first = false;
    }
    cypher += "}]->(b) RETURN r";

    Json params;
    params["from"] = from;
    params["to"] = to;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        params[it.key()] = it.value();
    }

    String response;
    return runCypher(cypher, params, response);
}

bool StellarDBClient::deleteRelation(const String& from, const String& type, const String& to) {
    String cypher = "MATCH (a {id: $from})-[r:`" + type + "`]->(b {id: $to}) DELETE r";
    Json params;
    params["from"] = from;
    params["to"] = to;

    String response;
    return runCypher(cypher, params, response);
}

std::vector<Json> StellarDBClient::getRelations(const String& from, const String& type) {
    String cypher;
    if (type.empty()) {
        cypher = "MATCH (a {id: $id})-[r]->(b) RETURN type(r) as type, b.id as target, properties(r) as props";
    } else {
        cypher = "MATCH (a {id: $id})-[r:`" + type + "`]->(b) RETURN type(r) as type, b.id as target, properties(r) as props";
    }

    Json params;
    params["id"] = from;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<Json> results;
    auto rows = parseCypherResult(response);

    for (const auto& row : rows) {
        Json rel;
        if (row.is_array() && row.size() >= 3) {
            rel["type"] = row[0];
            rel["target"] = row[1];
            rel["properties"] = row[2];
        } else if (row.is_object()) {
            rel = row;
        }
        results.push_back(rel);
    }

    return results;
}

std::vector<std::vector<Json>> StellarDBClient::findPath(const String& from, const String& to, int maxDepth) {
    // 优先尝试 StellarDB 内置最短路径算法
    auto pathNodes = shortestPath(from, to, "", maxDepth);
    if (!pathNodes.empty()) {
        std::vector<std::vector<Json>> paths;
        for (const auto& nodePath : pathNodes) {
            std::vector<Json> pathJsons;
            for (const auto& nodeId : nodePath) {
                auto node = getNode(nodeId);
                pathJsons.push_back(node.value_or(Json::object()));
            }
            paths.push_back(pathJsons);
        }
        return paths;
    }

    // 降级到 Cypher 路径查询
    int depth = std::min(maxDepth, config_.maxDepth);
    String cypher = "MATCH p=shortestPath((a {id: $from})-[*.." +
                     std::to_string(depth) + "]-(b {id: $to})) RETURN nodes(p), relationships(p)";

    Json params;
    params["from"] = from;
    params["to"] = to;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<std::vector<Json>> paths;
    auto rows = parseCypherResult(response);

    for (const auto& row : rows) {
        if (row.is_array() && row.size() >= 2) {
            std::vector<Json> path;
            path.push_back(row[0]);  // nodes
            path.push_back(row[1]);  // relationships
            paths.push_back(path);
        }
    }

    return paths;
}

std::vector<Json> StellarDBClient::query(const String& cypher) {
    String response;
    if (!runCypher(cypher, {}, response)) {
        return {};
    }
    return parseCypherResult(response);
}

std::vector<Json> StellarDBClient::query(const Json& querySpec) {
    String cypher = querySpec.value("query", "");
    Json params = querySpec.value("params", Json::object());

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }
    return parseCypherResult(response);
}

bool StellarDBClient::batchCreate(const std::vector<Triple>& triples) {
    if (triples.empty()) return true;

    // 分批处理
    const int batchSize = 500;
    for (size_t i = 0; i < triples.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, triples.size());

        String cypher = "UNWIND $triples AS t "
            "MERGE (s {id: t.subject}) "
            "MERGE (o {id: t.object}) "
            "MERGE (s)-[r:`relation` {type: t.predicate}]->(o)";

        Json params;
        params["triples"] = Json::array();
        for (size_t j = i; j < end; ++j) {
            params["triples"].push_back({
                {"subject", triples[j].subject},
                {"predicate", triples[j].predicate},
                {"object", triples[j].object}
            });
        }

        String response;
        if (!runCypher(cypher, params, response)) {
            return false;
        }
    }

    return true;
}

String StellarDBClient::getStatus() const {
    if (!connected_) return "disconnected";
    return "connected (StellarDB " + config_.host + ":" + std::to_string(config_.port) + "/" + config_.graphName + ")";
}

// ============================================================================
// StellarDB 扩展方法
// ============================================================================

std::vector<Json> StellarDBClient::executeCypher(const String& cypher, const Json& params) {
    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }
    return parseCypherResult(response);
}

Json StellarDBClient::getGraphStats() {
    String response;

    // 尝试 StellarDB 统计端点
    String url = baseUrl() + "/api/v1/graph/" + config_.graphName + "/stats";
    if (httpGet(url, response)) {
        try {
            return Json::parse(response);
        } catch (const nlohmann::json::exception& e) {
        spdlog::error("StellarDB JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("StellarDB error: {}", e.what());
    }
    }

    // 降级: Cypher 查询统计
    Json stats;
    auto nodeRes = executeCypher("MATCH (n) RETURN count(n) as nodeCount");
    if (!nodeRes.empty()) {
        if (nodeRes[0].is_array() && nodeRes[0].size() > 0) {
            stats["nodeCount"] = nodeRes[0][0];
        }
    }

    auto edgeRes = executeCypher("MATCH ()-[r]->() RETURN count(r) as edgeCount");
    if (!edgeRes.empty()) {
        if (edgeRes[0].is_array() && edgeRes[0].size() > 0) {
            stats["edgeCount"] = edgeRes[0][0];
        }
    }

    return stats;
}

bool StellarDBClient::createGraphSpace(const String& name, const Json& options) {
    String url = baseUrl() + "/api/v1/graphspaces";

    Json body;
    body["name"] = name;
    if (!options.empty()) {
        body["options"] = options;
    }

    // 设置副本数和分片数
    if (!body.contains("options")) {
        body["options"] = Json::object();
    }
    body["options"]["replica_num"] = options.value("replica_num", 3);
    body["options"]["shard_num"] = options.value("shard_num", 6);

    String response;
    return httpPost(url, body.dump(), response);
}

bool StellarDBClient::dropGraphSpace(const String& name) {
    String url = baseUrl() + "/api/v1/graphspaces/" + name;
    String response;
    return httpDelete(url, response);
}

std::vector<String> StellarDBClient::listGraphSpaces() {
    String url = baseUrl() + "/api/v1/graphspaces";
    String response;

    if (!httpGet(url, response)) {
        // 降级端点
        url = baseUrl() + "/graphspaces";
        if (!httpGet(url, response)) {
            return {};
        }
    }

    std::vector<String> spaces;
    try {
        Json result = Json::parse(response);
        Json list;
        if (result.contains("data") && result["data"].is_array()) {
            list = result["data"];
        } else if (result.is_array()) {
            list = result;
        }

        for (const auto& item : list) {
            if (item.is_string()) {
                spaces.push_back(item.get<String>());
            } else if (item.contains("name")) {
                spaces.push_back(item["name"].get<String>());
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("StellarDB JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("StellarDB error: {}", e.what());
    }

    return spaces;
}

std::vector<Json> StellarDBClient::getSubgraph(const String& nodeId, int depth) {
    String cypher = "MATCH (n {id: $id})-[*.." + std::to_string(depth) + "]-(m) RETURN n, m";

    Json params;
    params["id"] = nodeId;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    return parseCypherResult(response);
}

std::vector<String> StellarDBClient::getNodeLabels(const String& nodeId) {
    String cypher = "MATCH (n {id: $id}) RETURN labels(n)";
    Json params;
    params["id"] = nodeId;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<String> labels;
    auto results = parseCypherResult(response);
    if (!results.empty()) {
        if (results[0].is_array()) {
            if (results[0].size() > 0 && results[0][0].is_array()) {
                for (const auto& l : results[0][0]) {
                    labels.push_back(l.get<String>());
                }
            }
        }
    }

    return labels;
}

// ============================================================================
// 图算法
// ============================================================================

std::vector<std::vector<String>> StellarDBClient::shortestPath(
    const String& from, const String& to, const String& edgeType, int maxDepth
) {
    // 尝试 StellarDB 内置最短路径 API
    String url = baseUrl() + "/api/v1/graph/" + config_.graphName + "/algorithm/shortest_path";

    Json body;
    body["source"] = from;
    body["target"] = to;
    body["max_depth"] = std::min(maxDepth, config_.maxDepth);
    if (!edgeType.empty()) {
        body["edge_type"] = edgeType;
    }

    String response;
    if (httpPost(url, body.dump(), response)) {
        try {
            Json result = Json::parse(response);
            std::vector<std::vector<String>> paths;

            // 格式 1: { "data": { "paths": [[node1, node2, ...], ...] } }
            if (result.contains("data") && result["data"].contains("paths")) {
                for (const auto& path : result["data"]["paths"]) {
                    std::vector<String> nodePath;
                    for (const auto& node : path) {
                        if (node.is_string()) {
                            nodePath.push_back(node.get<String>());
                        } else if (node.contains("id")) {
                            nodePath.push_back(node["id"].get<String>());
                        }
                    }
                    if (!nodePath.empty()) paths.push_back(nodePath);
                }
                return paths;
            }

            // 格式 2: { "paths": [...] }
            if (result.contains("paths")) {
                for (const auto& path : result["paths"]) {
                    std::vector<String> nodePath;
                    for (const auto& node : path) {
                        if (node.is_string()) {
                            nodePath.push_back(node.get<String>());
                        } else if (node.contains("id")) {
                            nodePath.push_back(node["id"].get<String>());
                        }
                    }
                    if (!nodePath.empty()) paths.push_back(nodePath);
                }
                return paths;
            }
        } catch (const nlohmann::json::exception& e) {
        spdlog::error("StellarDB JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("StellarDB error: {}", e.what());
    }
    }

    // 降级: Cypher 路径查询
    String cypher = "MATCH p=shortestPath((a {id: $from})-[*.." +
                     std::to_string(std::min(maxDepth, config_.maxDepth)) + "]-(b {id: $to})) " +
                     "RETURN [node in nodes(p) | node.id] as path";

    Json params;
    params["from"] = from;
    params["to"] = to;

    String cypherResponse;
    if (!runCypher(cypher, params, cypherResponse)) {
        return {};
    }

    std::vector<std::vector<String>> paths;
    auto rows = parseCypherResult(cypherResponse);
    for (const auto& row : rows) {
        std::vector<String> nodePath;
        if (row.is_array()) {
            if (row.size() > 0 && row[0].is_array()) {
                for (const auto& n : row[0]) {
                    nodePath.push_back(n.get<String>());
                }
            }
        }
        if (!nodePath.empty()) paths.push_back(nodePath);
    }

    return paths;
}

std::vector<std::vector<String>> StellarDBClient::allPaths(
    const String& from, const String& to, const String& edgeType, int maxDepth
) {
    String cypher = "MATCH p=(a {id: $from})-[*.." +
                     std::to_string(std::min(maxDepth, config_.maxDepth)) + "]-(b {id: $to}) " +
                     "RETURN [node in nodes(p) | node.id] as path";

    if (!edgeType.empty()) {
        cypher = "MATCH p=(a {id: $from})-[:`" + edgeType + "`*.." +
                 std::to_string(std::min(maxDepth, config_.maxDepth)) + "]-(b {id: $to}) " +
                 "RETURN [node in nodes(p) | node.id] as path";
    }

    Json params;
    params["from"] = from;
    params["to"] = to;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<std::vector<String>> paths;
    auto rows = parseCypherResult(response);
    for (const auto& row : rows) {
        std::vector<String> nodePath;
        if (row.is_array()) {
            if (row.size() > 0 && row[0].is_array()) {
                for (const auto& n : row[0]) {
                    nodePath.push_back(n.get<String>());
                }
            }
        }
        if (!nodePath.empty()) paths.push_back(nodePath);
    }

    return paths;
}

std::vector<std::pair<String, double>> StellarDBClient::pageRank(
    const String& label, int iterations, double damping
) {
    // 尝试 StellarDB 内置 PageRank API
    String url = baseUrl() + "/api/v1/graph/" + config_.graphName + "/algorithm/pagerank";

    Json body;
    body["iterations"] = iterations;
    body["damping_factor"] = damping;
    if (!label.empty()) {
        body["label"] = label;
    }

    String response;
    if (httpPost(url, body.dump(), response)) {
        try {
            Json result = Json::parse(response);
            std::vector<std::pair<String, double>> ranks;

            if (result.contains("data") && result["data"].is_array()) {
                for (const auto& item : result["data"]) {
                    String id = item.value("id", item.value("nodeId", ""));
                    double score = item.value("score", item.value("pagerank", 0.0));
                    if (!id.empty()) {
                        ranks.push_back({id, score});
                    }
                }
                return ranks;
            }
        } catch (const nlohmann::json::exception& e) {
        spdlog::error("StellarDB JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("StellarDB error: {}", e.what());
    }
    }

    // 降级: 无分布式 PageRank 可用
    return {};
}

std::vector<String> StellarDBClient::commonNeighbors(const String& node1, const String& node2) {
    String cypher = "MATCH (a {id: $id1})--(c)--(b {id: $id2}) RETURN DISTINCT c.id";
    Json params;
    params["id1"] = node1;
    params["id2"] = node2;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<String> neighbors;
    auto rows = parseCypherResult(response);
    for (const auto& row : rows) {
        if (row.is_array() && row.size() > 0) {
            neighbors.push_back(row[0].get<String>());
        }
    }

    return neighbors;
}

std::vector<std::pair<String, int>> StellarDBClient::degreeCentrality(
    const String& label, const String& direction
) {
    String cypher;
    if (direction == "in") {
        cypher = "MATCH (n" + (label.empty() ? "" : ":`" + label + "`") +
                 ")<-[r]-() RETURN n.id as id, count(r) as degree ORDER BY degree DESC";
    } else if (direction == "out") {
        cypher = "MATCH (n" + (label.empty() ? "" : ":`" + label + "`") +
                 ")-[r]->() RETURN n.id as id, count(r) as degree ORDER BY degree DESC";
    } else {
        cypher = "MATCH (n" + (label.empty() ? "" : ":`" + label + "`") +
                 ")-[r]-() RETURN n.id as id, count(r) as degree ORDER BY degree DESC";
    }

    String response;
    if (!runCypher(cypher, {}, response)) {
        return {};
    }

    std::vector<std::pair<String, int>> degrees;
    auto rows = parseCypherResult(response);
    for (const auto& row : rows) {
        if (row.is_array() && row.size() >= 2) {
            degrees.push_back({row[0].get<String>(), row[1].get<int>()});
        }
    }

    return degrees;
}

// ============================================================================
// 批量操作
// ============================================================================

bool StellarDBClient::batchCreateNodes(const std::vector<std::tuple<String, String, Json>>& nodes) {
    if (nodes.empty()) return true;

    const int batchSize = 500;
    for (size_t i = 0; i < nodes.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, nodes.size());

        String cypher = "UNWIND $nodes AS node "
            "CREATE (n:`" + std::get<1>(nodes[i]) + "`) SET n = node.props, n.id = node.id";

        // 需要按 label 分组 (Cypher 不支持动态 label)
        // 简化: 逐 label 批量创建
        std::unordered_map<String, std::vector<std::pair<String, Json>>> byLabel;
        for (size_t j = i; j < end; ++j) {
            byLabel[std::get<1>(nodes[j])].push_back({std::get<0>(nodes[j]), std::get<2>(nodes[j])});
        }

        for (const auto& [label, items] : byLabel) {
            String labelCypher = "UNWIND $nodes AS node "
                "CREATE (n:`" + label + "`) SET n = node.props SET n.id = node.id";

            Json params;
            params["nodes"] = Json::array();
            for (const auto& [id, props] : items) {
                Json item;
                item["id"] = id;
                item["props"] = props;
                params["nodes"].push_back(item);
            }

            String response;
            if (!runCypher(labelCypher, params, response)) {
                return false;
            }
        }
    }

    return true;
}

bool StellarDBClient::batchCreateEdges(const std::vector<std::tuple<String, String, String, Json>>& edges) {
    if (edges.empty()) return true;

    // 按 edge type 分组批量创建
    std::unordered_map<String, std::vector<std::tuple<String, String, Json>>> byType;
    for (const auto& [from, type, to, props] : edges) {
        byType[type].push_back({from, to, props});
    }

    for (const auto& [type, items] : byType) {
        const int batchSize = 500;
        for (size_t i = 0; i < items.size(); i += batchSize) {
            size_t end = std::min(i + batchSize, items.size());

            String cypher = "UNWIND $edges AS e "
                "MATCH (a {id: e.from}), (b {id: e.to}) "
                "CREATE (a)-[r:`" + type + "`]->(b) SET r = e.props";

            Json params;
            params["edges"] = Json::array();
            for (size_t j = i; j < end; ++j) {
                Json edge;
                edge["from"] = std::get<0>(items[j]);
                edge["to"] = std::get<1>(items[j]);
                edge["props"] = std::get<2>(items[j]);
                params["edges"].push_back(edge);
            }

            String response;
            if (!runCypher(cypher, params, response)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace ontology
