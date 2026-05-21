#pragma once

#include "Storage.hpp"
#include "Inference.hpp"
#include "Swrl.hpp"
#include "Sparql.hpp"
#include "TextEmbedding.hpp"
#include "EmbeddingService.hpp"
#include "RagStorage.hpp"
#include "RagPipeline.hpp"
#include "RagQuery.hpp"
#include "HybridRetrieval.hpp"
#include "QueryTransform.hpp"
#include "Reranker.hpp"
#include "SemanticChunker.hpp"
#include "CommunityDetection.hpp"
#include "GraphRagAgent.hpp"
#include "LlmBackend.hpp"
#include "DocumentPreprocessor.hpp"
#include "NeuroSymbolicLearning.hpp"
#include "Explainability.hpp"
#include "Persistence.hpp"
#include "ShaclValidation.hpp"
#include <memory>

namespace ontology {

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

    // --- Validation & incremental reasoning ---
    std::shared_ptr<ShaclValidator> shaclValidator;
    std::shared_ptr<IncrementalReasoner> incrementalReasoner;

    // --- Explainability (owned uniquely by ServiceContext) ---
    std::unique_ptr<ExplainabilityEngine> explainabilityEngine;
};

using ServiceContextPtr = std::shared_ptr<ServiceContext>;

} // namespace ontology
