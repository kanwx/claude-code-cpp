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

} // namespace ontology
