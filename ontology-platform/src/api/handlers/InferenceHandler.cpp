#include <ontology/ApiHandler.hpp>

namespace ontology {

class InferenceHandler : public ApiHandler {
public:
    String name() const override { return "InferenceHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Inference ====================
        server.Post("/api/infer", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->hybridReasoner) {
                errorResponse(res, 500, "Reasoner not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                bool enableSymbolic = body.value("enableSymbolic", true);
                bool enableNeural = body.value("enableNeural", true);

                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                HybridReasoner::HybridResult result = ctx_->hybridReasoner->infer(query);
                Json j;
                j["explanation"] = result.explanation;
                j["facts"] = Json::array();
                for (const auto& t : result.combined) {
                    Json fact;
                    fact["subject"] = t.subject;
                    fact["predicate"] = t.predicate;
                    fact["object"] = t.object;
                    fact["confidence"] = t.confidence;
                    j["facts"].push_back(fact);
                }
                jsonResponse(res, j);
            } catch (...) {
                errorResponse(res, 400, "Invalid JSON");
            }
        });

        // ==================== Vector search ====================
        server.Post("/api/search", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->neuralReasoner) {
                errorResponse(res, 500, "Neural reasoner not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String entityId = body.value("entityId", "");
                int topK = body.value("topK", 10);
                String filterClass = body.value("classId", "");

                std::vector<std::pair<String, float>> results;
                if (!entityId.empty()) {
                    results = ctx_->neuralReasoner->findSimilar(entityId, topK, filterClass);
                }

                Json j = Json::array();
                for (const auto& [id, score] : results) {
                    auto ind = ctx_->storage->getIndividual(id);
                    if (ind) {
                        Json item;
                        item["id"] = id;
                        item["name"] = ind->name;
                        item["classId"] = ind->classId;
                        item["score"] = score;
                        j.push_back(item);
                    }
                }
                jsonResponse(res, j);
            } catch (...) {
                errorResponse(res, 400, "Invalid JSON");
            }
        });

        // ==================== Explainability routes ====================
        server.Post("/api/explain", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->explainabilityEngine) {
                errorResponse(res, 500, "Explainability engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");

                // Run inference with tracing, then explain
                if (ctx_->hybridReasoner) {
                    auto result = ctx_->hybridReasoner->infer(query);
                    if (!result.traceId.empty()) {
                        auto trace = ctx_->explainabilityEngine->getTrace(result.traceId);
                        if (trace) {
                            auto explanation = ctx_->explainabilityEngine->explain(*trace);
                            jsonResponse(res, explanation.toJson());
                            return;
                        }
                    }
                }

                // Fallback: explain last trace
                auto explanation = ctx_->explainabilityEngine->explainLast();
                jsonResponse(res, explanation.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        server.Get("/api/explain/traces", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->explainabilityEngine) {
                errorResponse(res, 500, "Explainability engine not initialized");
                return;
            }

            auto traces = ctx_->explainabilityEngine->getAllTraces();
            Json arr = Json::array();
            for (const auto& t : traces) {
                arr.push_back(t.toJson());
            }
            jsonResponse(res, arr);
        });

        server.Get("/api/explain/stats", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->explainabilityEngine) {
                errorResponse(res, 500, "Explainability engine not initialized");
                return;
            }
            jsonResponse(res, ctx_->explainabilityEngine->getStats());
        });

        // ==================== Neuro-Symbolic learning ====================
        server.Post("/api/learn/rules", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->neuroSymbolicLearner) {
                errorResponse(res, 500, "Neuro-symbolic learner not initialized");
                return;
            }

            try {
                auto rules = ctx_->neuroSymbolicLearner->discoverRules();
                Json j;
                j["numRules"] = rules.size();
                Json rulesJson = Json::array();
                for (const auto& r : rules) {
                    rulesJson.push_back(r.toJson());
                }
                j["rules"] = rulesJson;
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Error: ") + e.what());
            }
        });

        server.Post("/api/learn/train", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->neuroSymbolicLearner) {
                errorResponse(res, 500, "Neuro-symbolic learner not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                int epochs = body.value("epochs", 100);
                float lr = body.value("learning_rate", 0.01f);

                auto rules = ctx_->neuroSymbolicLearner->getDiscoveredRules();
                std::vector<Triple> triples;
                if (ctx_->storage) {
                    auto individuals = ctx_->storage->getAllIndividuals();
                    for (const auto& ind : individuals) {
                        auto related = ctx_->storage->queryTriples(
                            TripleStore::TriplePattern{ind.id, "", "", false, true, true}
                        );
                        triples.insert(triples.end(), related.begin(), related.end());
                    }
                }

                ctx_->neuroSymbolicLearner->constrainedTraining(rules, triples, epochs, lr);

                Json j;
                j["status"] = "ok";
                j["epochs"] = epochs;
                j["numRules"] = rules.size();
                j["numTriples"] = triples.size();
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });
    }
};

std::shared_ptr<ApiHandler> createInferenceHandler() {
    return std::make_shared<InferenceHandler>();
}

} // namespace ontology
