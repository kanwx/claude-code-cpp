#include <ontology/Storage.hpp>
#include <sstream>
#include <curl/curl.h>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace ontology {

// ============================================================================
// HippoClient 实现 - 星环向量数据库
// RESTful API, SASL 认证, SSL/TLS
// ============================================================================

HippoClient::HippoClient(const Config& config)
    : config_(config) {
    curl_global_init(CURL_GLOBAL_ALL);
}

HippoClient::~HippoClient() {
    disconnect();
    curl_global_cleanup();
}

String HippoClient::baseUrl() const {
    return (config_.useHttps ? "https://" : "http://") +
           config_.host + ":" + std::to_string(config_.port);
}

String HippoClient::metricToHippoType(const String& metric) const {
    // Hippo 距离度量映射
    if (metric == "cosine") return "COSINE";
    if (metric == "l2" || metric == "euclid" || metric == "euclidean") return "L2";
    if (metric == "ip" || metric == "dot" || metric == "inner_product") return "IP";
    // 默认余弦相似度
    return "COSINE";
}

// ============================================================================
// 认证
// ============================================================================

bool HippoClient::authenticate() {
    if (!authToken_.empty() &&
        std::chrono::system_clock::now() < tokenExpiry_) {
        return true;
    }

    // 如果配置了 Token, 直接使用
    if (!config_.token.empty()) {
        authToken_ = config_.token;
        tokenExpiry_ = std::chrono::system_clock::now() + std::chrono::hours(24);
        return true;
    }

    // SASL 认证: 通过用户名密码获取 Token
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    // SSL 配置
    if (config_.enableSsl && !config_.sslCertPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config_.sslCertPath.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        // 认证端点不可用, 降级为 Basic Auth
        authToken_ = "";
        return true;
    }

    try {
        Json result = Json::parse(response);
        if (result.contains("data") && result["data"].contains("token")) {
            authToken_ = result["data"]["token"].get<String>();
        } else if (result.contains("token")) {
            authToken_ = result["token"].get<String>();
        } else if (result.contains("access_token")) {
            authToken_ = result["access_token"].get<String>();
        } else {
            authToken_ = "";
            return true;
        }

        int expiresIn = result.value("expires_in", 7200);
        tokenExpiry_ = std::chrono::system_clock::now() +
                        std::chrono::seconds(expiresIn - 300);
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Hippo JSON error: {}", e.what());
        authToken_ = "";
        return true;
    } catch (const std::runtime_error& e) {
        spdlog::error("Hippo runtime error: {}", e.what());
        authToken_ = "";
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Hippo error: {}", e.what());
        authToken_ = "";
        return true;
    }
}

bool HippoClient::refreshToken() {
    authToken_.clear();
    return authenticate();
}

void HippoClient::applyAuthHeaders(struct curl_slist*& headers) {
    if (!authToken_.empty()) {
        String authHeader = "Authorization: Bearer " + authToken_;
        headers = curl_slist_append(headers, authHeader.c_str());
    } else if (!config_.username.empty()) {
        // Basic Auth 降级
        String credentials = config_.username + ":" + config_.password;
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

        String encoded = "Authorization: Basic " + ret;
        headers = curl_slist_append(headers, encoded.c_str());
    }
}

// ============================================================================
// HTTP 方法
// ============================================================================

