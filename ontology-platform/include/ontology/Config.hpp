#pragma once

#include "Core.hpp"
#include "Storage.hpp"
#include <memory>
#include <fstream>
#include <optional>

namespace ontology {

// ============================================================================
// 外部存储配置
// ============================================================================

struct StorageConfig {
    // Neo4j 配置
    struct Neo4jConfig {
        bool enabled = false;
        String uri = "bolt://localhost:7687";
        String username = "neo4j";
        String password = "";
        int connectionTimeout = 30;
        int maxPoolSize = 100;
    } neo4j;

    // Milvus 配置
    struct MilvusConfig {
        bool enabled = false;
        String host = "localhost";
        int port = 19530;
        String collection = "ontology_vectors";
        int dimension = 128;
        String metric = "cosine";
    } milvus;

    // Qdrant 配置
    struct QdrantConfig {
        bool enabled = false;
        String host = "localhost";
        int port = 6333;
        String collection = "ontology_vectors";
        int dimension = 128;
        String metric = "cosine";
    } qdrant;

    // PostgreSQL 配置
    struct PostgresConfig {
        bool enabled = false;
        String host = "localhost";
        int port = 5432;
        String database = "ontology";
        String username = "postgres";
        String password = "";
    } postgres;

    // MySQL 配置
    struct MysqlConfig {
        bool enabled = false;
        String host = "localhost";
        int port = 3306;
        String database = "ontology";
        String username = "root";
        String password = "";
    } mysql;

    // SQLite 配置
    struct SqliteConfig {
        bool enabled = false;
        String path = "ontology.db";
    } sqlite;

    // StellarDB (星环) 图数据库配置
    struct StellarDBConfig {
        bool enabled = false;
        String host = "localhost";
        int port = 8182;
        String graphName = "ontology";
        String username;
        String password;
        String token;
        bool useHttps = false;
    } stellardb;

    // Hippo (星环) 向量库配置
    struct HippoConfig {
        bool enabled = false;
        String host = "localhost";
        int port = 9200;
        String username;
        String password;
        String token;
        bool useHttps = false;
        bool enableSsl = false;
        String sslCertPath;
        int connectionTimeout = 30;
        int searchTimeout = 60;
        int maxPoolSize = 10;
    } hippo;

    Json toJson() const {
        Json j;
        j["neo4j"] = {
            {"enabled", neo4j.enabled},
            {"uri", neo4j.uri},
            {"username", neo4j.username},
            {"password", neo4j.password},
            {"connectionTimeout", neo4j.connectionTimeout},
            {"maxPoolSize", neo4j.maxPoolSize}
        };
        j["milvus"] = {
            {"enabled", milvus.enabled},
            {"host", milvus.host},
            {"port", milvus.port},
            {"collection", milvus.collection},
            {"dimension", milvus.dimension},
            {"metric", milvus.metric}
        };
        j["qdrant"] = {
            {"enabled", qdrant.enabled},
            {"host", qdrant.host},
            {"port", qdrant.port},
            {"collection", qdrant.collection},
            {"dimension", qdrant.dimension},
            {"metric", qdrant.metric}
        };
        j["postgres"] = {
            {"enabled", postgres.enabled},
            {"host", postgres.host},
            {"port", postgres.port},
            {"database", postgres.database},
            {"username", postgres.username},
            {"password", postgres.password}
        };
        j["mysql"] = {
            {"enabled", mysql.enabled},
            {"host", mysql.host},
            {"port", mysql.port},
            {"database", mysql.database},
            {"username", mysql.username},
            {"password", mysql.password}
        };
        j["sqlite"] = {
            {"enabled", sqlite.enabled},
            {"path", sqlite.path}
        };
        j["stellardb"] = {
            {"enabled", stellardb.enabled},
            {"host", stellardb.host},
            {"port", stellardb.port},
            {"graphName", stellardb.graphName},
            {"username", stellardb.username},
            {"password", stellardb.password},
            {"token", stellardb.token},
            {"useHttps", stellardb.useHttps}
        };
        j["hippo"] = {
            {"enabled", hippo.enabled},
            {"host", hippo.host},
            {"port", hippo.port},
            {"username", hippo.username},
            {"password", hippo.password},
            {"token", hippo.token},
            {"useHttps", hippo.useHttps},
            {"enableSsl", hippo.enableSsl},
            {"sslCertPath", hippo.sslCertPath},
            {"connectionTimeout", hippo.connectionTimeout},
            {"searchTimeout", hippo.searchTimeout},
            {"maxPoolSize", hippo.maxPoolSize}
        };
        return j;
    }

