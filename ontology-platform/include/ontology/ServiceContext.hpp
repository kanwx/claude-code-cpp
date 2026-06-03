#pragma once
#include <memory>
#include <ontology/bootstrap/RecoveryManager.hpp>

namespace ontology {

// Forward declarations
class HybridStorage;
class GraphDatabase;
class VectorDatabase;
class HybridReasoner;
class SymbolicReasoner;
class NeuralReasoner;
class SwrlEngine;
class SparqlEndpoint;
class TextEmbedder;
class EmbeddingService;
class RagStorage;
class RagPipeline;
class RagQueryEngine;
class HybridRetrievalEngine;
class QueryTransformEngine;
class RerankerEngine;
class SemanticChunker;
class CommunityDetector;
class GraphRagAgent;
class LlmBackend;
class DocumentPreprocessor;
class NeuroSymbolicLearner;
class WalManager;
class SnapshotManager;
class ShaclValidator;
class IncrementalReasoner;
class ExplainabilityEngine;

// Type aliases for storage — previously from storage/Forward.hpp via Storage.hpp
using StoragePtr        = std::shared_ptr<HybridStorage>;
using GraphDatabasePtr  = std::shared_ptr<GraphDatabase>;
using VectorDatabasePtr = std::shared_ptr<VectorDatabase>;

// Type aliases for reasoning — these were previously in Api.hpp.
// Now defined here alongside ServiceContext so that Api.hpp does not
// need to carry them.
using HybridReasonerPtr   = std::shared_ptr<HybridReasoner>;
using SymbolicReasonerPtr = std::shared_ptr<SymbolicReasoner>;
using NeuralReasonerPtr   = std::shared_ptr<NeuralReasoner>;
using SwrlEnginePtr       = std::shared_ptr<SwrlEngine>;
using SparqlEndpointPtr   = std::shared_ptr<SparqlEndpoint>;

/// Dependency injection container holding all service instances.
/// Replaces the 20+ setter injection methods on HttpServer with a single struct.
struct ServiceContext {
    // Destructor must be defined in .cpp where complete types are visible
    ~ServiceContext();

    // --- Core storage ---
    StoragePtr storage;
    GraphDatabasePtr graphDB;
    VectorDatabasePtr vectorDB;

    // --- Reasoning ---
    HybridReasonerPtr hybridReasoner;
    SymbolicReasonerPtr symbolicReasoner;
    NeuralReasonerPtr neuralReasoner;
    SwrlEnginePtr swrlEngine;
    SparqlEndpointPtr sparqlEndpoint;

    // --- Embedding & RAG ---
    std::shared_ptr<TextEmbedder> textEmbedder;
    std::shared_ptr<EmbeddingService> embeddingService;
    std::shared_ptr<RagStorage> ragStorage;
    std::shared_ptr<RagPipeline> ragPipeline;
    std::shared_ptr<RagQueryEngine> ragQueryEngine;
    std::shared_ptr<HybridRetrievalEngine> hybridRetrieval;
    std::shared_ptr<QueryTransformEngine> queryTransform;
    std::shared_ptr<RerankerEngine> reranker;
    std::shared_ptr<SemanticChunker> semanticChunker;

    // --- GraphRAG & Community ---
    std::shared_ptr<CommunityDetector> communityDetector;
    std::shared_ptr<GraphRagAgent> graphRagAgent;

    // --- LLM ---
    std::shared_ptr<LlmBackend> llmBackend;

    // --- Document processing ---
    std::shared_ptr<DocumentPreprocessor> documentPreprocessor;

    // --- Neuro-symbolic learning ---
    std::shared_ptr<NeuroSymbolicLearner> neuroSymbolicLearner;

    // --- Persistence ---
    std::shared_ptr<WalManager> walManager;
    std::shared_ptr<SnapshotManager> snapshotManager;

    // --- Recovery ---
    std::shared_ptr<RecoveryManager> recoveryManager;
    bool isReadOnly = false;

    // --- Validation & incremental reasoning ---
    std::shared_ptr<ShaclValidator> shaclValidator;
    std::shared_ptr<IncrementalReasoner> incrementalReasoner;

    // --- Explainability (owned uniquely by ServiceContext) ---
    std::unique_ptr<ExplainabilityEngine> explainabilityEngine;
};

using ServiceContextPtr = std::shared_ptr<ServiceContext>;

} // namespace ontology