bool HippoClient::httpGet(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (config_.enableSsl && !config_.sslCertPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config_.sslCertPath.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool HippoClient::httpPost(const String& url, const String& body, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.searchTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (config_.enableSsl && !config_.sslCertPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config_.sslCertPath.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool HippoClient::httpPut(const String& url, const String& body, String& response) {
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

    if (config_.enableSsl && !config_.sslCertPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config_.sslCertPath.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool HippoClient::httpDelete(const String& url, String& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connectionTimeout);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    applyAuthHeaders(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (config_.enableSsl && !config_.sslCertPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config_.sslCertPath.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

size_t HippoClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    ((String*)userp)->append((char*)contents, totalSize);
    return totalSize;
}

// ============================================================================
// VectorDatabase 接口实现
// ============================================================================

bool HippoClient::connect() {
    // 1. 认证
    if (!authenticate()) {
        connected_ = false;
        return false;
    }

    // 2. 验证连接: 列出集合
    String response;
    String url = baseUrl() + "/api/v1/collections";

    if (!httpGet(url, response)) {
        // 备用端点
        url = baseUrl() + "/v1/collections";
        if (!httpGet(url, response)) {
            url = baseUrl() + "/collections";
            if (!httpGet(url, response)) {
                connected_ = false;
                return false;
            }
        }
    }

    connected_ = true;
    return true;
}

void HippoClient::disconnect() {
    connected_ = false;
    authToken_.clear();
}

bool HippoClient::isConnected() const {
    return connected_;
}

bool HippoClient::createCollection(const String& name, int dimension, const String& metric) {
    Json body;
    body["collection_name"] = name;

    // Hippo Schema 定义
    Json schema;
    schema["dimension"] = dimension;
    schema["metric_type"] = metricToHippoType(metric);
    schema["auto_id"] = false;

    // 字段定义
    Json fields = Json::array();

    // 主键字段
    Json idField;
    idField["name"] = "id";
    idField["is_primary"] = true;
    idField["type"] = "VARCHAR";
    idField["max_length"] = 256;
    fields.push_back(idField);

    // 向量字段
    Json vectorField;
    vectorField["name"] = "vector";
    vectorField["type"] = "FLOAT_VECTOR";
    vectorField["dimension"] = dimension;
    fields.push_back(vectorField);

    schema["fields"] = fields;

    // 索引配置
    Json indexConfig;
    indexConfig["index_type"] = "HNSW";
    indexConfig["params"] = {{"M", 16}, {"efConstruction", 256}};
    schema["index"] = indexConfig;

    body["schema"] = schema;

    String response;

    // 尝试主端点
    String url = baseUrl() + "/api/v1/collections";
    if (httpPost(url, body.dump(), response)) return true;

    // 备用端点
    url = baseUrl() + "/v1/collections";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/collections";
    return httpPost(url, body.dump(), response);
}

bool HippoClient::dropCollection(const String& name) {
    String response;

    String url = baseUrl() + "/api/v1/collections/" + name;
    if (httpDelete(url, response)) return true;

    url = baseUrl() + "/v1/collections/" + name;
    if (httpDelete(url, response)) return true;

    url = baseUrl() + "/collections/" + name;
    return httpDelete(url, response);
}

bool HippoClient::hasCollection(const String& name) {
    String response;

    String url = baseUrl() + "/api/v1/collections/" + name;
    if (httpGet(url, response)) {
        try {
            Json result = Json::parse(response);
            // Hippo 响应格式
            if (result.contains("code") && result["code"].get<int>() == 0) return true;
            if (result.contains("data") && result["data"].is_object()) return true;
            if (result.contains("status") && result["status"] == "ok") return true;
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}
        return true;  // GET 成功即存在
    }

    // 备用端点
    url = baseUrl() + "/v1/collections/" + name;
    if (httpGet(url, response)) return true;

    return false;
}

bool HippoClient::insert(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata) {
    Json body;
    body["collection_name"] = collection;

    Json data = Json::array();
    Json row;
    row["id"] = id;
    row["vector"] = Json::array();
    for (float v : vector) {
        row["vector"].push_back(v);
    }
    // 元数据字段
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        row[it.key()] = it.value();
    }
    data.push_back(row);
    body["data"] = data;

    String response;

    // 尝试多个端点
    String url = baseUrl() + "/api/v1/collections/" + collection + "/insert";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/v1/collections/" + collection + "/insert";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/api/v1/vector/insert";
    return httpPost(url, body.dump(), response);
}

bool HippoClient::update(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata) {
    // Hippo 使用 upsert 语义
    Json body;
    body["collection_name"] = collection;
    body["id"] = id;
    body["vector"] = Json::array();
    for (float v : vector) {
        body["vector"].push_back(v);
    }
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        body[it.key()] = it.value();
    }

    String response;

    // 尝试 upsert 端点
    String url = baseUrl() + "/api/v1/collections/" + collection + "/upsert";
    if (httpPost(url, body.dump(), response)) return true;

    // 降级: delete + insert
    remove(collection, id);
    return insert(collection, id, vector, metadata);
}

bool HippoClient::remove(const String& collection, const String& id) {
    Json body;
    body["collection_name"] = collection;
    body["id"] = id;

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/delete";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/v1/collections/" + collection + "/delete";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/api/v1/vector/delete";
    return httpPost(url, body.dump(), response);
}

std::vector<VectorDatabase::SearchResult> HippoClient::search(
    const String& collection,
    const std::vector<float>& query,
    int topK,
    const Json& filter
) {
    Json body;
    body["collection_name"] = collection;
    body["limit"] = topK;

    // 查询向量
    body["vectors"] = Json::array();
    Json queryVec = Json::array();
    for (float v : query) {
        queryVec.push_back(v);
    }
    body["vectors"].push_back(queryVec);

    // 标量过滤条件
    if (!filter.empty()) {
        body["filter"] = filter;
        // Hippo 混合查询格式
        if (filter.contains("conditions")) {
            body["scalar_filter"] = filter["conditions"];
        }
    }

    // 搜索参数
    body["params"] = {{"ef", 128}};  // HNSW ef_search

    String response;

    // 尝试搜索端点
    String url = baseUrl() + "/api/v1/collections/" + collection + "/search";
    bool ok = httpPost(url, body.dump(), response);

    if (!ok) {
        url = baseUrl() + "/v1/collections/" + collection + "/search";
        ok = httpPost(url, body.dump(), response);
    }

    if (!ok) {
        url = baseUrl() + "/api/v1/vector/search";
        ok = httpPost(url, body.dump(), response);
    }

    if (!ok) return {};

    return [this, &response]() -> std::vector<VectorDatabase::SearchResult> {
        std::vector<VectorDatabase::SearchResult> results;
        try {
            Json result = Json::parse(response);

            // Hippo 响应格式 1: { "data": [...] }
            Json dataArr;
            if (result.contains("data") && result["data"].is_array()) {
                dataArr = result["data"];
            }
            // 格式 2: { "results": [...] }
            else if (result.contains("results") && result["results"].is_array()) {
                dataArr = result["results"];
            }
            // 格式 3: 直接数组
            else if (result.is_array()) {
                dataArr = result;
            }

            for (const auto& item : dataArr) {
                VectorDatabase::SearchResult sr;
                sr.id = item.value("id", item.value("doc_id", ""));
                sr.score = item.value("score", item.value("distance", item.value("similarity", 0.0f)));

                // 元数据
                if (item.contains("metadata")) {
                    sr.metadata = item["metadata"];
                } else if (item.contains("fields")) {
                    sr.metadata = item["fields"];
                } else if (item.contains("payload")) {
                    sr.metadata = item["payload"];
                }

                // 向量数据 (可选)
                if (item.contains("vector") && item["vector"].is_array()) {
                    for (const auto& v : item["vector"]) {
                        sr.vector.push_back(v.get<float>());
                    }
                }

                results.push_back(sr);
            }
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}

        return results;
    }();
}

bool HippoClient::batchInsert(const String& collection, const std::vector<std::pair<String, std::vector<float>>>& vectors) {
    std::vector<HippoVectorRecord> records;
    records.reserve(vectors.size());
    for (const auto& [id, vec] : vectors) {
        records.push_back({id, vec, {}});
    }
    return insertBatch(collection, records);
}

// ============================================================================
// Hippo 扩展方法
// ============================================================================

bool HippoClient::insertBatch(const String& collection, const std::vector<HippoVectorRecord>& records) {
    if (records.empty()) return true;

    // 分批插入, 每批最多 1000 条
    const int batchSize = 1000;
    for (size_t i = 0; i < records.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, records.size());

        Json body;
        body["collection_name"] = collection;

        Json data = Json::array();
        for (size_t j = i; j < end; ++j) {
            Json row;
            row["id"] = records[j].id;
            row["vector"] = Json::array();
            for (float v : records[j].vector) {
                row["vector"].push_back(v);
            }
            for (auto it = records[j].metadata.begin(); it != records[j].metadata.end(); ++it) {
                row[it.key()] = it.value();
            }
            data.push_back(row);
        }
        body["data"] = data;

        String response;

        String url = baseUrl() + "/api/v1/collections/" + collection + "/insert";
        bool ok = httpPost(url, body.dump(), response);

        if (!ok) {
            url = baseUrl() + "/api/v1/vector/insert";
            ok = httpPost(url, body.dump(), response);
        }

        if (!ok) return false;
    }

    return true;
}

std::vector<std::vector<VectorDatabase::SearchResult>> HippoClient::searchBatch(
    const String& collection,
    const std::vector<std::vector<float>>& queries,
    int topK
) {
    std::vector<std::vector<VectorDatabase::SearchResult>> allResults;

    // Hippo 批量搜索: 逐条查询 (也可优化为单次请求)
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

    body["params"] = {{"ef", 128}};

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/search";
    if (httpPost(url, body.dump(), response)) {
        try {
            Json result = Json::parse(response);
            if (result.contains("data") && result["data"].is_array()) {
                std::vector<VectorDatabase::SearchResult> currentBatch;
                int currentQueryIdx = -1;

                for (const auto& item : result["data"]) {
                    int qIdx = item.value("query_index", item.value("query_id", 0));

                    if (qIdx != currentQueryIdx && currentQueryIdx >= 0) {
                        allResults.push_back(currentBatch);
                        currentBatch.clear();
                    }
                    currentQueryIdx = qIdx;

                    VectorDatabase::SearchResult sr;
                    sr.id = item.value("id", "");
                    sr.score = item.value("score", item.value("distance", 0.0f));
                    if (item.contains("metadata")) {
                        sr.metadata = item["metadata"];
                    } else if (item.contains("fields")) {
                        sr.metadata = item["fields"];
                    }
                    currentBatch.push_back(sr);
                }

                if (!currentBatch.empty()) {
                    allResults.push_back(currentBatch);
                }

                return allResults;
            }
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}
    }

    // 降级: 逐条搜索
    for (const auto& query : queries) {
        auto results = search(collection, query, topK);
        allResults.push_back(results);
    }

    return allResults;
}

std::optional<HippoClient::HippoVectorRecord> HippoClient::get(const String& collection, const String& id) {
    Json body;
    body["collection_name"] = collection;
    body["id"] = id;

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/get";
    if (!httpPost(url, body.dump(), response)) {
        url = baseUrl() + "/api/v1/vector/get";
        if (!httpPost(url, body.dump(), response)) {
            return std::nullopt;
        }
    }

    try {
        Json result = Json::parse(response);

        Json dataArr;
        if (result.contains("data") && result["data"].is_array()) {
            dataArr = result["data"];
        } else if (result.contains("results") && result["results"].is_array()) {
            dataArr = result["results"];
        }

        if (!dataArr.empty()) {
            const auto& item = dataArr[0];
            HippoVectorRecord record;
            record.id = item.value("id", "");
            if (item.contains("vector") && item["vector"].is_array()) {
                for (const auto& v : item["vector"]) {
                    record.vector.push_back(v.get<float>());
                }
            }
            if (item.contains("metadata")) {
                record.metadata = item["metadata"];
            } else if (item.contains("fields")) {
                record.metadata = item["fields"];
            } else if (item.contains("payload")) {
                record.metadata = item["payload"];
            }
            return record;
        }
    } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}

    return std::nullopt;
}

size_t HippoClient::count(const String& collection) {
    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/stats";
    if (httpGet(url, response)) {
        try {
            Json result = Json::parse(response);
            if (result.contains("data")) {
                return result["data"].value("row_count", result["data"].value("count", 0));
            }
            return result.value("row_count", result.value("count", 0));
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}
    }

    // 备用端点
    url = baseUrl() + "/api/v1/collections/" + collection;
    if (httpGet(url, response)) {
        try {
            Json result = Json::parse(response);
            if (result.contains("data")) {
                return result["data"].value("row_count", result["data"].value("count", 0));
            }
            return result.value("row_count", result.value("count", 0));
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}
    }

    return 0;
}

bool HippoClient::createIndex(const String& collection, const String& field, const String& indexType) {
    Json body;
    body["collection_name"] = collection;
    body["field_name"] = field;
    body["index_type"] = indexType;

    // HNSW 参数
    if (indexType == "HNSW") {
        body["params"] = {{"M", 16}, {"efConstruction", 256}};
    }
    // IVF 参数
    else if (indexType.find("IVF") == 0) {
        body["params"] = {{"nlist", 1024}};
    }

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/index";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/api/v1/vector/index";
    return httpPost(url, body.dump(), response);
}

bool HippoClient::dropIndex(const String& collection, const String& field) {
    String response;
    String url = baseUrl() + "/api/v1/collections/" + collection + "/index/" + field;
    return httpDelete(url, response);
}

bool HippoClient::flush(const String& collection) {
    Json body;
    body["collection_name"] = collection;

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/flush";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/v1/collections/" + collection + "/flush";
    return httpPost(url, body.dump(), response);
}

bool HippoClient::loadCollection(const String& collection) {
    Json body;
    body["collection_name"] = collection;

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/load";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/v1/collections/" + collection + "/load";
    return httpPost(url, body.dump(), response);
}

bool HippoClient::releaseCollection(const String& collection) {
    Json body;
    body["collection_name"] = collection;

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/release";
    if (httpPost(url, body.dump(), response)) return true;

    url = baseUrl() + "/v1/collections/" + collection + "/release";
    return httpPost(url, body.dump(), response);
}

Json HippoClient::getCollectionStats(const String& collection) {
    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/stats";
    if (httpGet(url, response)) {
        try {
            return Json::parse(response);
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}
    }

    // 备用
    url = baseUrl() + "/api/v1/collections/" + collection;
    if (httpGet(url, response)) {
        try {
            return Json::parse(response);
        } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}
    }

    return {};
}

std::vector<VectorDatabase::SearchResult> HippoClient::hybridSearch(
    const String& collection,
    const std::vector<float>& query,
    int topK,
    const Json& scalarFilter,
    float vectorWeight,
    float scalarWeight
) {
    Json body;
    body["collection_name"] = collection;
    body["limit"] = topK;

    // 向量查询
    body["vector"] = Json::array();
    for (float v : query) {
        body["vector"].push_back(v);
    }

    // 混合查询权重
    body["hybrid_weights"] = {{"vector", vectorWeight}, {"scalar", scalarWeight}};

    // 标量过滤
    body["scalar_filter"] = scalarFilter;

    // HNSW 搜索参数
    body["params"] = {{"ef", 128}};

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/hybrid_search";
    if (!httpPost(url, body.dump(), response)) {
        // 降级: 普通向量搜索 + 客户端过滤
        return search(collection, query, topK * 3, scalarFilter);
    }

    std::vector<VectorDatabase::SearchResult> results;
    try {
        Json result = Json::parse(response);

        Json dataArr;
        if (result.contains("data") && result["data"].is_array()) {
            dataArr = result["data"];
        } else if (result.contains("results") && result["results"].is_array()) {
            dataArr = result["results"];
        }

        for (const auto& item : dataArr) {
            VectorDatabase::SearchResult sr;
            sr.id = item.value("id", "");
            sr.score = item.value("score", item.value("distance", 0.0f));

            if (item.contains("metadata")) {
                sr.metadata = item["metadata"];
            } else if (item.contains("fields")) {
                sr.metadata = item["fields"];
            }

            results.push_back(sr);

            if (static_cast<int>(results.size()) >= topK) break;
        }
    } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}

    return results;
}

std::vector<VectorDatabase::SearchResult> HippoClient::sqlQuery(const String& collection, const String& sql) {
    Json body;
    body["collection_name"] = collection;
    body["sql"] = sql;

    String response;

    String url = baseUrl() + "/api/v1/collections/" + collection + "/query";
    if (!httpPost(url, body.dump(), response)) {
        url = baseUrl() + "/api/v1/sql";
        if (!httpPost(url, body.dump(), response)) {
            return {};
        }
    }

    std::vector<VectorDatabase::SearchResult> results;
    try {
        Json result = Json::parse(response);

        Json dataArr;
        if (result.contains("data") && result["data"].is_array()) {
            dataArr = result["data"];
        } else if (result.contains("results") && result["results"].is_array()) {
            dataArr = result["results"];
        } else if (result.is_array()) {
            dataArr = result;
        }

        for (const auto& item : dataArr) {
            VectorDatabase::SearchResult sr;
            sr.id = item.value("id", "");
            sr.score = item.value("score", 1.0f);

            if (item.contains("metadata")) {
                sr.metadata = item["metadata"];
            } else if (item.contains("fields")) {
                sr.metadata = item["fields"];
            } else {
                // 整行作为元数据
                sr.metadata = item;
            }

            results.push_back(sr);
        }
    } catch (const nlohmann::json::exception& e) {
    spdlog::error("Hippo JSON error: {}", e.what());
} catch (const std::runtime_error& e) {
    spdlog::error("Hippo runtime error: {}", e.what());
} catch (const std::exception& e) {
    spdlog::error("Hippo error: {}", e.what());
}

    return results;
}

} // namespace ontology