    static StorageConfig fromJson(const Json& j) {
        StorageConfig config;

        if (j.contains("neo4j")) {
            auto& n = j["neo4j"];
            config.neo4j.enabled = n.value("enabled", false);
            config.neo4j.uri = n.value("uri", "bolt://localhost:7687");
            config.neo4j.username = n.value("username", "neo4j");
            config.neo4j.password = n.value("password", "");
            config.neo4j.connectionTimeout = n.value("connectionTimeout", 30);
            config.neo4j.maxPoolSize = n.value("maxPoolSize", 100);
        }

        if (j.contains("milvus")) {
            auto& m = j["milvus"];
            config.milvus.enabled = m.value("enabled", false);
            config.milvus.host = m.value("host", "localhost");
            config.milvus.port = m.value("port", 19530);
            config.milvus.collection = m.value("collection", "ontology_vectors");
            config.milvus.dimension = m.value("dimension", 128);
            config.milvus.metric = m.value("metric", "cosine");
        }

        if (j.contains("qdrant")) {
            auto& q = j["qdrant"];
            config.qdrant.enabled = q.value("enabled", false);
            config.qdrant.host = q.value("host", "localhost");
            config.qdrant.port = q.value("port", 6333);
            config.qdrant.collection = q.value("collection", "ontology_vectors");
            config.qdrant.dimension = q.value("dimension", 128);
            config.qdrant.metric = q.value("metric", "cosine");
        }

        if (j.contains("postgres")) {
            auto& p = j["postgres"];
            config.postgres.enabled = p.value("enabled", false);
            config.postgres.host = p.value("host", "localhost");
            config.postgres.port = p.value("port", 5432);
            config.postgres.database = p.value("database", "ontology");
            config.postgres.username = p.value("username", "postgres");
            config.postgres.password = p.value("password", "");
        }

        if (j.contains("mysql")) {
            auto& m = j["mysql"];
            config.mysql.enabled = m.value("enabled", false);
            config.mysql.host = m.value("host", "localhost");
            config.mysql.port = m.value("port", 3306);
            config.mysql.database = m.value("database", "ontology");
            config.mysql.username = m.value("username", "root");
            config.mysql.password = m.value("password", "");
        }

        if (j.contains("sqlite")) {
            auto& s = j["sqlite"];
            config.sqlite.enabled = s.value("enabled", false);
            config.sqlite.path = s.value("path", "ontology.db");
        }

        if (j.contains("stellardb")) {
            auto& s = j["stellardb"];
            config.stellardb.enabled = s.value("enabled", false);
            config.stellardb.host = s.value("host", "localhost");
            config.stellardb.port = s.value("port", 8182);
            config.stellardb.graphName = s.value("graphName", "ontology");
            config.stellardb.username = s.value("username", "");
            config.stellardb.password = s.value("password", "");
            config.stellardb.token = s.value("token", "");
            config.stellardb.useHttps = s.value("useHttps", false);
        }

        if (j.contains("hippo")) {
            auto& h = j["hippo"];
            config.hippo.enabled = h.value("enabled", false);
            config.hippo.host = h.value("host", "localhost");
            config.hippo.port = h.value("port", 9200);
            config.hippo.username = h.value("username", "");
            config.hippo.password = h.value("password", "");
            config.hippo.token = h.value("token", "");
            config.hippo.useHttps = h.value("useHttps", false);
            config.hippo.enableSsl = h.value("enableSsl", false);
            config.hippo.sslCertPath = h.value("sslCertPath", "");
            config.hippo.connectionTimeout = h.value("connectionTimeout", 30);
            config.hippo.searchTimeout = h.value("searchTimeout", 60);
            config.hippo.maxPoolSize = h.value("maxPoolSize", 10);
        }

        return config;
    }
};

// ============================================================================
// 推理引擎配置
// ============================================================================

struct ReasonerConfig {
    // 符号推理配置
    struct SymbolicConfig {
        bool enabled = true;
        int maxInferenceDepth = 10;
        bool enableTransitiveClosure = true;
        bool enableInverseInference = true;
    } symbolic;

