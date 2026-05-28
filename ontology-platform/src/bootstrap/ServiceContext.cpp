#include <ontology/ServiceContext.hpp>

// Include all headers for complete type definitions needed by unique_ptr destructor
#include <ontology/Storage.hpp>
#include <ontology/Inference.hpp>
#include <ontology/Swrl.hpp>
#include <ontology/Sparql.hpp>
#include <ontology/TextEmbedding.hpp>
#include <ontology/EmbeddingService.hpp>
#include <ontology/RagStorage.hpp>
#include <ontology/RagPipeline.hpp>
#include <ontology/RagQuery.hpp>
#include <ontology/HybridRetrieval.hpp>
#include <ontology/QueryTransform.hpp>
#include <ontology/Reranker.hpp>
#include <ontology/SemanticChunker.hpp>
#include <ontology/CommunityDetection.hpp>
#include <ontology/GraphRagAgent.hpp>
#include <ontology/LlmBackend.hpp>
#include <ontology/DocumentPreprocessor.hpp>
#include <ontology/NeuroSymbolicLearning.hpp>
#include <ontology/Explainability.hpp>
#include <ontology/Persistence.hpp>
#include <ontology/ShaclValidation.hpp>

namespace ontology {
ServiceContext::~ServiceContext() = default;
} // namespace ontology
