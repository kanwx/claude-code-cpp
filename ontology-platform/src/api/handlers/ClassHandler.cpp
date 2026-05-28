#include <ontology/ApiHandler.hpp>
#include <ontology/Storage.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

class ClassHandler : public ApiHandler {
public:
    String name() const override { return "ClassHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Class management ====================
        server.Get("/api/classes", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            auto classes = ctx_->storage->getAllClasses();
            Json j = Json::array();
            for (const auto& cls : classes) {
                j.push_back(classToJson(cls));
            }
            jsonResponse(res, j);
        });

        server.Get("/api/classes/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            auto cls = ctx_->storage->getClass(id);
            if (!cls) {
                errorResponse(res, 404, "Class not found");
                return;
            }
            jsonResponse(res, classToJson(*cls));
        });

        server.Post("/api/classes", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                Class cls;
                cls.id = body.value("id", "");
                cls.name = body.value("name", "");
                cls.description = body.value("description", "");
                cls.superClasses = body.value("superClasses", std::vector<String>{});
                cls.equivalentClasses = body.value("equivalentClasses", std::vector<String>{});
                cls.disjointClasses = body.value("disjointClasses", std::vector<String>{});
                cls.properties = body.value("properties", std::vector<String>{});

                if (ctx_->storage->addClass(cls)) {
                    // TODO: Re-add WebSocket push via ServiceContext event system

                    res.status = 201;
                    jsonResponse(res, classToJson(cls));
                } else {
                    errorResponse(res, 400, "Failed to create class");
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

        server.Put("/api/classes/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            auto existing = ctx_->storage->getClass(id);
            if (!existing) {
                errorResponse(res, 404, "Class not found");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                Class cls = *existing;
                cls.name = body.value("name", cls.name);
                cls.description = body.value("description", cls.description);
                cls.superClasses = body.value("superClasses", cls.superClasses);
                cls.equivalentClasses = body.value("equivalentClasses", cls.equivalentClasses);
                cls.disjointClasses = body.value("disjointClasses", cls.disjointClasses);

                if (ctx_->storage->updateClass(cls)) {
                    jsonResponse(res, classToJson(cls));
                } else {
                    errorResponse(res, 400, "Failed to update class");
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

        server.Delete("/api/classes/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            String id = req.path_params.at("id");
            if (ctx_->storage->removeClass(id)) {
                res.status = 204;
            } else {
                errorResponse(res, 404, "Class not found");
            }
        });

        // ==================== Relation management ====================
        server.Get("/api/relations", [this](const httplib::Request&, httplib::Response& res) {
            Json j = Json::array();
            if (ctx_->storage) {
                auto relations = ctx_->storage->getAllRelations();
                for (const auto& rel : relations) {
                    j.push_back(relationToJson(rel));
                }
            }
            jsonResponse(res, j);
        });

        server.Post("/api/relations", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                Relation rel;
                rel.id = body.value("id", "");
                rel.name = body.value("name", "");
                rel.description = body.value("description", "");
                rel.domain = body.value("domain", "");
                rel.range = body.value("range", "");
                rel.isFunctional = body.value("isFunctional", false);
                rel.isTransitive = body.value("isTransitive", false);
                rel.isSymmetric = body.value("isSymmetric", false);
                rel.isReflexive = body.value("isReflexive", false);

                if (ctx_->storage->addRelation(rel)) {
                    res.status = 201;
                    jsonResponse(res, relationToJson(rel));
                } else {
                    errorResponse(res, 400, "Failed to create relation");
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

        // ==================== Batch operations ====================
        server.Post("/api/batch/classes", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                std::vector<Class> classes;

                if (body.contains("classes")) {
                    for (const auto& c : body["classes"]) {
                        Class cls;
                        cls.id = c.value("id", "");
                        cls.name = c.value("name", "");
                        cls.description = c.value("description", "");
                        cls.superClasses = c.value("superClasses", std::vector<String>{});
                        cls.equivalentClasses = c.value("equivalentClasses", std::vector<String>{});
                        cls.disjointClasses = c.value("disjointClasses", std::vector<String>{});
                        cls.properties = c.value("properties", std::vector<String>{});
                        classes.push_back(cls);
                    }
                }

                auto result = ctx_->storage->batchAddClasses(classes);

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

std::shared_ptr<ApiHandler> createClassHandler() {
    return std::make_shared<ClassHandler>();
}

} // namespace ontology