    // 神经推理配置
    struct NeuralConfig {
        bool enabled = true;
        String embeddingModel = "TransE";
        int embeddingDimension = 128;
        float learningRate = 0.01f;
        int trainingEpochs = 100;
        int negativeSamples = 10;
    } neural;

    // 混合推理配置
    struct HybridConfig {
        bool enabled = true;
        float symbolicWeight = 0.5f;
        float neuralWeight = 0.5f;
        float confidenceThreshold = 0.7f;
    } hybrid;

    Json toJson() const {
        return {
            {"symbolic", {
                {"enabled", symbolic.enabled},
                {"maxInferenceDepth", symbolic.maxInferenceDepth},
                {"enableTransitiveClosure", symbolic.enableTransitiveClosure},
                {"enableInverseInference", symbolic.enableInverseInference}
            }},
            {"neural", {
                {"enabled", neural.enabled},
                {"embeddingModel", neural.embeddingModel},
                {"embeddingDimension", neural.embeddingDimension},
                {"learningRate", neural.learningRate},
                {"trainingEpochs", neural.trainingEpochs},
                {"negativeSamples", neural.negativeSamples}
            }},
            {"hybrid", {
                {"enabled", hybrid.enabled},
                {"symbolicWeight", hybrid.symbolicWeight},
                {"neuralWeight", hybrid.neuralWeight},
                {"confidenceThreshold", hybrid.confidenceThreshold}
            }}
        };
    }

    static ReasonerConfig fromJson(const Json& j) {
        ReasonerConfig config;

        if (j.contains("symbolic")) {
            auto& s = j["symbolic"];
            config.symbolic.enabled = s.value("enabled", true);
            config.symbolic.maxInferenceDepth = s.value("maxInferenceDepth", 10);
            config.symbolic.enableTransitiveClosure = s.value("enableTransitiveClosure", true);
            config.symbolic.enableInverseInference = s.value("enableInverseInference", true);
        }

        if (j.contains("neural")) {
            auto& n = j["neural"];
            config.neural.enabled = n.value("enabled", true);
            config.neural.embeddingModel = n.value("embeddingModel", "TransE");
            config.neural.embeddingDimension = n.value("embeddingDimension", 128);
            config.neural.learningRate = n.value("learningRate", 0.01f);
            config.neural.trainingEpochs = n.value("trainingEpochs", 100);
            config.neural.negativeSamples = n.value("negativeSamples", 10);
        }

        if (j.contains("hybrid")) {
            auto& h = j["hybrid"];
            config.hybrid.enabled = h.value("enabled", true);
            config.hybrid.symbolicWeight = h.value("symbolicWeight", 0.5f);
            config.hybrid.neuralWeight = h.value("neuralWeight", 0.5f);
            config.hybrid.confidenceThreshold = h.value("confidenceThreshold", 0.7f);
        }

        return config;
    }
};

// ============================================================================
// 服务器配置
// ============================================================================

struct ServerConfig {
    String host = "0.0.0.0";
    int port = 8080;
    bool cors = true;
    String jwtSecret = "";
    int timeout = 30;
    int maxConnections = 100;
    bool enableMcp = true;
    int mcpPort = 9090;

    Json toJson() const {
        return {
            {"host", host},
            {"port", port},
            {"cors", cors},
            {"jwtSecret", jwtSecret},
            {"timeout", timeout},
            {"maxConnections", maxConnections},
            {"enableMcp", enableMcp},
            {"mcpPort", mcpPort}
        };
    }

