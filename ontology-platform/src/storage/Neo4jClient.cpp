#include <ontology/Storage.hpp>
#include <sstream>
#include <curl/curl.h>
#include <mutex>
#include <condition_variable>
#include <spdlog/spdlog.h>

namespace ontology {

// ============================================================================
// Neo4jClient 实现 - 标准图数据库接口
// ============================================================================

Neo4jClient::Neo4jClient(const Config& config)
    : config_(config) {
    curl_global_init(CURL_GLOBAL_ALL);
}

Neo4jClient::~Neo4jClient() {
    curl_global_cleanup();
}

bool Neo4jClient::connect() {
    // Neo4j bolt:// URI needs to be converted to HTTP for REST API
    // bolt://localhost:7687 → http://localhost:7474
    // neo4j://localhost:7687 → http://localhost:7474
    // http://... → use as-is
    String httpUri = config_.uri;
    if (httpUri.find("bolt://") == 0) {
        httpUri = "http://" + httpUri.substr(7);
        // Replace port 7687 with 7474 (Neo4j HTTP port)
        auto portPos = httpUri.rfind(':');
        if (portPos != String::npos && httpUri.substr(portPos + 1).find("7687") == 0) {
            httpUri = httpUri.substr(0, portPos + 1) + "7474";
        }
    } else if (httpUri.find("neo4j://") == 0) {
        httpUri = "http://" + httpUri.substr(8);
        auto portPos = httpUri.rfind(':');
        if (portPos != String::npos && httpUri.substr(portPos + 1).find("7687") == 0) {
            httpUri = httpUri.substr(0, portPos + 1) + "7474";
        }
    }
    httpUri_ = httpUri;

    String response;
    String url = httpUri_ + "/";

    if (!httpGet(url, response)) {
        connected_ = false;
        return false;
    }

    connected_ = true;
    return true;
}

void Neo4jClient::disconnect() {
    connected_ = false;
}

bool Neo4jClient::isConnected() const {
    return connected_;
}

bool Neo4jClient::createNode(const String& id, const String& label, const Json& properties) {
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

bool Neo4jClient::updateNode(const String& id, const Json& properties) {
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

bool Neo4jClient::deleteNode(const String& id) {
    String cypher = "MATCH (n {id: $id}) DETACH DELETE n";
    Json params;
    params["id"] = id;

    String response;
    return runCypher(cypher, params, response);
}

std::optional<Json> Neo4jClient::getNode(const String& id) {
    String cypher = "MATCH (n {id: $id}) RETURN n";
    Json params;
    params["id"] = id;

    String response;
    if (!runCypher(cypher, params, response)) {
        return std::nullopt;
    }

    try {
        Json result = Json::parse(response);
        if (result["results"].size() > 0 && result["results"][0]["data"].size() > 0) {
            return result["results"][0]["data"][0]["row"][0];
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return std::nullopt;
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return std::nullopt;
    }

    return std::nullopt;
}

std::vector<Json> Neo4jClient::findNodes(const String& label, const Json& conditions) {
    String cypher = "MATCH (n:`" + label + "`";
    if (!conditions.empty()) {
        cypher += " {";
        bool first = true;
        for (auto it = conditions.begin(); it != conditions.end(); ++it) {
            if (!first) cypher += ", ";
            cypher += it.key() + ": $" + it.key();
            first = false;
        }
        cypher += "}";
    }
    cypher += ") RETURN n";

    Json params = conditions;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<Json> results;
    try {
        Json result = Json::parse(response);
        for (const auto& row : result["results"][0]["data"]) {
            results.push_back(row["row"][0]);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return {};
    }

    return results;
}

bool Neo4jClient::createRelation(const String& from, const String& type, const String& to, const Json& properties) {
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

bool Neo4jClient::deleteRelation(const String& from, const String& type, const String& to) {
    String cypher = "MATCH (a {id: $from})-[r:`" + type + "`]->(b {id: $to}) DELETE r";
    Json params;
    params["from"] = from;
    params["to"] = to;

    String response;
    return runCypher(cypher, params, response);
}

std::vector<Json> Neo4jClient::getOutgoingRelations(const String& nodeId, const String& type) {
    String cypher;
    if (type.empty()) {
        cypher = "MATCH (a {id: $id})-[r]->(b) RETURN type(r) as type, b.id as target, properties(r) as props";
    } else {
        cypher = "MATCH (a {id: $id})-[r:`" + type + "`]->(b) RETURN type(r) as type, b.id as target, properties(r) as props";
    }

    Json params;
    params["id"] = nodeId;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<Json> results;
    try {
        Json result = Json::parse(response);
        for (const auto& row : result["results"][0]["data"]) {
            Json rel;
            rel["type"] = row["row"][0];
            rel["target"] = row["row"][1];
            rel["properties"] = row["row"][2];
            results.push_back(rel);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return {};
    }

    return results;
}

std::vector<Json> Neo4jClient::getIncomingRelations(const String& nodeId, const String& type) {
    String cypher;
    if (type.empty()) {
        cypher = "MATCH (a)-[r]->(b {id: $id}) RETURN type(r) as type, a.id as source, properties(r) as props";
    } else {
        cypher = "MATCH (a)-[r:`" + type + "`]->(b {id: $id}) RETURN type(r) as type, a.id as source, properties(r) as props";
    }

    Json params;
    params["id"] = nodeId;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<Json> results;
    try {
        Json result = Json::parse(response);
        for (const auto& row : result["results"][0]["data"]) {
            Json rel;
            rel["type"] = row["row"][0];
            rel["source"] = row["row"][1];
            rel["properties"] = row["row"][2];
            results.push_back(rel);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return {};
    }

    return results;
}

std::vector<Json> Neo4jClient::query(const String& cypher) {
    String response;
    if (!runCypher(cypher, {}, response)) {
        return {};
    }

    std::vector<Json> results;
    try {
        Json result = Json::parse(response);
        for (const auto& row : result["results"][0]["data"]) {
            results.push_back(row["row"]);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return {};
    }

    return results;
}

std::vector<Json> Neo4jClient::query(const Json& querySpec) {
    String cypher = querySpec.value("query", "");
    Json params = querySpec.value("params", Json::object());

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<Json> results;
    try {
        Json result = Json::parse(response);
        for (const auto& row : result["results"][0]["data"]) {
            results.push_back(row["row"]);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return {};
    }

    return results;
}

std::vector<std::vector<Json>> Neo4jClient::findPath(const String& from, const String& to, int maxDepth) {
    String cypher = "MATCH p=shortestPath((a {id: $from})-[*.." + std::to_string(maxDepth) + "]-(b {id: $to})) RETURN nodes(p), relationships(p)";

    Json params;
    params["from"] = from;
    params["to"] = to;

    String response;
    if (!runCypher(cypher, params, response)) {
        return {};
    }

    std::vector<std::vector<Json>> paths;
    try {
        Json result = Json::parse(response);
        for (const auto& row : result["results"][0]["data"]) {
            std::vector<Json> path;
            path.push_back(row["row"][0]); // nodes
            path.push_back(row["row"][1]); // relationships
            paths.push_back(path);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return {};
    }

    return paths;
}

size_t Neo4jClient::nodeCount() {
    String response;
    if (!runCypher("MATCH (n) RETURN count(n)", {}, response)) {
        return 0;
    }

    try {
        Json result = Json::parse(response);
        return result["results"][0]["data"][0]["row"][0].get<size_t>();
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return 0;
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return 0;
    }
}

size_t Neo4jClient::relationCount() {
    String response;
    if (!runCypher("MATCH ()-[r]->() RETURN count(r)", {}, response)) {
        return 0;
    }

    try {
        Json result = Json::parse(response);
        return result["results"][0]["data"][0]["row"][0].get<size_t>();
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        return 0;
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        return 0;
    }
}

bool Neo4jClient::runCypher(const String& cypher, const Json& params, String& response) {
    Json body;
    body["query"] = cypher;
    if (!params.empty()) {
        body["parameters"] = params;
    }

    String url = httpUri_ + "/db/neo4j/tx/commit";

    // 构建请求体
    Json statements = Json::array();
    Json stmt;
    stmt["statement"] = cypher;
    if (!params.empty()) {
        stmt["parameters"] = params;
    }
    statements.push_back(stmt);
    body["statements"] = statements;

    return httpPost(url, body.dump(), response);
}

bool Neo4jClient::httpPost(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

    String auth = config_.username + ":" + config_.password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool Neo4jClient::httpGet(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    String auth = config_.username + ":" + config_.password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool Neo4jClient::httpDelete(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

    String auth = config_.username + ":" + config_.password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

String Neo4jClient::executeCypher(const String& cypher, const Json& params) {
    String response;
    if (!runCypher(cypher, params, response)) {
        return "";
    }
    return response;
}

size_t Neo4jClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    ((String*)userp)->append((char*)contents, totalSize);
    return totalSize;
}

bool Neo4jClient::batchCreate(const std::vector<Triple>& triples) {
    // 批量创建优化：使用 UNWIND 进行批量插入
    if (triples.empty()) return true;

    // 分批处理，每批最多 1000 个
    const int batchSize = 1000;
    for (size_t i = 0; i < triples.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, triples.size());

        // 构建批量 Cypher
        String cypher = "UNWIND $triples AS t "
            "MERGE (s {id: t.subject}) "
            "MERGE (o {id: t.object}) "
            "MERGE (s)-[r:`relation` {type: t.predicate}]->(o)";

        Json params;
        params["triples"] = Json::array();

        for (size_t j = i; j < end; ++j) {
            Json tripleJson;
            tripleJson["subject"] = triples[j].subject;
            tripleJson["predicate"] = triples[j].predicate;
            tripleJson["object"] = triples[j].object;
            params["triples"].push_back(tripleJson);
        }

        String response;
        if (!runCypher(cypher, params, response)) {
            return false;
        }
    }

    return true;
}

bool Neo4jClient::beginTransaction() {
    // 开始事务
    String response;
    String url = httpUri_ + "/db/neo4j/tx";

    Json body;
    body["statements"] = Json::array();

    if (!httpPost(url, body.dump(), response)) {
        return false;
    }

    try {
        Json result = Json::parse(response);
        if (result.contains("transaction") && result["transaction"].contains("id")) {
            currentTransactionId_ = result["transaction"]["id"].get<int>();
            return true;
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
    }

    return false;
}

bool Neo4jClient::commitTransaction() {
    if (currentTransactionId_ == -1) return false;

    String response;
    String url = httpUri_ + "/db/neo4j/tx/" + std::to_string(currentTransactionId_) + "/commit";

    Json body;
    body["statements"] = Json::array();

    if (!httpPost(url, body.dump(), response)) {
        return false;
    }

    currentTransactionId_ = -1;
    return true;
}

bool Neo4jClient::rollbackTransaction() {
    if (currentTransactionId_ == -1) return false;

    String response;
    String url = httpUri_ + "/db/neo4j/tx/" + std::to_string(currentTransactionId_);

    if (!httpDelete(url, response)) {
        return false;
    }

    currentTransactionId_ = -1;
    return true;
}

std::vector<Json> Neo4jClient::executeInTransaction(const std::vector<String>& cyphers) {
    if (!beginTransaction()) return {};

    String url = httpUri_ + "/db/neo4j/tx/" + std::to_string(currentTransactionId_);

    Json body;
    body["statements"] = Json::array();

    for (const auto& cypher : cyphers) {
        Json stmt;
        stmt["statement"] = cypher;
        body["statements"].push_back(stmt);
    }

    String response;
    std::vector<Json> results;

    if (!httpPost(url, body.dump(), response)) {
        rollbackTransaction();
        return {};
    }

    try {
        Json result = Json::parse(response);
        for (const auto& res : result["results"]) {
            for (const auto& row : res["data"]) {
                results.push_back(row["row"]);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Neo4j JSON error: {}", e.what());
        rollbackTransaction();
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Neo4j runtime error: {}", e.what());
        rollbackTransaction();
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Neo4j error: {}", e.what());
        rollbackTransaction();
        return {};
    }

    commitTransaction();
    return results;
}

// 连接池实现
Neo4jConnectionPool::Neo4jConnectionPool(const Neo4jClient::Config& config, int poolSize)
    : config_(config), maxPoolSize_(poolSize) {
    for (int i = 0; i < poolSize; ++i) {
        auto client = std::make_shared<Neo4jClient>(config);
        if (client->connect()) {
            pool_.push(client);
        }
    }
}

std::shared_ptr<Neo4jClient> Neo4jConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待可用连接
    while (pool_.empty()) {
        if (cv_.wait_for(lock, std::chrono::seconds(30)) == std::cv_status::timeout) {
            return nullptr;
        }
    }

    auto client = pool_.top();
    pool_.pop();
    return client;
}

void Neo4jConnectionPool::release(std::shared_ptr<Neo4jClient> client) {
    std::unique_lock<std::mutex> lock(mutex_);
    pool_.push(client);
    cv_.notify_one();
}

size_t Neo4jConnectionPool::availableConnections() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return pool_.size();
}

bool Neo4jClient::batchCreateWithTransaction(const std::vector<Triple>& triples) {
    if (!beginTransaction()) return false;

    bool success = true;
    const int batchSize = 500;

    for (size_t i = 0; i < triples.size() && success; i += batchSize) {
        size_t end = std::min(i + batchSize, triples.size());

        String cypher = "UNWIND $batch AS t "
            "MATCH (s {id: t.s}), (o {id: t.o}) "
            "CREATE (s)-[r:`rel` {type: t.p}]->(o)";

        Json params;
        params["batch"] = Json::array();

        for (size_t j = i; j < end; ++j) {
            params["batch"].push_back({
                {"s", triples[j].subject},
                {"p", triples[j].predicate},
                {"o", triples[j].object}
            });
        }

        String response;
        String url = httpUri_ + "/db/neo4j/tx/" + std::to_string(currentTransactionId_);

        Json body;
        body["statements"] = Json::array();
        Json stmt;
        stmt["statement"] = cypher;
        stmt["parameters"] = params;
        body["statements"].push_back(stmt);

        if (!httpPost(url, body.dump(), response)) {
            success = false;
        }
    }

    if (success) {
        return commitTransaction();
    } else {
        rollbackTransaction();
        return false;
    }
}

std::vector<Json> Neo4jClient::getRelations(const String& from, const String& type) {
    return getOutgoingRelations(from, type);
}

String Neo4jClient::getStatus() const {
    return connected_ ? "connected" : "disconnected";
}

// ============================================================================
// Neo4jClient — Cypher result parser
// ============================================================================

std::vector<Json> Neo4jClient::parseCypherResult(const String& response) {
    std::vector<Json> results;
    try {
        Json result = Json::parse(response);
        // Neo4j REST format: { "results": [{ "data": [{ "row": [...] }] }] }
        if (result.contains("results") && result["results"].is_array()) {
            for (const auto& res : result["results"]) {
                if (res.contains("data") && res["data"].is_array()) {
                    for (const auto& item : res["data"]) {
                        if (item.contains("row") && item["row"].is_array()) {
                            // row is an array matching the RETURN clause
                            // Each element in row corresponds to a returned alias
                            // When there's a single alias, row[0] is the value
                            if (item["row"].size() == 1) {
                                results.push_back(item["row"][0]);
                            } else {
                                results.push_back(item["row"]);
                            }
                        } else {
                            results.push_back(item);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Neo4j parseCypherResult error: {}", e.what());
    }
    return results;
}

// ============================================================================
// Neo4jClient — Authority source: data loading
// ============================================================================

LoadResult Neo4jClient::loadAllClasses(std::vector<Class>& out) {
    LoadResult result;
    String response;
    String cypher =
        "MATCH (c:Class) RETURN c.id AS id, c.name AS name, "
        "c.description AS description, c.superClasses AS superClasses, "
        "c.equivalentClasses AS equivalentClasses, c.disjointClasses AS disjointClasses, "
        "c.properties AS properties, c.metadata AS metadata";
    if (!runCypher(cypher, {}, response)) {
        result.error = "Cypher query failed";
        return result;
    }
    try {
        auto parsed = parseCypherResult(response);
        for (auto& row : parsed) {
            Class cls;
            cls.id = row.value("id", "");
            cls.name = row.value("name", "");
            cls.description = row.value("description", "");
            if (row.contains("superClasses") && row["superClasses"].is_array()) {
                cls.superClasses = row["superClasses"].get<std::vector<String>>();
            }
            if (row.contains("equivalentClasses") && row["equivalentClasses"].is_array()) {
                cls.equivalentClasses = row["equivalentClasses"].get<std::vector<String>>();
            }
            if (row.contains("disjointClasses") && row["disjointClasses"].is_array()) {
                cls.disjointClasses = row["disjointClasses"].get<std::vector<String>>();
            }
            if (row.contains("properties") && row["properties"].is_array()) {
                cls.properties = row["properties"].get<std::vector<String>>();
            }
            if (row.contains("metadata")) {
                cls.metadata = row["metadata"];
            }
            out.push_back(std::move(cls));
        }
        result.success = true;
        result.count = static_cast<int>(out.size());
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }
    return result;
}

LoadResult Neo4jClient::loadAllIndividuals(std::vector<Individual>& out) {
    LoadResult result;
    String response;
    String cypher =
        "MATCH (i:Individual) RETURN i.id AS id, i.name AS name, "
        "i.classId AS classId, i.properties AS properties, "
        "i.importance AS importance, i.metadata AS metadata";
    if (!runCypher(cypher, {}, response)) {
        result.error = "Cypher query failed";
        return result;
    }
    try {
        auto parsed = parseCypherResult(response);
        for (auto& row : parsed) {
            Individual ind;
            ind.id = row.value("id", "");
            ind.name = row.value("name", "");
            ind.classId = row.value("classId", "");
            ind.importance = row.value("importance", 1.0f);
            if (row.contains("properties") && row["properties"].is_object()) {
                for (auto it = row["properties"].begin(); it != row["properties"].end(); ++it) {
                    ind.properties[it.key()] = it.value();
                }
            }
            if (row.contains("metadata")) {
                ind.metadata = row["metadata"];
            }
            out.push_back(std::move(ind));
        }
        result.success = true;
        result.count = static_cast<int>(out.size());
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }
    return result;
}

LoadResult Neo4jClient::loadAllRelations(std::vector<Relation>& out) {
    LoadResult result;
    String response;
    String cypher =
        "MATCH (r:Relation) RETURN r.id AS id, r.name AS name, "
        "r.description AS description, r.domain AS domain, r.range AS range, "
        "r.isFunctional AS isFunctional, r.isTransitive AS isTransitive, "
        "r.isSymmetric AS isSymmetric, r.inverseProperty AS inverseProperty, "
        "r.superProperties AS superProperties, r.metadata AS metadata";
    if (!runCypher(cypher, {}, response)) {
        result.error = "Cypher query failed";
        return result;
    }
    try {
        auto parsed = parseCypherResult(response);
        for (auto& row : parsed) {
            Relation rel;
            rel.id = row.value("id", "");
            rel.name = row.value("name", "");
            rel.description = row.value("description", "");
            rel.domain = row.value("domain", "");
            rel.range = row.value("range", "");
            rel.isFunctional = row.value("isFunctional", false);
            rel.isTransitive = row.value("isTransitive", false);
            rel.isSymmetric = row.value("isSymmetric", false);
            rel.inverseProperty = row.value("inverseProperty", "");
            if (row.contains("superProperties") && row["superProperties"].is_array()) {
                rel.superProperties = row["superProperties"].get<std::vector<String>>();
            }
            if (row.contains("metadata")) {
                rel.metadata = row["metadata"];
            }
            out.push_back(std::move(rel));
        }
        result.success = true;
        result.count = static_cast<int>(out.size());
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }
    return result;
}

LoadResult Neo4jClient::loadAllTriples(std::vector<Triple>& out) {
    LoadResult result;
    String response;
    String cypher =
        "MATCH (s)-[r]->(o) WHERE r.predicate IS NOT NULL "
        "RETURN s.id AS subject, r.predicate AS predicate, o.id AS object, "
        "r.confidence AS confidence";
    if (!runCypher(cypher, {}, response)) {
        result.error = "Cypher query failed";
        return result;
    }
    try {
        auto parsed = parseCypherResult(response);
        for (auto& row : parsed) {
            Triple t;
            t.subject = row.value("subject", "");
            t.predicate = row.value("predicate", "");
            t.object = row.value("object", "");
            t.confidence = row.value("confidence", 1.0f);
            out.push_back(std::move(t));
        }
        result.success = true;
        result.count = static_cast<int>(out.size());
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }
    return result;
}

// ============================================================================
// Neo4jClient — Authority source: single writes
// ============================================================================

bool Neo4jClient::createTriple(const Triple& triple) {
    Json params;
    params["s"] = triple.subject;
    params["p"] = triple.predicate;
    params["o"] = triple.object;
    params["conf"] = triple.confidence;
    String cypher =
        "MATCH (s {id: $s}), (o {id: $o}) "
        "CREATE (s)-[r:TRIPLE {predicate: $p, confidence: $conf}]->(o) "
        "RETURN type(r)";
    String response;
    return runCypher(cypher, params, response);
}

bool Neo4jClient::createRelation(const Relation& rel) {
    Json props;
    props["name"] = rel.name;
    props["description"] = rel.description;
    props["domain"] = rel.domain;
    props["range"] = rel.range;
    props["isFunctional"] = rel.isFunctional;
    props["isTransitive"] = rel.isTransitive;
    props["isSymmetric"] = rel.isSymmetric;
    props["inverseProperty"] = rel.inverseProperty;
    props["superProperties"] = rel.superProperties;
    props["metadata"] = rel.metadata;
    if (!createNode(rel.id, "Relation", props)) return false;
    if (!rel.domain.empty()) {
        Json p;
        p["id"] = rel.id;
        p["domain"] = rel.domain;
        String cypher = "MATCH (r:Relation {id: $id}), (d {id: $domain}) MERGE (r)-[:HAS_DOMAIN]->(d)";
        String response;
        runCypher(cypher, p, response);
    }
    if (!rel.range.empty()) {
        Json p;
        p["id"] = rel.id;
        p["range"] = rel.range;
        String cypher = "MATCH (r:Relation {id: $id}), (rng {id: $range}) MERGE (r)-[:HAS_RANGE]->(rng)";
        String response;
        runCypher(cypher, p, response);
    }
    return true;
}

bool Neo4jClient::deleteClass(const String& id) {
    return deleteNode(id);
}

bool Neo4jClient::deleteIndividual(const String& id) {
    return deleteNode(id);
}

bool Neo4jClient::deleteTriple(const Triple& triple) {
    Json params;
    params["s"] = triple.subject;
    params["p"] = triple.predicate;
    params["o"] = triple.object;
    String cypher =
        "MATCH (s {id: $s})-[r:TRIPLE {predicate: $p}]->(o {id: $o}) DELETE r";
    String response;
    return runCypher(cypher, params, response);
}

bool Neo4jClient::deleteRelation(const String& id) {
    return deleteNode(id);
}

// ============================================================================
// Neo4jClient — Authority source: graph queries
// ============================================================================

PathResult Neo4jClient::findShortestPath(const String& from, const String& to) {
    PathResult result;
    Json params;
    params["from"] = from;
    params["to"] = to;
    String cypher =
        "MATCH path = shortestPath((s {id: $from})-[*]-(t {id: $to})) "
        "RETURN [n IN nodes(path) | n.id] AS nodeIds, "
        "[r IN relationships(path) | type(r)] AS edgeTypes";
    String response;
    if (!runCypher(cypher, params, response)) {
        result.error = "Cypher query failed";
        return result;
    }
    try {
        auto parsed = parseCypherResult(response);
        if (!parsed.empty()) {
            result.success = true;
            if (parsed[0].contains("nodeIds") && parsed[0]["nodeIds"].is_array()) {
                result.nodes = parsed[0]["nodeIds"].get<std::vector<String>>();
            }
            if (parsed[0].contains("edgeTypes") && parsed[0]["edgeTypes"].is_array()) {
                result.edges = parsed[0]["edgeTypes"].get<std::vector<String>>();
            }
        }
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }
    return result;
}

std::vector<String> Neo4jClient::getSubClassClosure(const String& classId) {
    Json params;
    params["id"] = classId;
    String cypher = "MATCH (c:Class {id: $id})<-[:subClassOf*]-(sub:Class) RETURN DISTINCT sub.id AS id";
    String response;
    if (!runCypher(cypher, params, response)) return {};
    try {
        auto parsed = parseCypherResult(response);
        std::vector<String> result;
        for (auto& row : parsed) result.push_back(row.value("id", ""));
        return result;
    } catch (const std::exception&) { return {}; }
}

std::vector<String> Neo4jClient::getSuperClassClosure(const String& classId) {
    Json params;
    params["id"] = classId;
    String cypher = "MATCH (c:Class {id: $id})-[:subClassOf*]->(sup:Class) RETURN DISTINCT sup.id AS id";
    String response;
    if (!runCypher(cypher, params, response)) return {};
    try {
        auto parsed = parseCypherResult(response);
        std::vector<String> result;
        for (auto& row : parsed) result.push_back(row.value("id", ""));
        return result;
    } catch (const std::exception&) { return {}; }
}

CommunityResult Neo4jClient::detectCommunities(const String& algorithm, const Json& params) {
    CommunityResult result;
    String cypher;
    if (algorithm == "louvain") {
        cypher =
            "CALL gds.louvain.stream('ontologyGraph', {relationshipTypes: ['subClassOf']}) "
            "YIELD nodeId, communityId RETURN gds.util.asNode(nodeId).id AS id, communityId";
    } else if (algorithm == "label_propagation") {
        cypher =
            "CALL gds.labelPropagation.stream('ontologyGraph', {}) "
            "YIELD nodeId, communityId RETURN gds.util.asNode(nodeId).id AS id, communityId";
    } else {
        result.error = "Unknown algorithm: " + algorithm;
        return result;
    }
    String response;
    if (!runCypher(cypher, {}, response)) {
        result.error = "Community detection query failed";
        return result;
    }
    try {
        auto parsed = parseCypherResult(response);
        std::unordered_map<int64_t, std::vector<String>> groups;
        for (auto& row : parsed) {
            int64_t cid = row.value("communityId", int64_t(0));
            String nodeId = row.value("id", "");
            groups[cid].push_back(nodeId);
        }
        for (auto& [cid, members] : groups) {
            result.communities.push_back(std::move(members));
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }
    return result;
}

// ============================================================================
// Neo4jClient — Authority source: health
// ============================================================================

HealthStatus Neo4jClient::healthCheck() const {
    HealthStatus hs;
    String response;
    const_cast<Neo4jClient*>(this)->runCypher("RETURN 1 AS ok", {}, response);
    hs.connected = !response.empty();
    if (!hs.connected) hs.error = "Health check query failed";
    return hs;
}

} // namespace ontology
