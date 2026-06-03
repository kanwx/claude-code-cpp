#include <ontology/ApiHandler.hpp>
#include <ontology/Storage.hpp>
#include <ontology/storage/HybridStorage.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

static bool checkReadOnly(const std::shared_ptr<HybridStorage>& storage, httplib::Response& res) {
    if (storage && storage->isReadOnly()) {
        res.status = 503;
        res.set_content(R"({"error":"Service in read-only mode","reason":"graph database unavailable"})",
                        "application/json");
        return true;
    }
    return false;
}

class IndividualHandler : public ApiHandler {
public:
    String name() const override { return "IndividualHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Individual management ====================
        server.Get("/api/individuals", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            String classId = req.get_param_value("classId");
            String keyword = req.get_param_value("keyword");

            std::vector<Individual> individuals;
            if (!classId.empty()) {
                individuals = ctx_->storage->getIndividualsByClass(classId);
            } else {
                individuals = ctx_->storage->getAllIndividuals();
            }

            // Keyword filter
            if (!keyword.empty()) {
                std::vector<Individual> filtered;
                for (const auto& ind : individuals) {
                    if (ind.name.find(keyword) != String::npos ||
                        ind.id.find(keyword) != String::npos) {
                        filtered.push_back(ind);
                    }
                }
                individuals = std::move(filtered);
            }

            Json j = Json::array();
            for (const auto& ind : individuals) {
                j.push_back(individualToJson(ind));
            }
            jsonResponse(res, j);
        });

        server.Get("/api/individuals/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            auto ind = ctx_->storage->getIndividual(id);
            if (!ind) {
                errorResponse(res, 404, "Individual not found");
                return;
            }
            jsonResponse(res, individualToJson(*ind));
        });

        server.Post("/api/individuals", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }
            if (checkReadOnly(ctx_->storage, res)) return;

            try {
                Json body = Json::parse(req.body);
                Individual ind;
                ind.id = body.value("id", "");
                ind.name = body.value("name", "");
                ind.classId = body.value("classId", "");
                ind.importance = body.value("importance", 1.0f);

                // Parse properties
                if (body.contains("properties") && body["properties"].is_object()) {
                    for (auto it = body["properties"].begin(); it != body["properties"].end(); ++it) {
                        ind.properties[it.key()] = it.value();
                    }
                }

                if (body.contains("relations")) {
                    for (auto it = body["relations"].begin(); it != body["relations"].end(); ++it) {
                        std::vector<String> targets;
                        for (const auto& t : it.value()) {
                            targets.push_back(t.get<String>());
                        }
                        ind.relations[it.key()] = targets;
                    }
                }

                std::vector<float> embedding;
                if (body.contains("embedding")) {
                    for (const auto& v : body["embedding"]) {
                        embedding.push_back(v.get<float>());
                    }
                }

                if (ctx_->storage->addIndividual(ind)) {
                    // TODO: Re-add WebSocket push via ServiceContext event system

                    res.status = 201;
                    jsonResponse(res, individualToJson(ind));
                } else {
                    errorResponse(res, 400, "Failed to create individual");
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

        server.Put("/api/individuals/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }
            if (checkReadOnly(ctx_->storage, res)) return;

            String id = req.path_params.at("id");
            auto existing = ctx_->storage->getIndividual(id);
            if (!existing) {
                errorResponse(res, 404, "Individual not found");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                Individual ind = *existing;
                ind.name = body.value("name", ind.name);
                ind.classId = body.value("classId", ind.classId);
                ind.properties = body.value("properties", ind.properties);
                ind.importance = body.value("importance", ind.importance);

                std::vector<float> embedding;
                if (body.contains("embedding")) {
                    for (const auto& v : body["embedding"]) {
                        embedding.push_back(v.get<float>());
                    }
                }

                if (ctx_->storage->updateIndividual(ind)) {
                    jsonResponse(res, individualToJson(ind));
                } else {
                    errorResponse(res, 400, "Failed to update individual");
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

        server.Delete("/api/individuals/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }
            if (checkReadOnly(ctx_->storage, res)) return;

            String id = req.path_params.at("id");
            if (ctx_->storage->removeIndividual(id)) {
                res.status = 204;
            } else {
                errorResponse(res, 404, "Individual not found");
            }
        });

        // ==================== Batch individuals ====================
        server.Post("/api/batch/individuals", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }
            if (checkReadOnly(ctx_->storage, res)) return;

            try {
                Json body = Json::parse(req.body);
                std::vector<Individual> individuals;

                if (body.contains("individuals")) {
                    for (const auto& i : body["individuals"]) {
                        Individual ind;
                        ind.id = i.value("id", "");
                        ind.name = i.value("name", "");
                        ind.classId = i.value("classId", "");
                        if (i.contains("properties")) {
                            for (auto it = i["properties"].begin(); it != i["properties"].end(); ++it) {
                                ind.properties[it.key()] = it.value();
                            }
                        }
                        individuals.push_back(ind);
                    }
                }

                auto result = ctx_->storage->batchAddIndividuals(individuals);

                // TODO: Re-add WebSocket push via ServiceContext event system

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
    }
};

std::shared_ptr<ApiHandler> createIndividualHandler() {
    return std::make_shared<IndividualHandler>();
}

} // namespace ontology
