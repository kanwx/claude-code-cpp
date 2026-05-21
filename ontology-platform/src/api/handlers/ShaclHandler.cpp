#include <ontology/ApiHandler.hpp>
#include <ontology/ShaclValidation.hpp>

namespace ontology {

class ShaclHandler : public ApiHandler {
public:
    String name() const override { return "ShaclHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== SHACL shape management ====================
        server.Get("/shacl/shapes", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->shaclValidator) {
                errorResponse(res, 500, "SHACL validator not initialized");
                return;
            }

            auto shapes = ctx_->shaclValidator->getShapes();
            Json j = Json::array();
            for (const auto& shape : shapes) {
                j.push_back(shape.toJson());
            }
            jsonResponse(res, j);
        });

        server.Post("/shacl/shapes", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->shaclValidator) {
                errorResponse(res, 500, "SHACL validator not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                ShaclNodeShape shape = ShaclNodeShape::fromJson(body);
                ctx_->shaclValidator->addShape(shape);
                res.status = 201;
                jsonResponse(res, shape.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        server.Delete("/shacl/shapes/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->shaclValidator) {
                errorResponse(res, 500, "SHACL validator not initialized");
                return;
            }

            String id = req.path_params.at("id");
            ctx_->shaclValidator->removeShape(id);
            res.status = 204;
        });

        // ==================== SHACL validation ====================
        server.Post("/shacl/validate", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->shaclValidator) {
                errorResponse(res, 500, "SHACL validator not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);

                // Determine validation scope
                String scope = body.value("scope", "all"); // all, node, class, incremental
                String targetId = body.value("targetId", "");

                ShaclValidationReport report;

                if (scope == "node" && !targetId.empty()) {
                    report = ctx_->shaclValidator->validateNode(targetId);
                } else if (scope == "class" && !targetId.empty()) {
                    report = ctx_->shaclValidator->validateClass(targetId);
                } else if (scope == "incremental") {
                    // Incremental validation with change set
                    std::vector<Triple> added, removed;
                    if (body.contains("added") && body["added"].is_array()) {
                        for (const auto& t : body["added"]) {
                            Triple tr;
                            tr.subject = t.value("subject", "");
                            tr.predicate = t.value("predicate", "");
                            tr.object = t.value("object", "");
                            added.push_back(tr);
                        }
                    }
                    if (body.contains("removed") && body["removed"].is_array()) {
                        for (const auto& t : body["removed"]) {
                            Triple tr;
                            tr.subject = t.value("subject", "");
                            tr.predicate = t.value("predicate", "");
                            tr.object = t.value("object", "");
                            removed.push_back(tr);
                        }
                    }
                    report = ctx_->shaclValidator->validateIncremental(added, removed);
                } else {
                    // Validate entire ontology
                    report = ctx_->shaclValidator->validate();
                }

                jsonResponse(res, report.toJson());
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });
    }
};

std::shared_ptr<ApiHandler> createShaclHandler() {
    return std::make_shared<ShaclHandler>();
}

} // namespace ontology
