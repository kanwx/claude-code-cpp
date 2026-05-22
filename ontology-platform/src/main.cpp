#include <ontology/Api.hpp>
#include <ontology/Config.hpp>
#include "bootstrap/Bootstrap.hpp"
#include <iostream>
#include <iomanip>

using namespace ontology;

// Handler factory function declarations (defined in src/api/handlers/*.cpp)
namespace ontology {
std::shared_ptr<ApiHandler> createHealthHandler();
std::shared_ptr<ApiHandler> createClassHandler();
std::shared_ptr<ApiHandler> createIndividualHandler();
std::shared_ptr<ApiHandler> createTripleHandler();
std::shared_ptr<ApiHandler> createInferenceHandler();
std::shared_ptr<ApiHandler> createSparqlHandler();
std::shared_ptr<ApiHandler> createRuleHandler();
std::shared_ptr<ApiHandler> createRagHandler();
std::shared_ptr<ApiHandler> createShaclHandler();
std::shared_ptr<ApiHandler> createPersistenceHandler();
std::shared_ptr<ApiHandler> createExportHandler();
} // namespace ontology

// ============================================================================
// Helpers
// ============================================================================

void printBanner() {
    std::cout << R"(
╔════════════════════════════════════════════════════════════════╗
║                    Ontology Platform v1.0                       ║
║                 本体建模与混合推理平台                           ║
╚════════════════════════════════════════════════════════════════╝
)" << "\n";
}

void printEndpoints(int port, const OntologyConfig& config) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Server running at http://localhost:" << port << std::setw(23) << "║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "API Endpoints:\n";
    std::cout << "  GET    /api/health           Health check\n";
    std::cout << "  GET    /api/classes          List all classes\n";
    std::cout << "  POST   /api/classes          Create a class\n";
    std::cout << "  GET    /api/individuals      List all individuals\n";
    std::cout << "  POST   /api/individuals      Create an individual\n";
    std::cout << "  GET    /api/triples          List all triples\n";
    std::cout << "  POST   /api/triples          Add a triple\n";
    std::cout << "  POST   /api/triples/query    Query triples by pattern\n";
    std::cout << "  POST   /api/transitive       Transitive closure query\n";
    std::cout << "  POST   /api/path             Find shortest path\n";
    std::cout << "  GET    /api/rules            List SWRL rules\n";
    std::cout << "  POST   /api/rules            Add a SWRL rule\n";
    std::cout << "  POST   /api/rules/infer      Run rule inference\n";
    std::cout << "  GET    /api/sparql           SPARQL query (via param)\n";
    std::cout << "  POST   /api/sparql           SPARQL query (via body)\n";
    std::cout << "  POST   /api/infer            Hybrid inference\n";
    std::cout << "  POST   /api/search           Vector similarity search\n";
    std::cout << "  GET    /api/export           Export ontology\n";
    std::cout << "  POST   /api/import           Import ontology\n";
    std::cout << "  POST   /api/explain          Explain inference (with tracing)\n";
    std::cout << "  GET    /api/explain/traces   List inference traces\n";
    std::cout << "  GET    /api/explain/stats    Explainability statistics\n";
    std::cout << "  POST   /api/learn/rules      Discover rules from embeddings\n";
    std::cout << "  POST   /api/learn/train      Constrained training\n";

    if (config.rag.enabled) {
        std::cout << "\n  RAG Endpoints (HttpRagClient compatible):\n";
        std::cout << "  GET    /health               RAG health check\n";
        std::cout << "  POST   /search               Search documents\n";
        std::cout << "  POST   /documents            Add document\n";
        std::cout << "  GET    /documents/:id         Get document\n";
        std::cout << "  DELETE /documents/:id         Delete document\n";
        std::cout << "  GET    /knowledge-bases       List knowledge bases\n";
        std::cout << "  POST   /rag/query            Enhanced query (graph+entity)\n";
        std::cout << "  POST   /rag/knowledge-bases  Create knowledge base\n";
        std::cout << "  GET    /rag/stats            RAG statistics\n";
        std::cout << "\n  Advanced RAG v2 Endpoints:\n";
        std::cout << "  POST   /rag/hybrid           Hybrid retrieval (BM25+Vector+Graph)\n";
        std::cout << "  POST   /rag/transform        Query transformation (HyDE/Expand/Decompose)\n";
        std::cout << "  POST   /rag/rerank           Re-rank results (Cross-encoder + MMR)\n";
        std::cout << "  POST   /rag/communities      Community detection (GraphRAG)\n";
        std::cout << "  POST   /rag/stream           Streaming answer (SSE)\n";
        std::cout << "\n  File Ingestion Endpoints:\n";
        std::cout << "  POST   /rag/ingest/text      Ingest raw text\n";
        std::cout << "  POST   /rag/ingest/file      Upload file (multipart)\n";
        std::cout << "  POST   /rag/ingest/batch     Batch file upload\n";
        std::cout << "  GET    /rag/status           Check backend status\n";
    }

    if (config.validation.enableShacl) {
        std::cout << "\n  Validation Endpoints:\n";
        std::cout << "  POST   /shacl/shapes         Add SHACL shapes\n";
        std::cout << "  POST   /shacl/validate       Validate ontology against shapes\n";
        std::cout << "  GET    /shacl/shapes         List SHACL shapes\n";
    }

    if (config.persistence.enableWal) {
        std::cout << "\n  Persistence Endpoints:\n";
        std::cout << "  POST   /persistence/snapshot Create snapshot\n";
        std::cout << "  POST   /persistence/restore  Restore from snapshot\n";
        std::cout << "  GET    /persistence/snapshots List snapshots\n";
        std::cout << "  GET    /persistence/stats    Persistence statistics\n";
    }

    std::cout << "\n";
    std::cout << "Press Ctrl+C to stop...\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    printBanner();

    // Load configuration
    String configPath = "config.json";
    for (int i = 1; i < argc; ++i) {
        String arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        }
    }

    OntologyConfig config = OntologyConfig::load(configPath);

    // Validate
    auto errors = config.validate();
    if (!errors.empty()) {
        std::cerr << "Configuration errors:\n";
        for (const auto& err : errors) {
            std::cerr << "  - " << err << "\n";
        }
        return 1;
    }

    std::cout << "Starting Ontology Platform v2.0...\n";
    std::cout << "  Port: " << config.server.port << "\n";

    // Bootstrap all services
    auto ctx = Bootstrap::initialize(config);

    // Create HTTP server
    HttpServer::Config serverConfig;
    serverConfig.port = config.server.port;
    HttpServer server(serverConfig);

    // Set shared service context
    server.setContext(ctx);

    // Register all route handler modules
    server.addHandler(createHealthHandler());
    server.addHandler(createClassHandler());
    server.addHandler(createIndividualHandler());
    server.addHandler(createTripleHandler());
    server.addHandler(createInferenceHandler());
    server.addHandler(createSparqlHandler());
    server.addHandler(createRuleHandler());
    server.addHandler(createRagHandler());
    server.addHandler(createShaclHandler());
    server.addHandler(createPersistenceHandler());
    server.addHandler(createExportHandler());

    // Print endpoint listing
    printEndpoints(config.server.port, config);

    // Run (blocking)
    server.start();

    // Cleanup
    if (ctx->snapshotManager) ctx->snapshotManager->stopAutoSnapshot();

    return 0;
}