    static ServerConfig fromJson(const Json& j) {
        ServerConfig config;
        config.host = j.value("host", "0.0.0.0");
        config.port = j.value("port", 8080);
        config.cors = j.value("cors", true);
        config.jwtSecret = j.value("jwtSecret", "");
        config.timeout = j.value("timeout", 30);
        config.maxConnections = j.value("maxConnections", 100);
        config.enableMcp = j.value("enableMcp", true);
        config.mcpPort = j.value("mcpPort", 9090);
        return config;
    }
};

// ============================================================================
// RAG 配置
// ============================================================================

struct RagConfig {
    bool enabled = true;
    String embeddingMethod = "hash_fingerprint";
    int chunkSize = 500;
    int chunkOverlap = 100;
    int topKChunks = 10;
    int embeddingDimension = 128;
    float bm25Weight = 0.3f;
    float vectorWeight = 0.4f;
    float graphWeight = 0.3f;
    bool enableReranker = true;
    bool enableMMR = true;
    bool enableCommunityDetection = true;
    bool extractEntities = true;
    float extractionConfidenceThreshold = 0.7f;

    Json toJson() const {
        return {
            {"enabled", enabled},
            {"embeddingMethod", embeddingMethod},
            {"chunkSize", chunkSize},
            {"chunkOverlap", chunkOverlap},
            {"topKChunks", topKChunks},
            {"embeddingDimension", embeddingDimension},
            {"bm25Weight", bm25Weight},
            {"vectorWeight", vectorWeight},
            {"graphWeight", graphWeight},
            {"enableReranker", enableReranker},
            {"enableMMR", enableMMR},
            {"enableCommunityDetection", enableCommunityDetection},
            {"extractEntities", extractEntities},
            {"extractionConfidenceThreshold", extractionConfidenceThreshold}
        };
    }

    static RagConfig fromJson(const Json& j) {
        RagConfig config;
        config.enabled = j.value("enabled", true);
        config.embeddingMethod = j.value("embeddingMethod", "hash_fingerprint");
        config.chunkSize = j.value("chunkSize", 500);
        config.chunkOverlap = j.value("chunkOverlap", 100);
        config.topKChunks = j.value("topKChunks", 10);
        config.embeddingDimension = j.value("embeddingDimension", 128);
        config.bm25Weight = j.value("bm25Weight", 0.3f);
        config.vectorWeight = j.value("vectorWeight", 0.4f);
        config.graphWeight = j.value("graphWeight", 0.3f);
        config.enableReranker = j.value("enableReranker", true);
        config.enableMMR = j.value("enableMMR", true);
        config.enableCommunityDetection = j.value("enableCommunityDetection", true);
        config.extractEntities = j.value("extractEntities", true);
        config.extractionConfidenceThreshold = j.value("extractionConfidenceThreshold", 0.7f);
        return config;
    }
};

// ============================================================================
// Embedding 配置
// ============================================================================

struct EmbeddingConfig {
    String apiKey = "";
    String endpoint = "https://api.openai.com/v1";
    String model = "text-embedding-3-small";
    String ollamaEndpoint = "http://localhost:11434";
    String ollamaModel = "nomic-embed-text";

    Json toJson() const {
        return {
            {"apiKey", apiKey},
            {"endpoint", endpoint},
            {"model", model},
            {"ollamaEndpoint", ollamaEndpoint},
            {"ollamaModel", ollamaModel}
        };
    }

    static EmbeddingConfig fromJson(const Json& j) {
        EmbeddingConfig config;
        config.apiKey = j.value("apiKey", "");
        config.endpoint = j.value("endpoint", "https://api.openai.com/v1");
        config.model = j.value("model", "text-embedding-3-small");
        config.ollamaEndpoint = j.value("ollamaEndpoint", "http://localhost:11434");
        config.ollamaModel = j.value("ollamaModel", "nomic-embed-text");
        return config;
    }
};

// ============================================================================
// Persistence 配置
// ============================================================================

struct PersistenceConfig {
    bool enableWal = true;
    bool enableSnapshots = true;
    String walDirectory = "./wal";
    String snapshotDirectory = "./snapshots";

