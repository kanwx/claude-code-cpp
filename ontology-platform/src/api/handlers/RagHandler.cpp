#include <ontology/ApiHandler.hpp>

namespace ontology {

class RagHandler : public ApiHandler {
public:
    String name() const override { return "RagHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Health checks ====================
        server.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
            Json j;
            j["status"] = "ok";
            jsonResponse(res, j);
        });

        server.Get("/rag/health", [this](const httplib::Request&, httplib::Response& res) {
            Json j;
            j["status"] = "ok";
            if (ctx_->ragStorage) {
                j["knowledgeBases"] = ctx_->ragStorage->knowledgeBaseCount();
                j["documents"] = ctx_->ragStorage->documentCount();
                j["chunks"] = ctx_->ragStorage->chunkCount();
            }
            jsonResponse(res, j);
        });

        // ==================== Knowledge base management ====================
        // HttpRagClient compat: GET /knowledge-bases
        server.Get("/knowledge-bases", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            auto kbs = ctx_->ragStorage->listKnowledgeBases();
            Json j;
            j["knowledge_bases"] = Json::array();
            for (const auto& kb : kbs) {
                j["knowledge_bases"].push_back(kb.toJson());
            }
            jsonResponse(res, j);
        });

        server.Get("/rag/knowledge-bases", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            auto kbs = ctx_->ragStorage->listKnowledgeBases();
            Json j = Json::array();
            for (const auto& kb : kbs) {
                j.push_back(kb.toJson());
            }
            jsonResponse(res, j);
        });

        server.Post("/rag/knowledge-bases", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                KnowledgeBase kb;
                kb.id = body.value("id", "");
                kb.name = body.value("name", "");
                kb.description = body.value("description", "");
                if (body.contains("tags") && body["tags"].is_array()) {
                    for (const auto& t : body["tags"]) {
                        kb.tags.push_back(t.get<String>());
                    }
                }

                String id = ctx_->ragStorage->createKnowledgeBase(kb);
                auto created = ctx_->ragStorage->getKnowledgeBase(id);
                res.status = 201;
                jsonResponse(res, created ? created->toJson() : Json::object());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        server.Get("/rag/knowledge-bases/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            auto kb = ctx_->ragStorage->getKnowledgeBase(id);
            if (!kb) {
                errorResponse(res, 404, "Knowledge base not found");
                return;
            }
            jsonResponse(res, kb->toJson());
        });

        server.Delete("/rag/knowledge-bases/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            if (ctx_->ragStorage->deleteKnowledgeBase(id)) {
                Json j;
                j["success"] = true;
                jsonResponse(res, j);
            } else {
                errorResponse(res, 404, "Knowledge base not found");
            }
        });

        // ==================== Document management ====================
        // HttpRagClient compat: POST /documents
        server.Post("/documents", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragPipeline) {
                errorResponse(res, 500, "RAG pipeline not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String id = body.value("id", "");
                String title = body.value("title", "");
                String content = body.value("content", "");
                String source = body.value("source", "");
                std::vector<String> tags;
                if (body.contains("tags") && body["tags"].is_array()) {
                    for (const auto& t : body["tags"]) {
                        if (t.is_string()) tags.push_back(t.get<String>());
                    }
                }
                Json metadata = body.value("metadata", Json::object());

                auto result = ctx_->ragPipeline->ingest(source, title, content, source, tags, metadata);

                Json j;
                j["success"] = true;
                j["documentId"] = result.documentId;
                j["chunkCount"] = result.chunkCount;
                j["entityCount"] = result.entityCount;
                j["tripleCount"] = result.tripleCount;
                if (!result.warnings.empty()) {
                    j["warnings"] = result.warnings;
                }
                res.status = 201;
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        server.Post("/rag/documents", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragPipeline) {
                errorResponse(res, 500, "RAG pipeline not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String knowledgeBaseId = body.value("knowledgeBaseId", "");
                String title = body.value("title", "");
                String content = body.value("content", "");
                String source = body.value("source", "");
                std::vector<String> tags;
                if (body.contains("tags") && body["tags"].is_array()) {
                    for (const auto& t : body["tags"]) {
                        if (t.is_string()) tags.push_back(t.get<String>());
                    }
                }
                Json metadata = body.value("metadata", Json::object());

                auto result = ctx_->ragPipeline->ingest(knowledgeBaseId, title, content, source, tags, metadata);
                res.status = 201;
                jsonResponse(res, result.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // HttpRagClient compat: GET /documents/:id
        server.Get("/documents/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            auto doc = ctx_->ragStorage->getDocument(id);
            if (!doc) {
                errorResponse(res, 404, "Document not found");
                return;
            }
            Json j;
            j["document"] = doc->toJson();
            jsonResponse(res, j);
        });

        server.Get("/rag/documents/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            auto doc = ctx_->ragStorage->getDocument(id);
            if (!doc) {
                errorResponse(res, 404, "Document not found");
                return;
            }

            Json j = doc->toJson();
            auto chunks = ctx_->ragStorage->getChunksByDocument(id);
            Json chunksArr = Json::array();
            for (const auto& c : chunks) {
                chunksArr.push_back(c.toJson());
            }
            j["chunks"] = chunksArr;
            jsonResponse(res, j);
        });

        server.Get("/rag/documents", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String kbId = req.get_param_value("knowledgeBaseId");
            auto docs = ctx_->ragStorage->listDocuments(kbId);
            Json j = Json::array();
            for (const auto& doc : docs) {
                j.push_back(doc.toJson());
            }
            jsonResponse(res, j);
        });

        // HttpRagClient compat: DELETE /documents/:id
        server.Delete("/documents/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            if (ctx_->ragStorage->deleteDocument(id)) {
                Json j;
                j["success"] = true;
                jsonResponse(res, j);
            } else {
                errorResponse(res, 404, "Document not found");
            }
        });

        server.Delete("/rag/documents/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            if (ctx_->ragStorage->deleteDocument(id)) {
                Json j;
                j["success"] = true;
                jsonResponse(res, j);
            } else {
                errorResponse(res, 404, "Document not found");
            }
        });

        // ==================== Search ====================
        // HttpRagClient compat: POST /search
        server.Post("/search", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragQueryEngine) {
                errorResponse(res, 500, "RAG query engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                int topK = body.value("top_k", 5);
                float minScore = body.value("min_score", 0.0f);
                std::vector<String> knowledgeBases;
                if (body.contains("knowledge_bases") && body["knowledge_bases"].is_array()) {
                    for (const auto& kb : body["knowledge_bases"]) {
                        if (kb.is_string()) knowledgeBases.push_back(kb.get<String>());
                    }
                }
                int contentMaxLen = body.value("content_max_len", 1000);

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                auto chunks = ctx_->ragQueryEngine->search(query, topK, knowledgeBases, minScore);

                Json results = Json::array();
                for (const auto& chunk : chunks) {
                    Json doc;
                    doc["id"] = chunk.documentId;
                    doc["title"] = chunk.documentId;
                    String text = chunk.text;
                    if (static_cast<int>(text.size()) > contentMaxLen) {
                        text = text.substr(0, contentMaxLen) + "...";
                    }
                    doc["content"] = text;
                    doc["source"] = chunk.knowledgeBaseId;
                    doc["score"] = chunk.score;
                    doc["tags"] = Json::array();
                    doc["metadata"] = Json::object();
                    doc["metadata"]["chunkId"] = chunk.id;
                    doc["metadata"]["chunkIndex"] = chunk.chunkIndex;
                    results.push_back(doc);
                }

                Json j;
                j["results"] = results;
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // Enhanced search with graph+entity fusion
        server.Post("/rag/search", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragQueryEngine) {
                errorResponse(res, 500, "RAG query engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                int topK = body.value("top_k", 10);
                float minScore = body.value("min_score", 0.0f);
                std::vector<String> knowledgeBases;
                if (body.contains("knowledge_bases") && body["knowledge_bases"].is_array()) {
                    for (const auto& kb : body["knowledge_bases"]) {
                        if (kb.is_string()) knowledgeBases.push_back(kb.get<String>());
                    }
                }

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                auto chunks = ctx_->ragQueryEngine->search(query, topK, knowledgeBases, minScore);

                Json j = Json::array();
                for (const auto& chunk : chunks) {
                    j.push_back(chunk.toJson());
                }
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== Enhanced query ====================
        server.Post("/rag/query", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragQueryEngine) {
                errorResponse(res, 500, "RAG query engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                int topK = body.value("top_k", 0);
                std::vector<String> knowledgeBases;
                if (body.contains("knowledge_bases") && body["knowledge_bases"].is_array()) {
                    for (const auto& kb : body["knowledge_bases"]) {
                        if (kb.is_string()) knowledgeBases.push_back(kb.get<String>());
                    }
                }

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                auto result = ctx_->ragQueryEngine->query(query, knowledgeBases, topK);
                jsonResponse(res, result.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== RAG stats ====================
        server.Get("/rag/stats", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->ragStorage) {
                errorResponse(res, 500, "RAG storage not initialized");
                return;
            }

            Json j;
            j["knowledgeBases"] = ctx_->ragStorage->knowledgeBaseCount();
            j["documents"] = ctx_->ragStorage->documentCount();
            j["chunks"] = ctx_->ragStorage->chunkCount();
            jsonResponse(res, j);
        });

        // ==================== Hybrid retrieval ====================
        server.Post("/rag/hybrid", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->hybridRetrieval) {
                errorResponse(res, 500, "Hybrid retrieval engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                int topK = body.value("top_k", 10);

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                auto results = ctx_->hybridRetrieval->retrieve(query, {}, topK);
                Json j = Json::array();
                for (const auto& r : results) {
                    j.push_back(r.toJson());
                }
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== Streaming query (SSE) ====================
        server.Post("/rag/stream", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragQueryEngine) {
                errorResponse(res, 500, "RAG query engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                int topK = body.value("top_k", 5);

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                // SSE response
                res.set_header("Content-Type", "text/event-stream");
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Connection", "keep-alive");

                // Execute query
                auto result = ctx_->ragQueryEngine->query(query, {}, topK);

                // Send staged SSE events
                // 1. Retrieval stage
                Json retrievalEvent;
                retrievalEvent["type"] = "retrieval";
                retrievalEvent["numChunks"] = result.chunks.size();
                res.body += "event: retrieval\ndata: " + retrievalEvent.dump() + "\n\n";

                // 2. Each document chunk
                for (size_t i = 0; i < result.chunks.size(); i++) {
                    Json chunkEvent;
                    chunkEvent["type"] = "chunk";
                    chunkEvent["index"] = i;
                    chunkEvent["chunk"] = result.chunks[i].toJson();
                    res.body += "event: chunk\ndata: " + chunkEvent.dump() + "\n\n";
                }

                // 3. Final answer
                Json answerEvent;
                answerEvent["type"] = "answer";
                answerEvent["answer"] = result.answer;
                answerEvent["confidence"] = result.confidence;
                res.body += "event: answer\ndata: " + answerEvent.dump() + "\n\n";

                // 4. Done
                res.body += "event: done\ndata: {}\n\n";
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== Community detection & search ====================
        server.Post("/rag/communities", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->communityDetector) {
                errorResponse(res, 500, "Community detector not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String action = body.value("action", "search");

                if (action == "detect") {
                    auto communities = ctx_->communityDetector->detectCommunities();
                    Json j = Json::array();
                    for (const auto& c : communities) {
                        j.push_back(c.toJson());
                    }
                    jsonResponse(res, j);
                } else {
                    // search
                    String query = body.value("query", "");
                    int topK = body.value("top_k", 5);

                    if (query.empty()) {
                        errorResponse(res, 400, "Query is required for search");
                        return;
                    }

                    auto communities = ctx_->communityDetector->getRelevantCommunities(query, topK);
                    Json j = Json::array();
                    for (const auto& c : communities) {
                        j.push_back(c.toJson());
                    }
                    jsonResponse(res, j);
                }
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== Query transform ====================
        server.Post("/rag/transform", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->queryTransform) {
                errorResponse(res, 500, "Query transform engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                String method = body.value("method", "expand");

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                auto transformed = ctx_->queryTransform->transform(query);
                Json j;
                j["original"] = transformed.originalQuery;
                j["method"] = method;
                Json queriesArr = Json::array();
                for (const auto& q : transformed.expandedQueries) {
                    queriesArr.push_back(q.text);
                }
                j["queries"] = queriesArr;
                j["expanded"] = true;
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== Reranking ====================
        server.Post("/rag/rerank", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->reranker) {
                errorResponse(res, 500, "Reranker engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                // Parse input results
                std::vector<HybridRetrievalEngine::RetrievalResult> inputResults;
                if (body.contains("results") && body["results"].is_array()) {
                    for (const auto& r : body["results"]) {
                        HybridRetrievalEngine::RetrievalResult result;
                        result.id = r.value("id", "");
                        result.text = r.value("text", "");
                        result.documentId = r.value("document_id", "");
                        result.knowledgeBaseId = r.value("knowledge_base_id", "");
                        result.fusedScore = r.value("score", 0.0f);
                        inputResults.push_back(result);
                    }
                }

                auto reranked = ctx_->reranker->rerank(query, inputResults);
                Json j = Json::array();
                for (const auto& r : reranked) {
                    j.push_back(r.toJson());
                }
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== GraphRAG Agent query ====================
        server.Post("/rag/agent", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->graphRagAgent) {
                errorResponse(res, 500, "GraphRAG agent not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                int maxSteps = body.value("max_steps", 10);

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                std::vector<String> kbIds;
                if (body.contains("knowledge_bases") && body["knowledge_bases"].is_array()) {
                    for (const auto& kb : body["knowledge_bases"]) {
                        if (kb.is_string()) kbIds.push_back(kb.get<String>());
                    }
                }

                auto result = ctx_->graphRagAgent->query(query, kbIds);
                jsonResponse(res, result.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        // ==================== File ingestion ====================
        server.Post("/rag/ingest/text", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragPipeline) {
                errorResponse(res, 500, "RAG pipeline not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String text = body.value("text", "");
                String kbId = body.value("knowledge_base_id", "default");
                String title = body.value("title", "");
                String source = body.value("source", "api");

                if (text.empty()) {
                    errorResponse(res, 400, "Text is required");
                    return;
                }

                auto result = ctx_->ragPipeline->ingest(kbId, title, text, source);
                jsonResponse(res, result.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        server.Post("/rag/ingest/file", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragPipeline) {
                errorResponse(res, 500, "RAG pipeline not initialized");
                return;
            }

            // multipart/form-data file upload
            if (!req.form.has_file("file")) {
                errorResponse(res, 400, "No file uploaded. Use multipart/form-data with 'file' field.");
                return;
            }

            const auto& file = req.form.get_file("file");
            String kbId = "default";
            if (req.form.has_file("knowledge_base_id")) {
                kbId = req.form.get_file("knowledge_base_id").content;
            }
            String title = file.filename;
            if (req.form.has_file("title")) {
                title = req.form.get_file("title").content;
            }
            String source = "file_upload";
            if (req.form.has_file("source")) {
                source = req.form.get_file("source").content;
            }

            auto result = ctx_->ragPipeline->ingestBuffer(
                file.content, file.content_type, file.filename,
                kbId, title, source
            );
            jsonResponse(res, result.toJson());
        });

        server.Post("/rag/ingest/batch", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->ragPipeline) {
                errorResponse(res, 500, "RAG pipeline not initialized");
                return;
            }

            Json results = Json::array();
            // Process all files with key "file"
            auto allFiles = req.form.get_files("file");
            if (allFiles.empty()) {
                errorResponse(res, 400, "No files uploaded");
                return;
            }

            String kbId = "default";
            if (req.form.has_file("knowledge_base_id")) {
                kbId = req.form.get_file("knowledge_base_id").content;
            }

            for (const auto& file : allFiles) {
                auto result = ctx_->ragPipeline->ingestBuffer(
                    file.content, file.content_type, file.filename,
                    kbId, file.filename, "batch_upload"
                );
                results.push_back(result.toJson());
            }

            jsonResponse(res, {{"results", results}, {"count", results.size()}});
        });

        // ==================== RAG status ====================
        server.Get("/rag/status", [this](const httplib::Request&, httplib::Response& res) {
            Json j;
            j["status"] = "ok";

            // Check embedding service
            if (ctx_->embeddingService) {
                j["embedding"] = ctx_->embeddingService->getStats();
                j["embedding"]["available"] = ctx_->embeddingService->isAvailable();
            } else if (ctx_->textEmbedder) {
                j["embedding"] = {
                    {"available", ctx_->textEmbedder->isExternalAvailable()},
                    {"dimension", ctx_->textEmbedder->dimension()}
                };
            } else {
                j["embedding"] = {{"available", false}};
            }

            // Check LLM backend
            if (ctx_->llmBackend) {
                j["llm"] = {
                    {"available", ctx_->llmBackend->isAvailable()},
                    {"name", ctx_->llmBackend->name()},
                    {"model", ctx_->llmBackend->modelName()}
                };
            } else {
                j["llm"] = {{"available", false}};
            }

            // Check RAG storage
            if (ctx_->ragStorage) {
                j["ragStorage"] = {
                    {"available", true},
                    {"knowledgeBases", ctx_->ragStorage->knowledgeBaseCount()},
                    {"documents", ctx_->ragStorage->documentCount()},
                    {"chunks", ctx_->ragStorage->chunkCount()}
                };
            }

            // Check vector DB via storage
            if (ctx_->storage) {
                j["vectorDb"] = {{"available", true}};
            }

            jsonResponse(res, j);
        });
    }
};

std::shared_ptr<ApiHandler> createRagHandler() {
    return std::make_shared<RagHandler>();
}

} // namespace ontology
