#include <ontology/Storage.hpp>
#include <sstream>
#include <curl/curl.h>
#include <mutex>
#include <condition_variable>
#include <spdlog/spdlog.h>

namespace ontology {

// ============================================================================
// MilvusClient 实现 - 标准向量数据库接口
// ============================================================================

MilvusClient::MilvusClient(const Config& config)
    : config_(config) {
    curl_global_init(CURL_GLOBAL_ALL);
}

MilvusClient::~MilvusClient() {
    curl_global_cleanup();
}

bool MilvusClient::connect() {
    String response;
    String url = baseUrl() + "/v1/vector/collections";

    if (!httpGet(url, response)) {
        return false;
    }

    connected_ = true;
    return true;
}

bool MilvusClient::createCollection(const String& name, int dimension, const String& metric) {
    Json body;
    body["collection_name"] = name;

    Json schema;
    schema["auto_id"] = false;
    schema["fields"] = Json::array();

    Json idField;
    idField["name"] = "id";
    idField["is_primary"] = true;
    idField["type"] = "VarChar";
    idField["max_length"] = 256;
    schema["fields"].push_back(idField);

    Json vectorField;
    vectorField["name"] = "vector";
    vectorField["type"] = "FloatVector";
    vectorField["dimension"] = dimension;
    schema["fields"].push_back(vectorField);

    body["schema"] = schema;

    String response;
    String url = baseUrl() + "/v1/vector/collections";
    return httpPost(url, body.dump(), response);
}

bool MilvusClient::dropCollection(const String& name) {
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + name;
    return httpDelete(url, response);
}

bool MilvusClient::hasCollection(const String& name) {
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + name;

    if (!httpGet(url, response)) {
        return false;
    }

    try {
        Json result = Json::parse(response);
        return result.value("code", -1) == 0;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return false;
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return false;
    }
}

bool MilvusClient::insert(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata) {
    Json body;
    body["collection_name"] = collection;

    Json data = Json::array();
    Json row;
    row["id"] = id;
    row["vector"] = Json::array();
    for (float v : vector) {
        row["vector"].push_back(v);
    }
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        row[it.key()] = it.value();
    }
    data.push_back(row);
    body["data"] = data;

    String response;
    String url = baseUrl() + "/v1/vector/insert";
    return httpPost(url, body.dump(), response);
}

bool MilvusClient::insertBatch(const String& collection, const std::vector<MilvusClient::VectorRecord>& records) {
    Json body;
    body["collection_name"] = collection;

    Json data = Json::array();
    for (const auto& record : records) {
        Json row;
        row["id"] = record.id;
        row["vector"] = Json::array();
        for (float v : record.vector) {
            row["vector"].push_back(v);
        }
        for (auto it = record.metadata.begin(); it != record.metadata.end(); ++it) {
            row[it.key()] = it.value();
        }
        data.push_back(row);
    }
    body["data"] = data;

    String response;
    String url = baseUrl() + "/v1/vector/insert";
    return httpPost(url, body.dump(), response);
}