    Json toJson() const {
        return {
            {"enableWal", enableWal},
            {"enableSnapshots", enableSnapshots},
            {"walDirectory", walDirectory},
            {"snapshotDirectory", snapshotDirectory}
        };
    }

    static PersistenceConfig fromJson(const Json& j) {
        PersistenceConfig config;
        config.enableWal = j.value("enableWal", true);
        config.enableSnapshots = j.value("enableSnapshots", true);
        config.walDirectory = j.value("walDirectory", "./wal");
        config.snapshotDirectory = j.value("snapshotDirectory", "./snapshots");
        return config;
    }
};

// ============================================================================
// Validation 配置
// ============================================================================

struct ValidationConfig {
    bool enableShacl = true;

    Json toJson() const {
        return {
            {"enableShacl", enableShacl}
        };
    }

    static ValidationConfig fromJson(const Json& j) {
        ValidationConfig config;
        config.enableShacl = j.value("enableShacl", true);
        return config;
    }
};

// ============================================================================
// LLM 配置
// ============================================================================

struct LlmConfig {
    String endpoint = "";
    String model = "";
    String apiPath = "/v1/chat/completions";
    String apiKey = "";
    int timeoutMs = 60000;
    int maxTokens = 4096;
    bool enableImageToText = false;

    Json toJson() const {
        return {
            {"endpoint", endpoint},
            {"model", model},
            {"apiPath", apiPath},
            {"apiKey", apiKey},
            {"timeoutMs", timeoutMs},
            {"maxTokens", maxTokens},
            {"enableImageToText", enableImageToText}
        };
    }

    static LlmConfig fromJson(const Json& j) {
        LlmConfig config;
        config.endpoint = j.value("endpoint", "");
        config.model = j.value("model", "");
        config.apiPath = j.value("apiPath", "/v1/chat/completions");
        config.apiKey = j.value("apiKey", "");
        config.timeoutMs = j.value("timeoutMs", 60000);
        config.maxTokens = j.value("maxTokens", 4096);
        config.enableImageToText = j.value("enableImageToText", false);
        return config;
    }
};

// ============================================================================
// Rerank 配置
// ============================================================================

struct RerankConfig {
    String endpoint = "";
    String model = "";
    String apiPath = "/v1/rerank";
    String apiKey = "";
    int timeoutMs = 30000;

    Json toJson() const {
        return {
            {"endpoint", endpoint},
            {"model", model},
            {"apiPath", apiPath},
            {"apiKey", apiKey},
            {"timeoutMs", timeoutMs}
        };
    }

    static RerankConfig fromJson(const Json& j) {
        RerankConfig config;
        config.endpoint = j.value("endpoint", "");
        config.model = j.value("model", "");
        config.apiPath = j.value("apiPath", "/v1/rerank");
        config.apiKey = j.value("apiKey", "");
        config.timeoutMs = j.value("timeoutMs", 30000);
        return config;
    }
};

// ============================================================================
// LocalEmbedding 配置
// ============================================================================

struct LocalEmbeddingConfig {
    String endpoint = "";
    String model = "";
    String apiPath = "/v1/embeddings";
    String apiKey = "";
    int timeoutMs = 30000;

    Json toJson() const {
        return {
            {"endpoint", endpoint},
            {"model", model},
            {"apiPath", apiPath},
            {"apiKey", apiKey},
            {"timeoutMs", timeoutMs}
        };
    }

    static LocalEmbeddingConfig fromJson(const Json& j) {
        LocalEmbeddingConfig config;
        config.endpoint = j.value("endpoint", "");
        config.model = j.value("model", "");
        config.apiPath = j.value("apiPath", "/v1/embeddings");
        config.apiKey = j.value("apiKey", "");
        config.timeoutMs = j.value("timeoutMs", 30000);
        return config;
    }
};

// ============================================================================
// 完整配置
// ============================================================================

class OntologyConfig {
public:
    StorageConfig storage;
    ReasonerConfig reasoner;
    ServerConfig server;
    RagConfig rag;
    EmbeddingConfig embedding;
    PersistenceConfig persistence;
    ValidationConfig validation;
    LlmConfig llm;
    RerankConfig rerank;
    LocalEmbeddingConfig localEmbedding;

