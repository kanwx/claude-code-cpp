#include <ontology/ApiHandler.hpp>
#include <ontology/Storage.hpp>
#include <ontology/storage/TripleStore.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

class TripleHandler : public ApiHandler {
public:
    String name() const override { return "TripleHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Triple management ====================
        server.Get("/api/triples", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            auto triples = ctx_->storage->getAllTriples();
            Json j = Json::array();
            for (const auto& t : triples) {
                Json triple;
                triple["subject"] = t.subject;
                triple["predicate"] = t.predicate;
                triple["object"] = t.object;
                triple["confidence"] = t.confidence;
                triple["source"] = t.source;
                j.push_back(triple);
            }
            jsonResponse(res, j);
        });

        server.Post("/api/triples", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                Triple t;
                t.subject = body.value("subject", "");
                t.predicate = body.value("predicate", "");
                t.object = body.value("object", "");
                t.confidence = body.value("confidence", 1.0f);
                t.source = body.value("source", "");

                if (ctx_->storage->addTriple(t)) {
                    // TODO: Re-add WebSocket push via ServiceContext event system

                    res.status = 201;
                    Json result;
                    result["subject"] = t.subject;
                    result["predicate"] = t.predicate;
                    result["object"] = t.object;
                    jsonResponse(res, result);
                } else {
                    errorResponse(res, 400, "Failed to add triple");
                }
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        server.Post("/api/triples/query", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                TripleStore::TriplePattern pattern;
                pattern.subject = body.value("subject", "");
                pattern.predicate = body.value("predicate", "");
                pattern.object = body.value("object", "");
                // Auto-detect: empty field = variable, unless explicitly specified
                pattern.subjectIsVar = body.contains("subjectIsVar") ? body["subjectIsVar"].get<bool>() : pattern.subject.empty();
                pattern.predicateIsVar = body.contains("predicateIsVar") ? body["predicateIsVar"].get<bool>() : pattern.predicate.empty();
                pattern.objectIsVar = body.contains("objectIsVar") ? body["objectIsVar"].get<bool>() : pattern.object.empty();

                auto triples = ctx_->storage->queryTriples(pattern);
                Json j = Json::array();
                for (const auto& t : triples) {
                    Json triple;
                    triple["subject"] = t.subject;
                    triple["predicate"] = t.predicate;
                    triple["object"] = t.object;
                    triple["confidence"] = t.confidence;
                    j.push_back(triple);
                }
                jsonResponse(res, j);
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        // ==================== Batch triples ====================
        server.Post("/api/batch/triples", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String operation = body.value("operation", "add"); // add or remove
                std::vector<Triple> triples;

                if (body.contains("triples")) {
                    for (const auto& t : body["triples"]) {
                        Triple tr;
                        tr.subject = t.value("subject", "");
                        tr.predicate = t.value("predicate", "");
                        tr.object = t.value("object", "");
                        tr.confidence = t.value("confidence", 1.0f);
                        tr.source = t.value("source", "batch");
                        triples.push_back(tr);
                    }
                }

                HybridStorage::BatchResult result;
                if (operation == "remove") {
                    result = ctx_->storage->batchRemoveTriples(triples);
                } else {
                    result = ctx_->storage->batchAddTriples(triples);
                    // TODO: Re-add WebSocket push via ServiceContext event system
                }

                Json j;
                j["succeeded"] = result.succeeded;
                j["failed"] = result.failed;
                j["errors"] = result.errors;
                jsonResponse(res, j);
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        // ==================== Transitive query ====================
        server.Post("/api/transitive", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String subject = body.value("subject", "");
                String predicate = body.value("predicate", "");
                int maxDepth = body.value("maxDepth", 10);

                if (subject.empty() || predicate.empty()) {
                    errorResponse(res, 400, "Both 'subject' and 'predicate' are required");
                    return;
                }

                // BFS to find all transitively related entities
                std::vector<String> visited;
                std::vector<Triple> results;
                std::vector<String> queue = {subject};
                std::unordered_set<String> seen = {subject};

                while (!queue.empty() && (int)visited.size() < maxDepth) {
                    std::vector<String> nextQueue;
                    for (const auto& current : queue) {
                        visited.push_back(current);
                        auto triples = ctx_->storage->queryTriples(
                            TripleStore::TriplePattern{current, predicate, "", false, false, true});
                        for (const auto& t : triples) {
                            results.push_back(t);
                            if (seen.find(t.object) == seen.end()) {
                                seen.insert(t.object);
                                nextQueue.push_back(t.object);
                            }
                        }
                    }
                    queue = std::move(nextQueue);
                }

                Json j;
                j["visited"] = visited;
                j["facts"] = Json::array();
                for (const auto& t : results) {
                    Json fact;
                    fact["subject"] = t.subject;
                    fact["predicate"] = t.predicate;
                    fact["object"] = t.object;
                    fact["confidence"] = t.confidence;
                    j["facts"].push_back(fact);
                }
                jsonResponse(res, j);
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        // ==================== Path query ====================
        server.Post("/api/path", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String from = body.value("from", "");
                String to = body.value("to", "");
                String predicate = body.value("predicate", "");
                int maxDepth = body.value("maxDepth", 5);

                if (from.empty() || to.empty()) {
                    errorResponse(res, 400, "Both 'from' and 'to' are required");
                    return;
                }

                auto paths = ctx_->storage->findPath(from, to, predicate, maxDepth);
                Json j = Json::array();
                for (const auto& path : paths) {
                    j.push_back(path);
                }
                jsonResponse(res, j);
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });
    }
};

std::shared_ptr<ApiHandler> createTripleHandler() {
    return std::make_shared<TripleHandler>();
}

} // namespace ontology
