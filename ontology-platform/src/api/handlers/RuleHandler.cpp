#include <ontology/ApiHandler.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

class RuleHandler : public ApiHandler {
public:
    String name() const override { return "RuleHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== SWRL Rule management ====================
        server.Get("/api/rules", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->swrlEngine) {
                errorResponse(res, 500, "SWRL engine not initialized");
                return;
            }

            auto rules = ctx_->swrlEngine->getRules();
            Json j = Json::array();
            for (const auto& rule : rules) {
                j.push_back(rule.toJson());
            }
            jsonResponse(res, j);
        });

        server.Post("/api/rules", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->swrlEngine) {
                errorResponse(res, 500, "SWRL engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                SwrlRule rule = SwrlRule::fromJson(body);
                ctx_->swrlEngine->addRule(rule);
                res.status = 201;
                jsonResponse(res, rule.toJson());
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        server.Delete("/api/rules/:id", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->swrlEngine) {
                errorResponse(res, 500, "SWRL engine not initialized");
                return;
            }

            String id = req.path_params.at("id");
            ctx_->swrlEngine->removeRule(id);
            res.status = 204;
        });

        server.Post("/api/rules/infer", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->swrlEngine) {
                errorResponse(res, 500, "SWRL engine not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                int maxIterations = body.value("maxIterations", 100);
                auto inferred = ctx_->swrlEngine->infer(maxIterations);

                Json j;
                j["inferred"] = Json::array();
                for (const auto& t : inferred) {
                    Json fact;
                    fact["subject"] = t.subject;
                    fact["predicate"] = t.predicate;
                    fact["object"] = t.object;
                    fact["confidence"] = t.confidence;
                    j["inferred"].push_back(fact);
                }
                j["count"] = inferred.size();
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

std::shared_ptr<ApiHandler> createRuleHandler() {
    return std::make_shared<RuleHandler>();
}

} // namespace ontology