    /// 从文件加载配置
    static OntologyConfig load(const String& path) {
        OntologyConfig config;

        std::ifstream file(path);
        if (!file.is_open()) {
            return config;
        }

        try {
            Json j = Json::parse(file);
            config = fromJson(j);
        } catch (const std::exception&) {
            // 解析失败，返回默认配置
        }

        return config;
    }

    /// 保存配置到文件
    void save(const String& path) const {
        Json j = toJson();
        std::ofstream file(path);
        file << j.dump(2);
    }

    /// 转换为 JSON
    Json toJson() const {
        Json j;
        j["storage"]      = storage.toJson();
        j["reasoner"]     = reasoner.toJson();
        j["server"]       = server.toJson();
        j["rag"]          = rag.toJson();
        j["embedding"]    = embedding.toJson();
        j["persistence"]  = persistence.toJson();
        j["validation"]   = validation.toJson();
        j["llm"]          = llm.toJson();
        j["rerank"]       = rerank.toJson();
        j["localEmbedding"] = localEmbedding.toJson();

        // Preserve backward-compat: localModel section derived from llm/rerank/localEmbedding
        Json localModel;
        if (!localEmbedding.endpoint.empty()) {
            localModel["embedding"] = localEmbedding.toJson();
        }
        if (!rerank.endpoint.empty()) {
            localModel["rerank"] = rerank.toJson();
        }
        if (!llm.endpoint.empty()) {
            localModel["llm"] = llm.toJson();
        }
        localModel["enableImageToText"] = llm.enableImageToText;
        j["localModel"] = localModel;

        return j;
    }

    /// 从 JSON 创建
    static OntologyConfig fromJson(const Json& j) {
        OntologyConfig config;
        if (j.contains("storage")) {
            config.storage = StorageConfig::fromJson(j["storage"]);
        }
        if (j.contains("reasoner")) {
            config.reasoner = ReasonerConfig::fromJson(j["reasoner"]);
        }
        if (j.contains("server")) {
            config.server = ServerConfig::fromJson(j["server"]);
        }
        if (j.contains("rag")) {
            config.rag = RagConfig::fromJson(j["rag"]);
        }
        if (j.contains("embedding")) {
            config.embedding = EmbeddingConfig::fromJson(j["embedding"]);
        }
        if (j.contains("persistence")) {
            config.persistence = PersistenceConfig::fromJson(j["persistence"]);
        }
        if (j.contains("validation")) {
            config.validation = ValidationConfig::fromJson(j["validation"]);
        }
        if (j.contains("llm")) {
            config.llm = LlmConfig::fromJson(j["llm"]);
        }
        if (j.contains("rerank")) {
            config.rerank = RerankConfig::fromJson(j["rerank"]);
        }
        if (j.contains("localEmbedding")) {
            config.localEmbedding = LocalEmbeddingConfig::fromJson(j["localEmbedding"]);
        }

        // Backward-compat: populate llm/rerank/localEmbedding from localModel section
        if (j.contains("localModel")) {
            const auto& lm = j["localModel"];
            if (lm.contains("llm") && config.llm.endpoint.empty()) {
                config.llm = LlmConfig::fromJson(lm["llm"]);
                config.llm.enableImageToText = lm.value("enableImageToText", false);
            }
            if (lm.contains("rerank") && config.rerank.endpoint.empty()) {
                config.rerank = RerankConfig::fromJson(lm["rerank"]);
            }
            if (lm.contains("embedding") && config.localEmbedding.endpoint.empty()) {
                config.localEmbedding = LocalEmbeddingConfig::fromJson(lm["embedding"]);
            }
        }

        return config;
    }

