#include "Bootstrap.hpp"

#include <ontology/Storage.hpp>
#include <ontology/Inference.hpp>
#include <ontology/Neural.hpp>
#include <ontology/Swrl.hpp>
#include <ontology/Sparql.hpp>
#include <ontology/TextEmbedding.hpp>
#include <ontology/RagStorage.hpp>
#include <ontology/RagPipeline.hpp>
#include <ontology/RagQuery.hpp>
#include <ontology/HybridRetrieval.hpp>
#include <ontology/QueryTransform.hpp>
#include <ontology/Reranker.hpp>
#include <ontology/SemanticChunker.hpp>
#include <ontology/CommunityDetection.hpp>
#include <ontology/EmbeddingService.hpp>
#include <ontology/Persistence.hpp>
#include <ontology/ShaclValidation.hpp>
#include <ontology/Explainability.hpp>
#include <ontology/Temporal.hpp>
#include <ontology/GraphRagAgent.hpp>
#include <ontology/NeuroSymbolicLearning.hpp>
#include <ontology/LlmBackend.hpp>
#include <ontology/DocumentPreprocessor.hpp>

#include <iostream>

namespace ontology {

// ============================================================================
// Public entry point
// ============================================================================

ServiceContextPtr Bootstrap::initialize(const OntologyConfig& config) {
    auto ctx = std::make_shared<ServiceContext>();

    initStorage(*ctx, config);
    initEmbedding(*ctx, config);
    initInference(*ctx, config);
    initRag(*ctx, config);
    initPersistence(*ctx, config);

    // --- Neuro-Symbolic Learning ---
    int embeddingDim = config.rag.embeddingDimension;

    NeuroSymbolicLearnerConfig nslConfig;
    ctx->neuroSymbolicLearner = std::make_shared<NeuroSymbolicLearner>(
        std::make_shared<TransEEmbedding>(embeddingDim), ctx->storage, nslConfig);
    std::cout << "  Neuro-Symbolic Learning: enabled\n";

    // --- Explainability ---
    ctx->explainabilityEngine = std::make_unique<ExplainabilityEngine>();
    if (ctx->symbolicReasoner)
        ctx->symbolicReasoner->setExplainabilityEngine(ctx->explainabilityEngine.get());
    if (ctx->neuralReasoner)
        ctx->neuralReasoner->setExplainabilityEngine(ctx->explainabilityEngine.get());
    if (ctx->hybridReasoner)
        ctx->hybridReasoner->setExplainabilityEngine(ctx->explainabilityEngine.get());
    std::cout << "  Explainability: enabled\n";

    // --- Truth Maintenance System ---
    // TMS is integrated inside IncrementalReasoner via Temporal.hpp
    std::cout << "  TMS: enabled\n";

    return ctx;
}

// ============================================================================
// Storage initialization
// ============================================================================

void Bootstrap::initStorage(ServiceContext& ctx, const OntologyConfig& config) {
    // Defaults
    String graphDbType = "neo4j";
    String graphDbUri  = "bolt://localhost:7687";
    String graphDbUser = "neo4j";
    String graphDbPass = "password";
    String vectorDbType = "milvus";
    String vectorDbHost = "localhost";
    int    vectorDbPort = 19530;
    int    embeddingDim = config.rag.embeddingDimension;

    // Resolve graph DB type from enabled storage configs
    if (config.storage.neo4j.enabled) {
        graphDbType = "neo4j";
        graphDbUri  = config.storage.neo4j.uri;
        graphDbUser = config.storage.neo4j.username;
        graphDbPass = config.storage.neo4j.password;
    } else if (config.storage.stellardb.enabled) {
        graphDbType = "stellardb";
    }

    // Resolve vector DB type from enabled storage configs
    if (config.storage.milvus.enabled) {
        vectorDbType = "milvus";
        vectorDbHost = config.storage.milvus.host;
        vectorDbPort = config.storage.milvus.port;
        embeddingDim = config.storage.milvus.dimension;
    } else if (config.storage.qdrant.enabled) {
        vectorDbType = "qdrant";
        vectorDbHost = config.storage.qdrant.host;
        vectorDbPort = config.storage.qdrant.port;
        embeddingDim = config.storage.qdrant.dimension;
    } else if (config.storage.hippo.enabled) {
        vectorDbType = "hippo";
        embeddingDim = config.rag.embeddingDimension;
    }

    // Graph database
    if (graphDbType == "neo4j") {
        Neo4jClient::Config neo4jConfig;
        neo4jConfig.uri = graphDbUri;
        neo4jConfig.username = graphDbUser;
        neo4jConfig.password = graphDbPass;
        ctx.graphDB = std::make_shared<Neo4jClient>(neo4jConfig);

        std::cout << "Connecting to Neo4j...\n";
        if (!ctx.graphDB->connect()) {
            std::cout << "Warning: Could not connect to Neo4j, using in-memory storage\n";
            ctx.graphDB.reset();
        }
    } else if (graphDbType == "stellardb") {
        StellarDBClient::Config stellarConfig;
        const auto& sdb = config.storage.stellardb;
        stellarConfig.host      = sdb.host;
        stellarConfig.port      = sdb.port;
        stellarConfig.graphName = sdb.graphName;
        stellarConfig.username  = sdb.username;
        stellarConfig.password  = sdb.password;
        stellarConfig.token     = sdb.token;
        stellarConfig.useHttps  = sdb.useHttps;
        ctx.graphDB = std::make_shared<StellarDBClient>(stellarConfig);

        std::cout << "Connecting to StellarDB (" << stellarConfig.host
                  << ":" << stellarConfig.port << ")...\n";
        if (!ctx.graphDB->connect()) {
            std::cout << "Warning: Could not connect to StellarDB, using in-memory storage\n";
            ctx.graphDB.reset();
        }
    }

    // Vector database
    if (vectorDbType == "milvus") {
        MilvusClient::Config milvusConfig;
        milvusConfig.host = vectorDbHost;
        milvusConfig.port = vectorDbPort;
        ctx.vectorDB = std::make_shared<MilvusClient>(milvusConfig);

        std::cout << "Connecting to Milvus...\n";
        if (!ctx.vectorDB->connect()) {
            std::cout << "Warning: Could not connect to Milvus, vector search disabled\n";
            ctx.vectorDB.reset();
        }
    } else if (vectorDbType == "qdrant") {
        QdrantClient::Config qdrantConfig;
        qdrantConfig.host = vectorDbHost;
        qdrantConfig.port = vectorDbPort;
        ctx.vectorDB = std::make_shared<QdrantClient>(qdrantConfig);

        std::cout << "Connecting to Qdrant...\n";
        if (!ctx.vectorDB->connect()) {
            std::cout << "Warning: Could not connect to Qdrant, vector search disabled\n";
            ctx.vectorDB.reset();
        }
    } else if (vectorDbType == "hippo") {
        HippoClient::Config hippoConfig;
        const auto& hp = config.storage.hippo;
        hippoConfig.host       = hp.host;
        hippoConfig.port       = hp.port;
        hippoConfig.username   = hp.username;
        hippoConfig.password   = hp.password;
        hippoConfig.token      = hp.token;
        hippoConfig.useHttps   = hp.useHttps;
        hippoConfig.enableSsl  = hp.enableSsl;
        hippoConfig.sslCertPath = hp.sslCertPath;
        ctx.vectorDB = std::make_shared<HippoClient>(hippoConfig);

        std::cout << "Connecting to Hippo (" << hippoConfig.host
                  << ":" << hippoConfig.port << ")...\n";
        if (!ctx.vectorDB->connect()) {
            std::cout << "Warning: Could not connect to Hippo, vector search disabled\n";
            ctx.vectorDB.reset();
        }
    }

    // Hybrid storage
    ctx.storage = std::make_shared<HybridStorage>(ctx.graphDB, ctx.vectorDB);
    ctx.storage->initialize("ontology", embeddingDim);

    std::cout << "  Graph DB: " << graphDbType << " (" << graphDbUri << ")\n";
    std::cout << "  Vector DB: " << vectorDbType << " (" << vectorDbHost << ":" << vectorDbPort << ")\n";
    std::cout << "  Embedding Dimension: " << embeddingDim << "\n";
}

// ============================================================================
// Embedding service initialization
// ============================================================================

void Bootstrap::initEmbedding(ServiceContext& ctx, const OntologyConfig& config) {
    int embeddingDim = config.rag.embeddingDimension;
    String embeddingMethod = config.rag.embeddingMethod;

    // EmbeddingService
    EmbeddingServiceConfig embConfig;
    embConfig.dimension   = embeddingDim;
    embConfig.enableCache = true;

    if (embeddingMethod == "openai" && !config.embedding.apiKey.empty()) {
        embConfig.backend    = EmbeddingBackend::OpenAI;
        embConfig.apiKey     = config.embedding.apiKey;
        embConfig.endpoint   = config.embedding.endpoint;
        embConfig.model      = config.embedding.model;
    } else if (embeddingMethod == "ollama") {
        embConfig.backend         = EmbeddingBackend::Ollama;
        embConfig.ollamaEndpoint  = config.embedding.ollamaEndpoint;
        embConfig.ollamaModel     = config.embedding.ollamaModel;
    } else {
        embConfig.backend = EmbeddingBackend::HashFingerprint;
    }

    ctx.embeddingService = std::make_shared<EmbeddingService>(embConfig);

    // Local embedding backend (vLLM/TEI compatible)
    if (!config.localEmbedding.endpoint.empty()) {
        auto localEmbBackend = std::make_shared<OpenAIEmbeddingBackend>(
            config.localEmbedding.apiKey,
            config.localEmbedding.endpoint,
            config.localEmbedding.model.empty() ? "default" : config.localEmbedding.model,
            embeddingDim,
            32,
            config.localEmbedding.apiPath,
            config.localEmbedding.timeoutMs
        );
        ctx.embeddingService->registerBackend("local", localEmbBackend);
        std::cout << "  Local Embedding: " << config.localEmbedding.endpoint
                  << " (model=" << config.localEmbedding.model << ")\n";
    }

    // TextEmbedder
    if (!config.localEmbedding.endpoint.empty() || embeddingMethod == "openai") {
        ctx.textEmbedder = std::make_shared<TextEmbedder>(ctx.embeddingService);
    } else {
        TextEmbedder::Config legacyConfig;
        legacyConfig.dimension = embeddingDim;
        legacyConfig.method    = embeddingMethod;
        legacyConfig.apiKey    = config.embedding.apiKey;
        legacyConfig.endpoint  = config.embedding.endpoint;
        legacyConfig.model     = config.embedding.model;
        ctx.textEmbedder = std::make_shared<TextEmbedder>(legacyConfig);
    }

    // Local LLM backend
    if (!config.llm.endpoint.empty()) {
        HttpLlmBackend::Config llmConfig;
        llmConfig.endpoint   = config.llm.endpoint;
        llmConfig.model      = config.llm.model;
        llmConfig.apiPath    = config.llm.apiPath;
        llmConfig.apiKey     = config.llm.apiKey;
        llmConfig.timeoutMs  = config.llm.timeoutMs;
        llmConfig.maxTokens  = config.llm.maxTokens;
        llmConfig.enableImage = config.llm.enableImageToText;
        ctx.llmBackend = std::make_shared<HttpLlmBackend>(llmConfig);
        std::cout << "  Local LLM: " << config.llm.endpoint
                  << " (model=" << config.llm.model
                  << ", image2text=" << (config.llm.enableImageToText ? "on" : "off") << ")\n";
    }

    // Document Preprocessor
    if (ctx.llmBackend || config.rag.enabled) {
        DocumentPreprocessor::Config prepConfig;
        prepConfig.enableImageToText = config.llm.enableImageToText;
        ctx.documentPreprocessor = std::make_shared<DocumentPreprocessor>(ctx.llmBackend, prepConfig);
        std::cout << "  Document Preprocessor: enabled\n";
    }
}

// ============================================================================
// Inference engines initialization
// ============================================================================

void Bootstrap::initInference(ServiceContext& ctx, const OntologyConfig& config) {
    int embeddingDim = config.rag.embeddingDimension;
    float symbolicWeight = config.reasoner.hybrid.symbolicWeight;
    float neuralWeight   = config.reasoner.hybrid.neuralWeight;

    ctx.symbolicReasoner = std::make_shared<SymbolicReasoner>(ctx.storage);
    ctx.neuralReasoner   = std::make_shared<NeuralReasoner>(ctx.storage, embeddingDim);
    ctx.hybridReasoner   = std::make_shared<HybridReasoner>(ctx.symbolicReasoner, ctx.neuralReasoner);

    HybridReasoner::Config hybridConfig;
    hybridConfig.symbolWeight = symbolicWeight;
    hybridConfig.neuralWeight = neuralWeight;
    ctx.hybridReasoner->setStorage(ctx.storage);

    ctx.swrlEngine      = std::make_shared<SwrlEngine>(ctx.storage);
    ctx.sparqlEndpoint  = std::make_shared<SparqlEndpoint>(ctx.storage);

    // SHACL
    if (config.validation.enableShacl) {
        ctx.shaclValidator = std::make_shared<ShaclValidator>(ctx.storage);
        ctx.incrementalReasoner = std::make_shared<IncrementalReasoner>(
            ctx.storage, ctx.symbolicReasoner, ctx.shaclValidator);
        std::cout << "  SHACL: enabled\n";
    }
}

// ============================================================================
// RAG components initialization
// ============================================================================

void Bootstrap::initRag(ServiceContext& ctx, const OntologyConfig& config) {
    if (!config.rag.enabled) {
        std::cout << "  RAG: disabled\n";
        return;
    }

    int embeddingDim = config.rag.embeddingDimension;

    // RagStorage
    ctx.ragStorage = std::make_shared<RagStorage>(ctx.storage, ctx.vectorDB);
    ctx.ragStorage->initializeCollection(embeddingDim);

    // Semantic Chunker
    SemanticChunker::Config chunkerConfig;
    chunkerConfig.chunkSize    = config.rag.chunkSize;
    chunkerConfig.chunkOverlap = config.rag.chunkOverlap;
    ctx.semanticChunker = std::make_shared<SemanticChunker>(ctx.textEmbedder, chunkerConfig);

    // RagPipeline
    RagPipeline::Config pipelineConfig;
    pipelineConfig.splitter.chunkSize    = config.rag.chunkSize;
    pipelineConfig.splitter.chunkOverlap = config.rag.chunkOverlap;
    ctx.ragPipeline = std::make_shared<RagPipeline>(
        ctx.textEmbedder, ctx.ragStorage, ctx.storage, nullptr);
    if (ctx.documentPreprocessor) ctx.ragPipeline->setDocumentPreprocessor(ctx.documentPreprocessor);
    if (ctx.llmBackend)           ctx.ragPipeline->setLlmBackend(ctx.llmBackend);

    // Hybrid Retrieval Engine (BM25 + Vector + Graph)
    ctx.hybridRetrieval = std::make_shared<HybridRetrievalEngine>(
        ctx.textEmbedder, ctx.ragStorage, ctx.storage);
    HybridRetrievalEngine::Config retConfig;
    retConfig.bm25Weight   = config.rag.bm25Weight;
    retConfig.vectorWeight = config.rag.vectorWeight;
    retConfig.graphWeight  = config.rag.graphWeight;
    ctx.hybridRetrieval->setConfig(retConfig);

    // Query Transform Engine (HyDE + Expansion + Decomposition)
    ctx.queryTransform = std::make_shared<QueryTransformEngine>(
        ctx.textEmbedder, ctx.storage);

    // Reranker Engine (Cross-encoder + MMR)
    if (config.rag.enableReranker) {
        ctx.reranker = std::make_shared<RerankerEngine>(ctx.textEmbedder, ctx.storage);
        RerankerEngine::Config rerankConfig;
        rerankConfig.enableMMR = config.rag.enableMMR;
        ctx.reranker->setConfig(rerankConfig);

        // Wire HTTP rerank backend if available
        if (!config.rerank.endpoint.empty()) {
            HttpRerankBackend::Config rerankBackendConfig;
            rerankBackendConfig.endpoint  = config.rerank.endpoint;
            rerankBackendConfig.model     = config.rerank.model;
            rerankBackendConfig.apiPath   = config.rerank.apiPath;
            rerankBackendConfig.apiKey    = config.rerank.apiKey;
            rerankBackendConfig.timeoutMs = config.rerank.timeoutMs;
            ctx.reranker->setRerankBackend(std::make_shared<HttpRerankBackend>(rerankBackendConfig));
            std::cout << "  Local Rerank: " << config.rerank.endpoint
                      << " (model=" << config.rerank.model << ")\n";
        }
    }

    // Community Detector (GraphRAG)
    if (config.rag.enableCommunityDetection) {
        ctx.communityDetector = std::make_shared<CommunityDetector>(
            ctx.storage, ctx.textEmbedder);
    }

    // GraphRAG Agent (ReAct iterative query)
    GraphRagAgentConfig agentConfig;
    ctx.graphRagAgent = std::make_shared<GraphRagAgent>(ctx.storage, ctx.textEmbedder, agentConfig);
    ctx.graphRagAgent->setSymbolicReasoner(ctx.symbolicReasoner);
    ctx.graphRagAgent->setNeuralReasoner(ctx.neuralReasoner);
    if (ctx.hybridRetrieval)   ctx.graphRagAgent->setHybridRetrieval(ctx.hybridRetrieval);
    if (ctx.communityDetector) ctx.graphRagAgent->setCommunityDetector(ctx.communityDetector);
    if (ctx.ragStorage)        ctx.graphRagAgent->setRagStorage(ctx.ragStorage);

    // RagQueryEngine (created after graphRagAgent so it can be wired back)
    ctx.ragQueryEngine = std::make_shared<RagQueryEngine>(
        ctx.textEmbedder, ctx.ragStorage, ctx.storage,
        ctx.neuralReasoner.get(), ctx.hybridReasoner.get(), nullptr);

    if (ctx.hybridRetrieval)   ctx.ragQueryEngine->setHybridRetrieval(ctx.hybridRetrieval);
    if (ctx.communityDetector) ctx.ragQueryEngine->setCommunityDetector(ctx.communityDetector);

    // Wire RagQueryEngine into GraphRagAgent
    if (ctx.ragQueryEngine) ctx.graphRagAgent->setRagQueryEngine(ctx.ragQueryEngine);

    // Inject TextEmbedder into NeuralReasoner
    if (ctx.neuralReasoner) ctx.neuralReasoner->setTextEmbedder(ctx.textEmbedder);

    std::cout << "  RAG: enabled (embedding=" << config.rag.embeddingMethod
              << ", dim=" << embeddingDim << ")\n";
    std::cout << "  Hybrid Retrieval: BM25=" << config.rag.bm25Weight
              << " Vector=" << config.rag.vectorWeight
              << " Graph=" << config.rag.graphWeight << "\n";
    std::cout << "  Query Transform: HyDE + Expansion + Decomposition\n";
    std::cout << "  Reranker: " << (config.rag.enableReranker ? "enabled" : "disabled")
              << " MMR: " << (config.rag.enableMMR ? "enabled" : "disabled") << "\n";
    std::cout << "  Community Detection: " << (config.rag.enableCommunityDetection ? "enabled" : "disabled") << "\n";
}

// ============================================================================
// Persistence initialization
// ============================================================================

void Bootstrap::initPersistence(ServiceContext& ctx, const OntologyConfig& config) {
    if (config.persistence.enableWal) {
        WalManager::Config walConfig;
        walConfig.walDirectory = config.persistence.walDirectory;
        ctx.walManager = std::make_shared<WalManager>(walConfig);
        std::cout << "  WAL: enabled (" << config.persistence.walDirectory << ")\n";
    }

    if (config.persistence.enableSnapshots) {
        SnapshotManager::Config snapConfig;
        snapConfig.snapshotDirectory = config.persistence.snapshotDirectory;
        ctx.snapshotManager = std::make_shared<SnapshotManager>(snapConfig);
        std::cout << "  Snapshots: enabled (" << config.persistence.snapshotDirectory << ")\n";
    }

    // Start auto-snapshot thread
    if (ctx.snapshotManager && config.persistence.enableSnapshots && ctx.storage) {
        ctx.snapshotManager->startAutoSnapshot(ctx.storage, ctx.ragStorage);
    }
}

} // namespace ontology