std::vector<VectorDatabase::SearchResult> MilvusClient::search(const String& collection, const std::vector<float>& query, int topK, const Json& filter) {
    Json body;
    body["collection_name"] = collection;
    body["limit"] = topK;

    body["vectors"] = Json::array();
    Json queryVec = Json::array();
    for (float v : query) {
        queryVec.push_back(v);
    }
    body["vectors"].push_back(queryVec);

    if (!filter.empty()) {
        body["filter"] = filter.dump();
    }

    String response;
    String url = baseUrl() + "/v1/vector/search";

    if (!httpPost(url, body.dump(), response)) {
        return {};
    }

    std::vector<VectorDatabase::SearchResult> results;
    try {
        Json result = Json::parse(response);
        if (result["data"].is_array()) {
            for (const auto& item : result["data"]) {
                VectorDatabase::SearchResult sr;
                sr.id = item.value("id", "");
                sr.score = item.value("distance", 0.0f);
                if (item.contains("fields")) {
                    sr.metadata = item["fields"];
                }
                results.push_back(sr);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return {};
    }

    return results;
}

std::vector<std::vector<VectorDatabase::SearchResult>> MilvusClient::searchBatch(
    const String& collection,
    const std::vector<std::vector<float>>& queries,
    int topK
) {
    Json body;
    body["collection_name"] = collection;
    body["limit"] = topK;

    body["vectors"] = Json::array();
    for (const auto& query : queries) {
        Json queryVec = Json::array();
        for (float v : query) {
            queryVec.push_back(v);
        }
        body["vectors"].push_back(queryVec);
    }

    String response;
    String url = baseUrl() + "/v1/vector/search";

    if (!httpPost(url, body.dump(), response)) {
        return {};
    }

    std::vector<std::vector<VectorDatabase::SearchResult>> allResults;
    try {
        Json result = Json::parse(response);
        if (result["data"].is_array()) {
            std::vector<VectorDatabase::SearchResult> currentBatch;
            String currentQueryId;

            for (const auto& item : result["data"]) {
                String queryId = item.value("query_id", "");

                if (!currentQueryId.empty() && queryId != currentQueryId) {
                    allResults.push_back(currentBatch);
                    currentBatch.clear();
                }
                currentQueryId = queryId;

                VectorDatabase::SearchResult sr;
                sr.id = item.value("id", "");
                sr.score = item.value("distance", 0.0f);
                if (item.contains("fields")) {
                    sr.metadata = item["fields"];
                }
                currentBatch.push_back(sr);
            }

            if (!currentBatch.empty()) {
                allResults.push_back(currentBatch);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return {};
    }

    return allResults;
}

bool MilvusClient::remove(const String& collection, const String& id) {
    Json body;
    body["collection_name"] = collection;
    body["id"] = id;

    String response;
    String url = baseUrl() + "/v1/vector/delete";
    return httpPost(url, body.dump(), response);
}

std::optional<MilvusClient::VectorRecord> MilvusClient::get(const String& collection, const String& id) {
    Json body;
    body["collection_name"] = collection;
    body["id"] = id;

    String response;
    String url = baseUrl() + "/v1/vector/get";

    if (!httpPost(url, body.dump(), response)) {
        return std::nullopt;
    }

    try {
        Json result = Json::parse(response);
        if (result["data"].is_array() && result["data"].size() > 0) {
            const auto& item = result["data"][0];
            MilvusClient::VectorRecord record;
            record.id = item.value("id", "");
            if (item.contains("vector")) {
                for (const auto& v : item["vector"]) {
                    record.vector.push_back(v.get<float>());
                }
            }
            if (item.contains("fields")) {
                record.metadata = item["fields"];
            }
            return record;
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return std::nullopt;
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return std::nullopt;
    }

    return std::nullopt;
}

size_t MilvusClient::count(const String& collection) {
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + collection;

    if (!httpGet(url, response)) {
        return 0;
    }

    try {
        Json result = Json::parse(response);
        return result.value("rowCount", 0);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return 0;
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return 0;
    }
}

bool MilvusClient::createIndex(const String& collection, const String& field, const String& indexType) {
    Json body;
    body["collection_name"] = collection;
    body["field_name"] = field;
    body["index_type"] = indexType;

    String response;
    String url = baseUrl() + "/v1/vector/index";
    return httpPost(url, body.dump(), response);
}

bool MilvusClient::update(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata) {
    // Milvus uses upsert for update
    return insert(collection, id, vector, metadata);
}

bool MilvusClient::batchInsert(const String& collection, const std::vector<std::pair<String, std::vector<float>>>& vectors) {
    std::vector<VectorRecord> records;
    for (const auto& [id, vec] : vectors) {
        records.push_back({id, vec, {}});
    }
    return insertBatch(collection, records);
}

// 连接池实现
MilvusConnectionPool::MilvusConnectionPool(const MilvusClient::Config& config, int poolSize)
    : config_(config), maxPoolSize_(poolSize) {
    for (int i = 0; i < poolSize; ++i) {
        auto client = std::make_shared<MilvusClient>(config);
        if (client->connect()) {
            pool_.push(client);
        }
    }
}

std::shared_ptr<MilvusClient> MilvusConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (pool_.empty()) {
        if (cv_.wait_for(lock, std::chrono::seconds(30)) == std::cv_status::timeout) {
            return nullptr;
        }
    }

    auto client = pool_.top();
    pool_.pop();
    return client;
}

void MilvusConnectionPool::release(std::shared_ptr<MilvusClient> client) {
    std::unique_lock<std::mutex> lock(mutex_);
    pool_.push(client);
    cv_.notify_one();
}

size_t MilvusConnectionPool::availableConnections() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return pool_.size();
}

bool MilvusClient::batchInsertOptimized(const String& collection, const std::vector<VectorRecord>& records, int batchSize) {
    // 分批插入，优化内存使用
    for (size_t i = 0; i < records.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, records.size());

        std::vector<VectorRecord> batch(records.begin() + i, records.begin() + end);
        if (!insertBatch(collection, batch)) {
            return false;
        }
    }
    return true;
}

bool MilvusClient::flush(const String& collection) {
    // 强制刷新数据到磁盘
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + collection + "/flush";

    Json body;
    body["collection_name"] = collection;

    return httpPost(url, body.dump(), response);
}

bool MilvusClient::loadCollection(const String& collection) {
    // 加载集合到内存
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + collection + "/load";

    Json body;
    body["collection_name"] = collection;

    return httpPost(url, body.dump(), response);
}

bool MilvusClient::releaseCollection(const String& collection) {
    // 从内存释放集合
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + collection + "/release";

    Json body;
    body["collection_name"] = collection;

    return httpPost(url, body.dump(), response);
}

Json MilvusClient::getCollectionStats(const String& collection) {
    String response;
    String url = baseUrl() + "/v1/vector/collections/" + collection;

    if (!httpGet(url, response)) {
        return {};
    }

    try {
        return Json::parse(response);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return {};
    }
}

bool MilvusClient::isConnected() const {
    return connected_;
}

void MilvusClient::disconnect() {
    connected_ = false;
}

bool MilvusClient::httpPost(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

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

bool MilvusClient::httpGet(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

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

bool MilvusClient::httpDelete(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

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

size_t MilvusClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    ((String*)userp)->append((char*)contents, totalSize);
    return totalSize;
}

// ============================================================================
// QdrantClient 实现 - 备选向量数据库
// ============================================================================

QdrantClient::QdrantClient(const Config& config)
    : config_(config) {
    curl_global_init(CURL_GLOBAL_ALL);
}

QdrantClient::~QdrantClient() {
    curl_global_cleanup();
}

bool QdrantClient::connect() {
    String response;
    String url = baseUrl() + "/collections";

    if (!httpGet(url, response)) {
        return false;
    }

    connected_ = true;
    return true;
}

bool QdrantClient::createCollection(const String& name, int dimension, const String& metric) {
    Json body;
    body["vectors"] = Json::object();
    body["vectors"]["size"] = dimension;
    body["vectors"]["distance"] = metric; // Cosine, Euclid, Dot

    String response;
    String url = baseUrl() + "/collections/" + name;
    return httpPut(url, body.dump(), response);
}

bool QdrantClient::dropCollection(const String& name) {
    String response;
    String url = baseUrl() + "/collections/" + name;
    return httpDelete(url, response);
}

bool QdrantClient::hasCollection(const String& name) {
    String response;
    String url = baseUrl() + "/collections/" + name;

    if (!httpGet(url, response)) {
        return false;
    }

    try {
        Json result = Json::parse(response);
        return result["result"].value("status", "") == "green";
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return false;
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return false;
    }
}

bool QdrantClient::insert(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata) {
    Json body;
    body["points"] = Json::array();

    Json point;
    point["id"] = id;
    point["vector"] = Json::array();
    for (float v : vector) {
        point["vector"].push_back(v);
    }
    point["payload"] = metadata;
    body["points"].push_back(point);

    String response;
    String url = baseUrl() + "/collections/" + collection + "/points?wait=true";
    return httpPut(url, body.dump(), response);
}

bool QdrantClient::insertBatch(const String& collection, const std::vector<QdrantClient::VectorRecord>& records) {
    Json body;
    body["points"] = Json::array();

    for (const auto& record : records) {
        Json point;
        point["id"] = record.id;
        point["vector"] = Json::array();
        for (float v : record.vector) {
            point["vector"].push_back(v);
        }
        point["payload"] = record.metadata;
        body["points"].push_back(point);
    }

    String response;
    String url = baseUrl() + "/collections/" + collection + "/points?wait=true";
    return httpPut(url, body.dump(), response);
}

std::vector<VectorDatabase::SearchResult> QdrantClient::search(const String& collection, const std::vector<float>& query, int topK, const Json& filter) {
    Json body;
    body["vector"] = Json::array();
    for (float v : query) {
        body["vector"].push_back(v);
    }
    body["limit"] = topK;

    if (!filter.empty()) {
        body["filter"] = filter;
    }

    String response;
    String url = baseUrl() + "/collections/" + collection + "/points/search";

    if (!httpPost(url, body.dump(), response)) {
        return {};
    }

    std::vector<VectorDatabase::SearchResult> results;
    try {
        Json result = Json::parse(response);
        for (const auto& item : result["result"]) {
            VectorDatabase::SearchResult sr;
            sr.id = item.value("id", "");
            sr.score = item.value("score", 0.0f);
            if (item.contains("payload")) {
                sr.metadata = item["payload"];
            }
            results.push_back(sr);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return {};
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return {};
    }

    return results;
}

std::vector<std::vector<VectorDatabase::SearchResult>> QdrantClient::searchBatch(
    const String& collection,
    const std::vector<std::vector<float>>& queries,
    int topK
) {
    std::vector<std::vector<VectorDatabase::SearchResult>> allResults;

    // Qdrant 批量搜索
    for (const auto& query : queries) {
        auto results = search(collection, query, topK);
        allResults.push_back(results);
    }

    return allResults;
}

bool QdrantClient::remove(const String& collection, const String& id) {
    Json body;
    body["points"] = Json::array();
    body["points"].push_back(id);

    String response;
    String url = baseUrl() + "/collections/" + collection + "/points/delete?wait=true";
    return httpPost(url, body.dump(), response);
}

std::optional<QdrantClient::VectorRecord> QdrantClient::get(const String& collection, const String& id) {
    Json body;
    body["ids"] = Json::array();
    body["ids"].push_back(id);

    String response;
    String url = baseUrl() + "/collections/" + collection + "/points";

    if (!httpPost(url, body.dump(), response)) {
        return std::nullopt;
    }

    try {
        Json result = Json::parse(response);
        if (result["result"].is_array() && result["result"].size() > 0) {
            const auto& item = result["result"][0];
            QdrantClient::VectorRecord record;
            record.id = item.value("id", "");
            if (item.contains("vector")) {
                for (const auto& v : item["vector"]) {
                    record.vector.push_back(v.get<float>());
                }
            }
            if (item.contains("payload")) {
                record.metadata = item["payload"];
            }
            return record;
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return std::nullopt;
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return std::nullopt;
    }

    return std::nullopt;
}

size_t QdrantClient::count(const String& collection) {
    String response;
    String url = baseUrl() + "/collections/" + collection;

    if (!httpGet(url, response)) {
        return 0;
    }

    try {
        Json result = Json::parse(response);
        return result["result"].value("points_count", 0);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Milvus JSON error: {}", e.what());
        return 0;
    } catch (const std::runtime_error& e) {
        spdlog::error("Milvus runtime error: {}", e.what());
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("Milvus error: {}", e.what());
        return 0;
    }
}

bool QdrantClient::update(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata) {
    // Qdrant uses upsert for update
    return insert(collection, id, vector, metadata);
}

bool QdrantClient::batchInsert(const String& collection, const std::vector<std::pair<String, std::vector<float>>>& vectors) {
    std::vector<VectorRecord> records;
    for (const auto& [id, vec] : vectors) {
        records.push_back({id, vec, {}});
    }
    return insertBatch(collection, records);
}

bool QdrantClient::isConnected() const {
    return connected_;
}

void QdrantClient::disconnect() {
    connected_ = false;
}

bool QdrantClient::createIndex(const String& collection, const String& field, const String& indexType) {
    // Qdrant 自动索引 payload 字段
    Json body;
    body["field_name"] = field;
    body["field_schema"] = indexType;

    String response;
    String url = baseUrl() + "/collections/" + collection + "/index";
    return httpPut(url, body.dump(), response);
}

String QdrantClient::baseUrl() const {
    return "http://" + config_.host + ":" + std::to_string(config_.port);
}

bool QdrantClient::httpPost(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

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

bool QdrantClient::httpGet(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

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

bool QdrantClient::httpPut(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

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

bool QdrantClient::httpDelete(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

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

size_t QdrantClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    ((String*)userp)->append((char*)contents, totalSize);
    return totalSize;
}

} // namespace ontology