    /// Validate config values. Returns empty vector on success, or error messages.
    std::vector<String> validate() const {
        std::vector<String> errors;

        // server.port in 1-65535
        if (server.port < 1 || server.port > 65535) {
            errors.push_back("server.port must be between 1 and 65535, got " + std::to_string(server.port));
        }

        // reasoner.hybrid.symbolicWeight in 0-1
        if (reasoner.hybrid.symbolicWeight < 0.0f || reasoner.hybrid.symbolicWeight > 1.0f) {
            errors.push_back("reasoner.hybrid.symbolicWeight must be between 0 and 1, got " + std::to_string(reasoner.hybrid.symbolicWeight));
        }

        // reasoner.hybrid.neuralWeight in 0-1
        if (reasoner.hybrid.neuralWeight < 0.0f || reasoner.hybrid.neuralWeight > 1.0f) {
            errors.push_back("reasoner.hybrid.neuralWeight must be between 0 and 1, got " + std::to_string(reasoner.hybrid.neuralWeight));
        }

        // rag weights non-negative
        if (rag.bm25Weight < 0.0f) {
            errors.push_back("rag.bm25Weight must be non-negative, got " + std::to_string(rag.bm25Weight));
        }
        if (rag.vectorWeight < 0.0f) {
            errors.push_back("rag.vectorWeight must be non-negative, got " + std::to_string(rag.vectorWeight));
        }
        if (rag.graphWeight < 0.0f) {
            errors.push_back("rag.graphWeight must be non-negative, got " + std::to_string(rag.graphWeight));
        }

        // rag.chunkSize positive
        if (rag.chunkSize <= 0) {
            errors.push_back("rag.chunkSize must be positive, got " + std::to_string(rag.chunkSize));
        }

        return errors;
    }

    /// 验证连接
    struct ConnectionStatus {
        String service;
        bool connected;
        String message;
        Json details;
    };

    std::vector<ConnectionStatus> validateConnections() const {
        std::vector<ConnectionStatus> results;

        // 检查 Neo4j
        if (storage.neo4j.enabled) {
            ConnectionStatus status;
            status.service = "neo4j";
            try {
                Neo4jClient::Config cfg;
                cfg.uri = storage.neo4j.uri;
                cfg.username = storage.neo4j.username;
                cfg.password = storage.neo4j.password;
                cfg.connectionTimeout = storage.neo4j.connectionTimeout;
                cfg.maxConnectionPoolSize = storage.neo4j.maxPoolSize;

                Neo4jClient client(cfg);
                status.connected = client.connect();
                status.message = status.connected ? "Connected successfully" : "Connection failed";
                status.details = {
                    {"uri", storage.neo4j.uri},
                    {"username", storage.neo4j.username}
                };
            } catch (const std::exception& e) {
                status.connected = false;
                status.message = String("Error: ") + e.what();
            }
            results.push_back(status);
        }

        // 检查 Milvus
        if (storage.milvus.enabled) {
            ConnectionStatus status;
            status.service = "milvus";
            try {
                MilvusClient::Config cfg;
                cfg.host = storage.milvus.host;
                cfg.port = storage.milvus.port;

                MilvusClient client(cfg);
                status.connected = client.connect();
                status.message = status.connected ? "Connected successfully" : "Connection failed";
                status.details = {
                    {"host", storage.milvus.host},
                    {"port", storage.milvus.port},
                    {"collection", storage.milvus.collection}
                };
            } catch (const std::exception& e) {
                status.connected = false;
                status.message = String("Error: ") + e.what();
            }
            results.push_back(status);
        }

        // 检查 Qdrant
        if (storage.qdrant.enabled) {
            ConnectionStatus status;
            status.service = "qdrant";
            try {
                QdrantClient::Config cfg;
                cfg.host = storage.qdrant.host;
                cfg.port = storage.qdrant.port;

                QdrantClient client(cfg);
                status.connected = client.connect();
                status.message = status.connected ? "Connected successfully" : "Connection failed";
                status.details = {
                    {"host", storage.qdrant.host},
                    {"port", storage.qdrant.port},
                    {"collection", storage.qdrant.collection}
                };
            } catch (const std::exception& e) {
                status.connected = false;
                status.message = String("Error: ") + e.what();
            }
            results.push_back(status);
        }

        return results;
    }
};

} // namespace ontology
